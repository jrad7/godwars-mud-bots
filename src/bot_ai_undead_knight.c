/*
 * bot_ai_undead_knight.c - Undead Knight class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_UNDEAD_KNIGHT.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_UNDEAD_KNIGHT].
 *
 * Undead Knight is a Vampire upgrade class with 4 power tracks:
 *   NECROMANCY   (powers[1]) - auras, command, cloak of death (0-10)
 *   INVOCATION   (powers[2]) - powerword offensive spells       (0-5)
 *   UNDEAD_SPIRIT(powers[3]) - passive damage reduction         (0-10)
 *   WEAPONSKILL  (powers[4]) - extra attacks, damage, parry     (0-10)
 *
 * All tracks trained with primal (ch->practice).
 * Training cost level N->N+1: (N * 60) + 60 primal.
 * weaponpractice sets HP/mana/move to 1 on use — only call from TRAINING state.
 *
 * Toggle auras (powers[AURAS] bitmask):
 *   BOG_AURA   (1) - 70% trap fleeing enemies          (NECROMANCY >= 6)
 *   DEATH_AURA (2) - retaliation hits each round       (NECROMANCY >= 2)
 *   FEAR_AURA  (4) - -20 hit/dam debuff on attackers   (NECROMANCY >= 9)
 *   MIGHT_AURA (8) - +300 damroll/hitroll              (NECROMANCY >= 4)
 *
 * IMPORTANT: MIGHT_AURA is stripped on death in jobo_util.c. buff_check
 *   must re-enable it after every death and login.
 *
 * Active combat abilities (POS_FIGHTING):
 *   powerword stun  - INVOCATION >= 5, POWER_TICK == 0; freeze 6s (cooldown 4)
 *   powerword kill  - INVOCATION >= 3, POWER_TICK == 0; 10% current HP   (cooldown 2)
 *   soulsuck        - SPIRIT >= 4, alignment < 0, vs PC only; drain + self-heal
 *
 * Between-fight healing (POS_STANDING):
 *   bloodrite - heals 500-1000 HP for 500 mana; use when HP < 80%
 *
 * Alignment: bots must be evil (alignment < 0) to use soulsuck.
 *   buff_check sets ch->alignment = -1000 directly if not already evil.
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
#include "undead_knight.h"
#include "bot.h"

/* -----------------------------------------------------------------------
 * Training priority table
 *
 * Ordered by combat impact. Each entry: track to train and target level.
 * Bot trains that step when powers[track] < target_level and has enough
 * primal for the next level.
 *
 * Priority rationale:
 *   WEAPONSKILL 1-5 first — first damage multiplier unlocks at > 4
 *   NECROMANCY 1-4 early  — death_aura (2) and might_aura (4) are large gains
 *   WEAPONSKILL 6-10      — second multiplier at > 8; lightning slash at > 9
 *   SPIRIT 1-4            — soulsuck unlocks at 4; early damage reduction
 *   NECROMANCY 5-6        — bog_aura: keeps enemies from fleeing in PvE/PvP
 *   INVOCATION 1-3        — powerword kill
 *   SPIRIT 5-10           — max damage reduction (54% at level 10)
 *   INVOCATION 4-5        — powerword stun; strongest powerword
 *   NECROMANCY 7-10       — fear_aura at 9; cloak of death at 10
 * ----------------------------------------------------------------------- */
static const struct {
    int track;         /* NECROMANCY/INVOCATION/UNDEAD_SPIRIT/WEAPONSKILL index */
    int target_level;  /* train when powers[track] < this value                 */
} train_order[] = {
    { WEAPONSKILL,     5  },  /* first dam mult (>4): +20% dam              */
    { NECROMANCY,      2  },  /* death_aura: free retaliation every round   */
    { NECROMANCY,      4  },  /* might_aura: +300 damroll/hitroll           */
    { WEAPONSKILL,    10  },  /* second mult (>8), lightning slash (>9)     */
    { UNDEAD_SPIRIT,   4  },  /* soulsuck unlock; early dam reduction       */
    { NECROMANCY,      6  },  /* bog_aura: 70% flee prevention              */
    { INVOCATION,      3  },  /* powerword kill: 10% current HP             */
    { UNDEAD_SPIRIT,  10  },  /* max damage reduction (54%)                 */
    { INVOCATION,      5  },  /* powerword stun: freeze target ~6 seconds   */
    { NECROMANCY,     10  },  /* fear_aura at 9; cloak of death at 10       */
    { -1, 0 }
};

/* -----------------------------------------------------------------------
 * bot_uk_track_cmd
 *
 * Returns the command string to issue for a given track.
 * WEAPONSKILL uses "weaponpractice"; other tracks use "gain <name>".
 * ----------------------------------------------------------------------- */
static const char *bot_uk_track_cmd( int track )
{
    switch ( track )
    {
        case WEAPONSKILL:   return "weaponpractice";
        case NECROMANCY:    return "gain necromancy";
        case INVOCATION:    return "gain invocation";
        case UNDEAD_SPIRIT: return "gain spirit";
        default:            return "";
    }
}

/* -----------------------------------------------------------------------
 * bot_uk_train_cost
 *
 * Returns primal cost to advance powers[track] by one level.
 * Formula: (current_level * 60) + 60
 * ----------------------------------------------------------------------- */
static int bot_uk_train_cost( int current_level )
{
    return ( current_level * 60 ) + 60;
}

/* -----------------------------------------------------------------------
 * bot_uk_primal_needed
 *
 * Returns the primal cost of the next pending training step, or 0 if all
 * training is complete.  Used by bot_primal_target() to set a high enough
 * accumulation goal so the bot can afford its next gain/weaponpractice.
 * ----------------------------------------------------------------------- */
int bot_uk_primal_needed( CHAR_DATA *ch )
{
    int i, cur;

    if ( !IS_CLASS(ch, CLASS_UNDEAD_KNIGHT) ) return 0;

    for ( i = 0; train_order[i].track != -1; i++ )
    {
        cur = ch->pcdata->powers[ train_order[i].track ];
        if ( cur >= train_order[i].target_level ) continue;
        return bot_uk_train_cost( cur );  /* cost of the very next pending step */
    }

    return 0;  /* all tracks fully trained */
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE if the bot has a pending training step it can afford.
 * ----------------------------------------------------------------------- */
static bool bot_uk_should_train( CHAR_DATA *ch )
{
    int i, cur, cost;

    if ( !IS_CLASS(ch, CLASS_UNDEAD_KNIGHT) ) return FALSE;

    for ( i = 0; train_order[i].track != -1; i++ )
    {
        cur = ch->pcdata->powers[ train_order[i].track ];
        if ( cur >= train_order[i].target_level ) continue;
        cost = bot_uk_train_cost( cur );
        if ( ch->practice >= cost ) return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Issues the next affordable training command from the priority table.
 * Returns TRUE when a command is issued.
 *
 * Note: "weaponpractice" sets HP/mana/move to 1. This is safe because
 * do_train is only called from the TRAINING state; the bot transitions
 * to RESTING when HP is low after the command fires.
 * ----------------------------------------------------------------------- */
static bool bot_uk_do_train( CHAR_DATA *ch )
{
    int i, cur, cost;

    if ( !IS_CLASS(ch, CLASS_UNDEAD_KNIGHT) ) return FALSE;

    for ( i = 0; train_order[i].track != -1; i++ )
    {
        cur = ch->pcdata->powers[ train_order[i].track ];
        if ( cur >= train_order[i].target_level ) continue;
        cost = bot_uk_train_cost( cur );
        if ( ch->practice < cost ) continue;

        bot_cmd( ch, bot_uk_track_cmd( train_order[i].track ) );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Ensures alignment is evil and all toggle buffs are active.
 * Issues at most one command per call; returns TRUE when it does.
 * Called each grinding tick between fights.
 *
 * Buff priority (highest first):
 *   0. Force evil alignment so soulsuck works (direct assignment, no cmd)
 *   1. unholysight  - PLR_HOLYLIGHT true-see (no level requirement)
 *   2. aura death   - retaliation hits every round (NECROMANCY >= 2)
 *   3. aura might   - +300 damroll/hitroll (NECROMANCY >= 4); stripped on death
 *   4. aura bog     - 70% flee prevention for enemies (NECROMANCY >= 6)
 *   5. aura fear    - -20 hit/dam debuff on attackers (NECROMANCY >= 9)
 * ----------------------------------------------------------------------- */
static bool bot_uk_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_UNDEAD_KNIGHT) ) return FALSE;

    /* Step 0: ensure evil alignment for soulsuck — no command needed */
    if ( ch->alignment >= 0 )
        ch->alignment = -1000;

    /* unholysight: true-see and detect hidden/invisible */
    if ( !IS_SET(ch->act, PLR_HOLYLIGHT) )
    { bot_cmd( ch, "unholysight" ); return TRUE; }

    /* aura death: retaliation hits against every attacker (NECROMANCY >= 2) */
    if ( ch->pcdata->powers[NECROMANCY] >= 2
      && !IS_SET(ch->pcdata->powers[AURAS], DEATH_AURA) )
    { bot_cmd( ch, "aura death" ); return TRUE; }

    /* aura might: +300 damroll/hitroll (NECROMANCY >= 4); re-enable after death */
    if ( ch->pcdata->powers[NECROMANCY] >= 4
      && !IS_SET(ch->pcdata->powers[AURAS], MIGHT_AURA) )
    { bot_cmd( ch, "aura might" ); return TRUE; }

    /* aura bog: 70% chance to trap fleeing opponents (NECROMANCY >= 6) */
    if ( ch->pcdata->powers[NECROMANCY] >= 6
      && !IS_SET(ch->pcdata->powers[AURAS], BOG_AURA) )
    { bot_cmd( ch, "aura bog" ); return TRUE; }

    /* aura fear: -20 hit/dam debuff on attackers (NECROMANCY >= 9) */
    if ( ch->pcdata->powers[NECROMANCY] >= 9
      && !IS_SET(ch->pcdata->powers[AURAS], FEAR_AURA) )
    { bot_cmd( ch, "aura fear" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: between_fights
 *
 * Between-fight recovery. Called each tick when not in combat.
 * Returns TRUE when a command is sent.
 *
 * bloodrite: heals 500-1000 HP for 500 mana (POS_STANDING).
 *   Use when HP is below 80% and mana is available. Fastest self-heal
 *   available between pulls; avoids wasted resting time in regen rooms.
 * ----------------------------------------------------------------------- */
static bool bot_uk_between_fights( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_UNDEAD_KNIGHT) ) return FALSE;

    /* bloodrite: 500-1000 HP heal for 500 mana */
    if ( ch->hit < ch->max_hit * 80 / 100
      && ch->mana >= 500 )
    { bot_cmd( ch, "bloodrite" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one active POS_FIGHTING ability per tick.
 * Called each pulse while ch->position == POS_FIGHTING.
 *
 * Most of UK's damage output comes from passives (WEAPONSKILL attack count,
 * WEAPONSKILL damage multipliers, DEATH_AURA retaliation, SPIRIT reduction),
 * so active abilities focus on burst and PvP control.
 *
 * Priority order:
 *   1. powerword stun  - INVOCATION >= 5, POWER_TICK == 0
 *                        Freeze target for ~6 seconds (WAIT_STATE 24).
 *                        Cooldown 4 ticks after use.
 *   2. powerword kill  - INVOCATION >= 3, POWER_TICK == 0
 *                        10% of target's current HP (cap 1500 PC / 5000 NPC).
 *                        Slays mobs under level 100 outright. Cooldown 2 ticks.
 *   3. soulsuck        - SPIRIT >= 4, alignment < 0, vs PC only.
 *                        Deals 250-1000 damage and heals UK for same amount.
 *                        No cooldown counter — random fire to avoid spam.
 *
 * Powerword notes:
 *   Both stun and kill share the POWER_TICK counter. Stun is preferred when
 *   available (INVOCATION 5) because the freeze gives more combat advantage
 *   than the percentage damage of kill. When stun is unlocked and POWER_TICK
 *   is 0, always use stun. Otherwise fall through to kill if available.
 *
 * soulsuck notes:
 *   PC targets only — the game returns early on NPCs. We guard here to avoid
 *   burning a tick on a rejected command during normal PvE grinding.
 *   Fires at 35% probability per tick to avoid being the only action taken.
 * ----------------------------------------------------------------------- */
static void bot_uk_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA  *target = ch->fighting;
    bool        vs_player;
    char        cmd[MAX_INPUT_LENGTH];

    if ( target == NULL ) return;

    vs_player = !IS_NPC(target);

    /* ----------------------------------------------------------------
     * Priority 1: powerword stun
     * Requires INVOCATION >= 5 and no active cooldown.
     * Always preferred over kill when available — the freeze is more
     * valuable than the percentage damage kill provides.
     * ---------------------------------------------------------------- */
    if ( ch->pcdata->powers[INVOCATION] >= 5
      && ch->pcdata->powers[POWER_TICK] == 0 )
    {
        sprintf( cmd, "powerword stun %s", target->name );
        bot_cmd( ch, cmd );
        return;
    }

    /* ----------------------------------------------------------------
     * Priority 2: powerword kill
     * Requires INVOCATION >= 3 and no active cooldown.
     * Effective against NPCs (percentage damage, instant-kills sub-100).
     * Also useful vs PCs for burst damage.
     * ---------------------------------------------------------------- */
    if ( ch->pcdata->powers[INVOCATION] >= 3
      && ch->pcdata->powers[POWER_TICK] == 0 )
    {
        sprintf( cmd, "powerword kill %s", target->name );
        bot_cmd( ch, cmd );
        return;
    }

    /* ----------------------------------------------------------------
     * Priority 3: soulsuck — PvP only, 35% chance per tick.
     * Requires SPIRIT >= 4, evil alignment, and a PC target.
     * Deals 250-1000 damage and heals UK for the same amount.
     * 35% rate prevents it dominating every tick while still firing often.
     * ---------------------------------------------------------------- */
    if ( vs_player
      && ch->pcdata->powers[UNDEAD_SPIRIT] >= 4
      && ch->alignment < 0
      && number_range( 1, 100 ) <= 35 )
    {
        sprintf( cmd, "soulsuck %s", target->name );
        bot_cmd( ch, cmd );
        return;
    }

    /* Fallback: normal multi_hit loop handles base damage output */
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_undead_knight_ai = {
    bot_uk_should_train,    /* should_train   */
    bot_uk_do_train,        /* do_train       */
    bot_uk_buff_check,      /* buff_check     */
    bot_uk_combat_action,   /* combat_action  */
    bot_uk_between_fights   /* between_fights */
};
