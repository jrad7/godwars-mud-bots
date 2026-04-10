/*
 * bot_ai_angel.c - Angel class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_ANGEL.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_ANGEL].
 *
 * Angel is a Monk upgrade class with 4 independent power tracks:
 *   ANGEL_PEACE (powers[1])   - defense, parry, self-heal
 *   ANGEL_LOVE  (powers[2])   - true-see, angel form, passive regen
 *   ANGEL_JUSTICE (powers[3]) - attacks/round, accuracy, mobility
 *   ANGEL_HARMONY (powers[4]) - damage reduction, aura hit, banish
 *
 * Each track has 5 levels. Training cost level N->N+1: (N+1)*10,000,000 exp.
 * Command: "train justice|love|harmony|peace"
 *
 * Toggle bits (powers[ANGEL_POWERS]):
 *   ANGEL_WINGS - trip immunity, enables swoop         (JUSTICE >= 1, POS_STANDING)
 *   ANGEL_HALO  - random spell proc each combat round  (JUSTICE >= 2, POS_STANDING)
 *   ANGEL_AURA  - extra heavenlyaura hit per attack    (HARMONY >= 2, POS_FIGHTING)
 *   ANGEL_EYE   - reflects incoming damage back        (JUSTICE >= 5, POS_FIGHTING)
 *                 costs 1600 move + 1600 mana/tick; PvP only
 *
 * Key passives (active once trained, no toggle):
 *   JUSTICE: +level attacks/round, -(level*9)% enemy dodge, damage scale
 *   PEACE:   +(level*9)% self dodge, damage reduction scaling
 *   HARMONY: incoming dam *= (100 - level*12) / 100  (up to 60% reduction)
 *   LOVE >= 4: auto-regen HP/mana/move each tick
 *
 * IMPORTANT: sinsofthepast, touchofgod, gbanish, forgiveness are PC-only.
 *   The game code returns early on IS_NPC targets. We guard here too to avoid
 *   wasting WAIT_STATE ticks on the "Not on mobiles" rejection.
 *
 * Combat abilities that are POS_FIGHTING only (use in combat_action):
 *   angelicaura, gbanish, harmony, sinsofthepast, eyeforaneye, touchofgod
 *
 * Combat abilities that are POS_STANDING only (use in buff_check):
 *   gpeace, gsenses, gfavor, awings, halo
 *
 * POS_STANDING heals (use in between_fights):
 *   innerpeace - heals ANGEL_PEACE*500 HP, costs 1500 mana, 18-tick delay
 */

#if defined(macintosh)
#include <types.h>
#else
#include <sys/types.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "merc.h"
#include "angel.h"
#include "bot.h"

/* -----------------------------------------------------------------------
 * Training priority table
 *
 * Ordered by combat impact. Each entry: the track to train and the target
 * level — bot trains that step when powers[track] < target_level and the
 * bot has enough exp for the next level.
 *
 * Priority rationale:
 *   JUSTICE first — most combat benefit (+attacks, accuracy, max_dam)
 *   LOVE 1->2 early — gfavor (+400 hit/dam) is a large immediate gain
 *   HARMONY 1->2 — angelicaura is a free extra hit per round
 *   Remaining tracks filled in descending combat value
 * ----------------------------------------------------------------------- */
static const struct {
    int track;         /* ANGEL_PEACE/LOVE/JUSTICE/HARMONY index */
    int target_level;  /* train when powers[track] < this value  */
} train_order[] = {
    { ANGEL_JUSTICE, 1 },   /* awings unlock, +1 attack/round           */
    { ANGEL_LOVE,    1 },   /* gsenses (prereq for love 2)              */
    { ANGEL_LOVE,    2 },   /* gfavor: +400 hit/dam angel form          */
    { ANGEL_JUSTICE, 2 },   /* halo combat spell procs                  */
    { ANGEL_HARMONY, 1 },   /* 12% dmg reduction + max_dam bonus        */
    { ANGEL_HARMONY, 2 },   /* angelicaura: free extra hit per attack   */
    { ANGEL_JUSTICE, 3 },   /* +3 attacks/round; sinsofthepast (PvP)   */
    { ANGEL_JUSTICE, 4 },   /* touchofgod stun (PvP); +4 attacks        */
    { ANGEL_JUSTICE, 5 },   /* eyeforaneye (PvP); +5 attacks; full max_dam */
    { ANGEL_HARMONY, 3 },   /* gbanish (PvP); 36% dmg reduction         */
    { ANGEL_HARMONY, 4 },   /* 48% dmg reduction                        */
    { ANGEL_HARMONY, 5 },   /* harmony spirit-kiss heal; 60% dmg reduce */
    { ANGEL_PEACE,   1 },   /* gpeace passive parry/dodge bonus         */
    { ANGEL_PEACE,   2 },   /* spiritform escape                        */
    { ANGEL_PEACE,   3 },   /* innerpeace: self-heal PEACE*500 HP       */
    { ANGEL_LOVE,    3 },   /* forgiveness ally-heal (PvP support)      */
    { ANGEL_LOVE,    4 },   /* passive auto-regen every tick            */
    { ANGEL_PEACE,   4 },   /* stronger innerpeace                      */
    { ANGEL_PEACE,   5 },   /* houseofgod AoE room heal                 */
    { ANGEL_LOVE,    5 },   /* martyr (trained but not used by bot)     */
    { -1, 0 }
};

/* -----------------------------------------------------------------------
 * bot_ang_track_name
 *
 * Returns the "train" argument string for a given track index.
 * ----------------------------------------------------------------------- */
static const char *bot_ang_track_name( int track )
{
    switch ( track )
    {
        case ANGEL_JUSTICE: return "justice";
        case ANGEL_LOVE:    return "love";
        case ANGEL_HARMONY: return "harmony";
        case ANGEL_PEACE:   return "peace";
        default:            return "";
    }
}

/* -----------------------------------------------------------------------
 * bot_ang_pool_exp
 *
 * Returns the exp threshold the bot must reach before spending on stats,
 * or 0 if no pooling is needed right now.
 *
 * Without this, bot_train_stats drains exp into HP each tick and the bot
 * never accumulates the 10M+ needed for the first training step.
 * ----------------------------------------------------------------------- */
long bot_ang_pool_exp( CHAR_DATA *ch )
{
    int i, cur;
    long cost;

    if ( !IS_CLASS(ch, CLASS_ANGEL) ) return 0;

    for ( i = 0; train_order[i].track != -1; i++ )
    {
        cur = ch->pcdata->powers[ train_order[i].track ];
        if ( cur >= train_order[i].target_level ) continue;
        cost = (long)(cur + 1) * 10000000L;
        if ( ch->exp < cost ) return cost;  /* pool until affordable */
        return 0;                            /* do_train will fire    */
    }

    return 0;  /* all steps complete */
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE if the bot has an affordable training step pending.
 * Cost to train track from level N to N+1: (N+1) * 10,000,000 exp.
 * ----------------------------------------------------------------------- */
static bool bot_ang_should_train( CHAR_DATA *ch )
{
    int i, cur;
    long cost;

    if ( !IS_CLASS(ch, CLASS_ANGEL) ) return FALSE;

    for ( i = 0; train_order[i].track != -1; i++ )
    {
        cur = ch->pcdata->powers[ train_order[i].track ];
        if ( cur >= train_order[i].target_level ) continue;
        cost = (long)(cur + 1) * 10000000L;
        if ( ch->exp >= cost ) return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Issues the next affordable training command from the priority table.
 * Returns TRUE when a command is issued.
 * ----------------------------------------------------------------------- */
static bool bot_ang_do_train( CHAR_DATA *ch )
{
    int i, cur;
    long cost;
    char cmd[64];

    if ( !IS_CLASS(ch, CLASS_ANGEL) ) return FALSE;

    for ( i = 0; train_order[i].track != -1; i++ )
    {
        cur = ch->pcdata->powers[ train_order[i].track ];
        if ( cur >= train_order[i].target_level ) continue;
        cost = (long)(cur + 1) * 10000000L;
        if ( ch->exp < cost ) continue;

        sprintf( cmd, "train %s", bot_ang_track_name(train_order[i].track) );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Ensures all POS_STANDING toggle buffs are active.
 * Issues at most one command per call; returns TRUE when it does.
 * Called each grinding tick between fights.
 *
 * Buff priority (highest first):
 *   1. gsenses  - PLR_HOLYLIGHT true-see (LOVE >= 1)
 *   2. awings   - ANGEL_WINGS trip immunity + swoop (JUSTICE >= 1)
 *   3. halo     - ANGEL_HALO random spell procs in combat (JUSTICE >= 2)
 *   4. gfavor   - angel form +400 hit/dam (LOVE >= 2); needs 2000 mana+move
 *   5. gpeace   - AFF_PEACE parry/dodge bonus (PEACE >= 1); needs mana buffer
 *
 * Notes:
 *   angelicaura (ANGEL_AURA) is POS_FIGHTING only — handled in combat_action.
 *   eyeforaneye (ANGEL_EYE) is PVP only — handled in combat_action.
 *   gfavor costs 2000 mana + 2000 move to activate but is free to maintain.
 *   gpeace costs 800 mana/tick to maintain; guard prevents enabling when low.
 * ----------------------------------------------------------------------- */
static bool bot_ang_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_ANGEL) ) return FALSE;

    /* gsenses: PLR_HOLYLIGHT true-see and detect hidden */
    if ( ch->pcdata->powers[ANGEL_LOVE] >= 1
      && !IS_SET(ch->act, PLR_HOLYLIGHT) )
    { bot_cmd( ch, "gsenses" ); return TRUE; }

    /* awings: trip immunity + enables swoop mobility */
    if ( ch->pcdata->powers[ANGEL_JUSTICE] >= 1
      && !IS_SET(ch->pcdata->powers[ANGEL_POWERS], ANGEL_WINGS) )
    { bot_cmd( ch, "awings" ); return TRUE; }

    /* halo: random spell proc (curse/web/heal/fireball/godbless) each round */
    if ( ch->pcdata->powers[ANGEL_JUSTICE] >= 2
      && !IS_SET(ch->pcdata->powers[ANGEL_POWERS], ANGEL_HALO) )
    { bot_cmd( ch, "halo" ); return TRUE; }

    /* gfavor: +400 damroll/hitroll angel form; re-enable if lost or not yet on */
    if ( ch->pcdata->powers[ANGEL_LOVE] >= 2
      && !IS_SET(ch->newbits, NEW_CUBEFORM)
      && ch->mana >= 2000
      && ch->move >= 2000 )
    { bot_cmd( ch, "gfavor" ); return TRUE; }

    /* gpeace: AFF_PEACE parry/dodge buff; guard against low mana so we don't
     * immediately drop from insufficient upkeep on the next tick */
    if ( ch->pcdata->powers[ANGEL_PEACE] >= 1
      && !IS_AFFECTED(ch, AFF_PEACE)
      && ch->mana > ch->max_mana / 2 )
    { bot_cmd( ch, "gpeace" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: between_fights
 *
 * Between-fight actions that don't fit the toggle pattern of buff_check.
 * Called each tick when not in combat. Returns TRUE when a command is sent.
 *
 * innerpeace: ANGEL_PEACE * 500 HP heal, costs 1500 mana, 18-tick lag.
 *   Used when HP is below 80% and we have mana to spare. The heal is
 *   modest at low PEACE levels but this is the only standing self-heal.
 * ----------------------------------------------------------------------- */
static bool bot_ang_between_fights( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_ANGEL) ) return FALSE;

    /* innerpeace: self-heal; only worth casting if HP is noticeably low */
    if ( ch->pcdata->powers[ANGEL_PEACE] >= 3
      && ch->hit < ch->max_hit * 80 / 100
      && ch->mana >= 1500 )
    { bot_cmd( ch, "innerpeace" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one active POS_FIGHTING ability per tick.
 * Called each pulse while ch->position == POS_FIGHTING.
 *
 * Most of Angel's damage output comes from passives (JUSTICE attack count,
 * HARMONY damage reduction, HALO procs, AURA multi-hit), so active
 * abilities are primarily PvP utilities and emergency healing.
 *
 * Priority order:
 *   0a. Enable angelicaura if not yet set (free extra hit every attack)
 *   0b. Drop eyeforaneye if active but resources too low to sustain
 *   0c. Enable eyeforaneye when fighting a PC and resources are healthy
 *   1.  PvP: touchofgod    - 100-200 dmg + 33% stun  (JUSTICE >= 4)
 *   2.  PvP: sinsofthepast - flaming+poison+wrathofgod (JUSTICE >= 3)
 *   3.  PvP: gbanish       - 500-1500 dmg + 30% teleport to hell (HARMONY >= 3)
 *   4.  All: harmony self  - spirit kiss heal at lvl 100-200 (HARMONY >= 5)
 *
 * EYEFORANEYE notes:
 *   When active, reflects dam/4 to dam/5 of each incoming hit back at the
 *   attacker, capped at 275-325. Costs 1600 move + 1600 mana per tick.
 *   Only worthwhile for PvP (sustained fights vs a single player). We enable
 *   it when mana and move are both above 60% and disable before they run dry.
 *
 * HARMONY (spirit kiss) notes:
 *   POS_FIGHTING only; targets a char in room. We target ourselves by name
 *   to use this as a combat self-heal when HP drops below 75%.
 * ----------------------------------------------------------------------- */
static void bot_ang_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA  *target = ch->fighting;
    int         roll;
    char        cmd[MAX_INPUT_LENGTH];
    const char *tname;
    bool        vs_player;

    if ( target == NULL ) return;

    vs_player = !IS_NPC(target);
    tname     = target->name;

    /* ----------------------------------------------------------------
     * Priority 0a: Enable angelicaura on the first tick of combat.
     * POS_FIGHTING only; free extra heavenlyaura multi_hit every attack.
     * ---------------------------------------------------------------- */
    if ( ch->pcdata->powers[ANGEL_HARMONY] >= 2
      && !IS_SET(ch->pcdata->powers[ANGEL_POWERS], ANGEL_AURA) )
    { bot_cmd( ch, "angelicaura" ); return; }

    /* ----------------------------------------------------------------
     * Priority 0b: Drop eyeforaneye if resources are getting low.
     * ANGEL_EYE costs 1600 move + 1600 mana per tick. Drop it early so
     * the bot doesn't run dry and lose the ability to flee or cast heals.
     * Thresholds: mana < 35% OR move < 35%.
     * ---------------------------------------------------------------- */
    if ( IS_SET(ch->pcdata->powers[ANGEL_POWERS], ANGEL_EYE)
      && ( ch->mana < ch->max_mana * 35 / 100
        || ch->move < ch->max_move * 35 / 100 ) )
    { bot_cmd( ch, "eyeforaneye" ); return; }   /* toggle off */

    /* ----------------------------------------------------------------
     * Priority 0c: Enable eyeforaneye at the start of a PvP fight.
     * Only when fighting a player and resources are healthy (> 60%).
     * ---------------------------------------------------------------- */
    if ( vs_player
      && ch->pcdata->powers[ANGEL_JUSTICE] >= 5
      && !IS_SET(ch->pcdata->powers[ANGEL_POWERS], ANGEL_EYE)
      && ch->mana > ch->max_mana * 60 / 100
      && ch->move > ch->max_move * 60 / 100 )
    { bot_cmd( ch, "eyeforaneye" ); return; }

    roll = number_range( 1, 100 );

    /* ----------------------------------------------------------------
     * Priorities 1-3: PvP-only abilities (PC targets only).
     * The game functions return early on IS_NPC targets; we guard here
     * to avoid burning WAIT_STATE ticks on rejected commands.
     * ---------------------------------------------------------------- */
    if ( vs_player )
    {
        /* Priority 1 (35%): touchofgod - 100-200 dmg + 33% stun */
        if ( ch->pcdata->powers[ANGEL_JUSTICE] >= 4
          && roll <= 35 )
        {
            sprintf( cmd, "touchofgod %s", tname );
            bot_cmd( ch, cmd );
            return;
        }

        /* Priority 2 (55%): sinsofthepast - AFF_FLAMING + AFF_POISON + wrathofgod hit */
        if ( ch->pcdata->powers[ANGEL_JUSTICE] >= 3
          && roll <= 55 )
        {
            sprintf( cmd, "sinsofthepast %s", tname );
            bot_cmd( ch, cmd );
            return;
        }

        /* Priority 3 (70%): gbanish - 500-1500 dmg + 30% teleport to hell.
         * Requires victim alignment <= 500 (not too good-aligned). */
        if ( ch->pcdata->powers[ANGEL_HARMONY] >= 3
          && roll <= 70
          && target->alignment <= 500 )
        {
            sprintf( cmd, "gbanish %s", tname );
            bot_cmd( ch, cmd );
            return;
        }
    }

    /* ----------------------------------------------------------------
     * Priority 4 (85%): harmony self - spirit kiss at level 100-200.
     * Available in PvE and PvP. Target ourselves by name for healing.
     * Only fire when HP is below 75% to avoid wasting the 12-tick lag.
     * ---------------------------------------------------------------- */
    if ( ch->pcdata->powers[ANGEL_HARMONY] >= 5
      && roll <= 85
      && ch->hit < ch->max_hit * 75 / 100 )
    {
        sprintf( cmd, "harmony %s", ch->name );
        bot_cmd( ch, cmd );
        return;
    }

    /* Fallback: normal multi_hit loop handles base damage output */
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_angel_ai = {
    bot_ang_should_train,   /* should_train   */
    bot_ang_do_train,       /* do_train       */
    bot_ang_buff_check,     /* buff_check     */
    bot_ang_combat_action,  /* combat_action  */
    bot_ang_between_fights  /* between_fights */
};
