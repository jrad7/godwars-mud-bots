/*
 * bot_ai_monk.c - Monk class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_MONK.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_MONK].
 *
 * Progression overview:
 *   - Fight styles (learn fight <name>, 50K exp each)
 *   - Techniques (learn techniques <name>, 200K exp each)
 *   - Core abilities (learn abilities <name>, 500K exp each)
 *   - Chi mastery (learn chi, scales (level+1)*1M exp per level)
 *   - Mantras (mantra power improve, (level+1)*10 primal each)
 *
 * Combat loop:
 *   - Focus chi at the start of every fight (primary damage multiplier)
 *   - Use technique combos (thrust/spin for lightning kick, shin/knee/spin for
 *     tornado kick) to trigger finishers via the monkcrap combo state machine
 *   - Emergency godsheal when HP drops below 40%
 *   - wrathofgod (4-hit burst) and darkblaze (blind) as situational abilities
 *
 * Passive buffs maintained between fights:
 *   godseye, adamantium, flaminghands, steelskin, spiritpower,
 *   godsfavor, chaoshands, cloak
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
#include "monk.h"
#include "bot.h"

/* Index into ch->stance[] for the autostance setting */
#define MONK_AUTODROP  12

/* Forward declaration - defined in kav_fight.c */
void do_stance( CHAR_DATA *ch, char *argument );

/* -----------------------------------------------------------------------
 * bot_monk_pick_train
 *
 * Evaluates the training priority list and returns the command string for
 * the next affordable training step, or NULL if nothing can be purchased.
 *
 * Priority:
 *  1.  Mantras 1-4   (primal; godseye, shield/scry, sacredinvis, wrathofgod)
 *  2.  Techniques: shin, knee, thrust, spin  (unlock core combo chains)
 *  3.  Chi 1-2       (1.2x damage multiplier begins at chi 1)
 *  4.  Fight styles: kick, trip, bash, knee, elbow
 *  5.  Techniques: sweep, elbow, backfist, palm
 *  6.  Mantras 5-9   (flaminghands through chaoshands)
 *  7.  Body ability 1-3  (adamantium at 1; spiritpower at 3 = +200 hit/dam)
 *  8.  Chi 3         (1.5x multiplier)
 *  9.  Spirit ability 1-4  (healingtouch at 3; deathtouch at 4)
 * 10.  Remaining fight styles
 * 11.  Mantras 10-14
 * 12.  Combat and Aware abilities
 * 13.  Body/Spirit to max (4)
 * 14.  Chi 4-6       (2x / 2.5x / 3x multiplier)
 * ----------------------------------------------------------------------- */

/* Returns TRUE when every slot in the monk gear table has a class-gear piece
 * equipped (vnum 33000-33199).  Used to gate mantra spending so primal isn't
 * consumed before gear is complete. */
static bool bot_monk_gear_complete( CHAR_DATA *ch )
{
    const BOT_GEAR_PIECE *entry;
    OBJ_DATA             *obj;

    for ( entry = bot_class_gear[BOT_CLASS_MONK]; entry->wear_slot != WEAR_NONE; entry++ )
    {
        obj = get_eq_char( ch, entry->wear_slot );
        if ( obj == NULL
          || obj->pIndexData->vnum < 33000
          || obj->pIndexData->vnum > 33199 )
            return FALSE;
    }
    return TRUE;
}

static const char *bot_monk_pick_train( CHAR_DATA *ch )
{
    /* rest_fs: remaining fight styles, checked at step 10 */
    static const struct { int bit; const char *cmd; } rest_fs[] = {
        { FS_HEADBUTT, "learn fight headbutt" },
        { FS_DISARM,   "learn fight disarm"   },
        { FS_BITE,     "learn fight bite"      },
        { FS_DIRT,     "learn fight dirt"      },
        { FS_GRAPPLE,  "learn fight grapple"   },
        { FS_PUNCH,    "learn fight punch"     },
        { FS_GOUGE,    "learn fight gouge"     },
        { FS_RIP,      "learn fight rip"       },
        { FS_STAMP,    "learn fight stamp"     },
        { FS_BACKFIST, "learn fight backfist"  },
        { FS_JUMPKICK, "learn fight jumpkick"  },
        { FS_SPINKICK, "learn fight spinkick"  },
        { FS_HURL,     "learn fight hurl"      },
        { FS_SWEEP,    "learn fight sweep"     },
        { FS_CHARGE,   "learn fight charge"    },
        { -1, NULL }
    };

    int pmonk;
    int mantra_cost;
    int chi_cost;
    int i;

    if ( ch->pcdata == NULL ) return NULL;

    /* Class gear costs primal (60/piece) and must be finished before any
     * training.  Don't spend exp on chi or techniques while primal is still
     * needed for gear slots. */
    if ( !bot_monk_gear_complete(ch) ) return NULL;

    pmonk       = ch->pcdata->powers[PMONK];
    mantra_cost = (pmonk + 1) * 10;

    /* --- 1. Mantras 1-4 (primal) --- */
    if ( pmonk < 4 && ch->practice >= mantra_cost )
        return "mantra power improve";

    /* --- 2. Core techniques (unlock combo chains) --- */
    if ( !IS_FS(ch, TECH_SHIN)   && ch->exp >= 200000 ) return "learn techniques shin";
    if ( !IS_FS(ch, TECH_KNEE)   && ch->exp >= 200000 ) return "learn techniques knee";
    if ( !IS_FS(ch, TECH_THRUST) && ch->exp >= 200000 ) return "learn techniques thrust";
    if ( !IS_FS(ch, TECH_SPIN)   && ch->exp >= 200000 ) return "learn techniques spin";

    /* --- 3. Chi 1-2 --- */
    if ( ch->chi[MAXIMUM] < 2 )
    {
        chi_cost = (ch->chi[MAXIMUM] + 1) * 1000000;
        if ( ch->exp >= chi_cost ) return "learn chi chi";
    }

    /* --- 4. Key fight styles --- */
    if ( !IS_FS(ch, FS_KICK)  && ch->exp >= 50000 ) return "learn fight kick";
    if ( !IS_FS(ch, FS_TRIP)  && ch->exp >= 50000 ) return "learn fight trip";
    if ( !IS_FS(ch, FS_BASH)  && ch->exp >= 50000 ) return "learn fight bash";
    if ( !IS_FS(ch, FS_KNEE)  && ch->exp >= 50000 ) return "learn fight knee";
    if ( !IS_FS(ch, FS_ELBOW) && ch->exp >= 50000 ) return "learn fight elbow";

    /* --- 5. Remaining techniques --- */
    if ( !IS_FS(ch, TECH_SWEEP) && ch->exp >= 200000 ) return "learn techniques sweep";
    if ( !IS_FS(ch, TECH_ELBOW) && ch->exp >= 200000 ) return "learn techniques elbow";
    if ( !IS_FS(ch, TECH_BACK)  && ch->exp >= 200000 ) return "learn techniques backfist";
    if ( !IS_FS(ch, TECH_PALM)  && ch->exp >= 200000 ) return "learn techniques palm";

    /* --- 6. Mantras 5-9 (recompute cost for current pmonk) --- */
    mantra_cost = (pmonk + 1) * 10;
    if ( pmonk >= 4 && pmonk < 9 && ch->practice >= mantra_cost )
        return "mantra power improve";

    /* --- 7. Body ability 1-3 (adamantium at 1, spiritpower at 3) --- */
    if ( ch->monkab[BODY] < 3 && ch->exp >= 500000 )
        return "learn abilities body";

    /* --- 8. Chi 3 (1.5x multiplier) --- */
    if ( ch->chi[MAXIMUM] < 3 )
    {
        chi_cost = (ch->chi[MAXIMUM] + 1) * 1000000;
        if ( ch->exp >= chi_cost ) return "learn chi chi";
    }

    /* --- 9. Spirit ability 1-4 (healingtouch at 3, deathtouch at 4) --- */
    if ( ch->monkab[SPIRIT] < 4 && ch->exp >= 500000 )
        return "learn abilities spirit";

    /* --- 10. Remaining fight styles --- */
    for ( i = 0; rest_fs[i].bit != -1; i++ )
    {
        if ( !IS_FS(ch, rest_fs[i].bit) && ch->exp >= 50000 )
            return rest_fs[i].cmd;
    }

    /* --- 11. Mantras 10-14 --- */
    mantra_cost = (pmonk + 1) * 10;
    if ( pmonk >= 9 && pmonk < 14 && ch->practice >= mantra_cost )
        return "mantra power improve";

    /* --- 12. Combat and Aware abilities --- */
    if ( ch->monkab[COMBAT] < 4 && ch->exp >= 500000 )
        return "learn abilities combat";
    if ( ch->monkab[AWARE]  < 4 && ch->exp >= 500000 )
        return "learn abilities awareness";

    /* --- 13. Body and Spirit to max (4) --- */
    if ( ch->monkab[BODY]   < 4 && ch->exp >= 500000 ) return "learn abilities body";
    if ( ch->monkab[SPIRIT] < 4 && ch->exp >= 500000 ) return "learn abilities spirit";

    /* --- 14. Max chi (4-6) --- */
    if ( ch->chi[MAXIMUM] < 6 )
    {
        chi_cost = (ch->chi[MAXIMUM] + 1) * 1000000;
        if ( ch->exp >= chi_cost ) return "learn chi chi";
    }

    return NULL;   /* nothing currently affordable */
}

/* -----------------------------------------------------------------------
 * bot_monk_primal_needed
 *
 * Returns the primal cost of the next pending mantra step, or 0 if all
 * mantras are maxed or gear isn't complete yet.
 * Used by bot_primal_target() to raise the accumulation goal so the bot
 * can afford mantras 7-14 whose cost exceeds 60 primal.
 * Mantra N->N+1 costs (N+1)*10 primal; max is level 13->14 = 140 primal.
 * ----------------------------------------------------------------------- */
int bot_monk_primal_needed( CHAR_DATA *ch )
{
    int pmonk;

    if ( !IS_CLASS(ch, CLASS_MONK) || ch->pcdata == NULL ) return 0;
    if ( !bot_monk_gear_complete(ch) ) return 0;

    pmonk = ch->pcdata->powers[PMONK];
    if ( pmonk >= 14 ) return 0;

    return (pmonk + 1) * 10;
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE if there is a training step the bot can afford right now.
 * ----------------------------------------------------------------------- */

static bool bot_monk_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_MONK) ) return FALSE;
    return bot_monk_pick_train( ch ) != NULL;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Executes one monk-specific training step.
 * Returns TRUE if a command was issued.
 * Called by the bot state machine before generic hp/mana/move spending.
 * ----------------------------------------------------------------------- */

static bool bot_monk_do_train( CHAR_DATA *ch )
{
    const char *cmd;

    if ( !IS_CLASS(ch, CLASS_MONK) ) return FALSE;

    cmd = bot_monk_pick_train( ch );
    if ( cmd == NULL ) return FALSE;

    bot_cmd( ch, cmd );
    return TRUE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Ensures all passive monk buffs are active.
 * Issues at most one command per call; returns TRUE when it does.
 * Safe to call between fights (all checked buffs persist indefinitely).
 *
 * Priority (most universally useful first):
 *   godseye       - truesight; always on once available
 *   adamantium    - hardened hands; always on once available
 *   flaminghands  - fire damage; always on once available
 *   steelskin     - damage shield; always on once available
 *   spiritpower   - +200 hitroll/damroll; costs 25 move (persists)
 *   godsfavor     - Almighty's blessing; costs 1500 move (persists)
 *   chaoshands    - all 5 elemental shields; always on once available
 *   cloak         - Cloak of Life; costs 1000 move (persists)
 * ----------------------------------------------------------------------- */

static bool bot_monk_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_MONK) ) return FALSE;

    /* Godseye (Mantra 1) - truesight: see invisible and hidden */
    if ( ch->pcdata->powers[PMONK] >= 1
      && !IS_SET(ch->act, PLR_HOLYLIGHT) )
    { bot_cmd( ch, "godseye" ); return TRUE; }

    /* Adamantium (Body >= 1) - hardens hands for extra combat damage */
    if ( ch->monkab[BODY] >= 1
      && !IS_SET(ch->newbits, NEW_MONKADAM) )
    { bot_cmd( ch, "adamantium" ); return TRUE; }

    /* Flaming Hands (Mantra 5) - adds fire damage to unarmed strikes */
    if ( ch->pcdata->powers[PMONK] >= 5
      && !IS_SET(ch->newbits, NEW_MONKFLAME) )
    { bot_cmd( ch, "flaminghands" ); return TRUE; }

    /* Steelskin (Mantra 6) - defensive shield reduces incoming damage */
    if ( ch->pcdata->powers[PMONK] >= 6
      && !IS_SET(ch->newbits, NEW_MONKSKIN) )
    { bot_cmd( ch, "steelskin" ); return TRUE; }

    /* Spiritpower (Body >= 3) - +200 hitroll and damroll, costs 25 move */
    if ( ch->monkab[BODY] >= 3
      && !IS_SET(ch->newbits, NEW_POWER)
      && ch->move >= 100 )
    { bot_cmd( ch, "spiritpower" ); return TRUE; }

    /* God's Favor (Mantra 8) - Almighty's blessing, costs 1500 move */
    if ( ch->pcdata->powers[PMONK] >= 8
      && !IS_SET(ch->newbits, NEW_MONKFAVOR)
      && ch->move >= 1500 )
    { bot_cmd( ch, "godsfavor" ); return TRUE; }

    /* Chaos Hands (Mantra 9) - all five elemental shields at once */
    if ( ch->pcdata->powers[PMONK] >= 9
      && !IS_ITEMAFF(ch, ITEMA_CHAOSHANDS) )
    { bot_cmd( ch, "chaoshands" ); return TRUE; }

    /* Cloak of Life (Mantra 11) - protective aura, costs 1000 move */
    if ( ch->pcdata->powers[PMONK] >= 11
      && !IS_SET(ch->newbits, NEW_MONKCLOAK)
      && ch->move >= 1000 )
    { bot_cmd( ch, "cloak" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one combat action per tick using probability-based selection.
 * Called each pulse while the bot is in POS_FIGHTING.
 *
 * Technique combos are triggered by the monkcrap state machine inside
 * the do_shinkick/do_knee/do_thrustkick/do_spinkick functions.  The bot
 * just needs to mix techniques in a natural ratio; the game engine fires
 * combo finishers automatically when the state aligns.
 *
 * Priority:
 *   0  Chi focus     - raise chi[CURRENT] to chi[MAXIMUM] before attacking;
 *                       chi is the primary damage multiplier (up to 3x at 6)
 *   1  God's Heal    - emergency HP recovery when below 40%
 *   2  Wrath of God  - 4-hit burst vs NPCs (Mantra 4)
 *   3  Dark Blaze    - blind + strip detection, long lockout (Mantra 8)
 *   4  Thrust Kick   - initiates/advances COMB_THRUST1/2 for lightning kick
 *   5  Spin Kick     - completes thrust combo (lightning kick) or
 *                       shin/knee combo (tornado kick)
 *   6  Shin Kick     - initiates COMB_SHIN for the tornado kick chain
 *   7  Knee Strike   - advances COMB_KNEE in the combo chain
 *   8  Elbow Strike  - reliable damage filler
 *   9  Backfist      - reliable damage filler
 * ----------------------------------------------------------------------- */

static void bot_monk_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA *target = ch->fighting;
    int        roll;

    if ( target == NULL || ch->pcdata == NULL ) return;

    roll = number_range( 1, 100 );

    /* Priority 0: Focus chi to maximum before expending other abilities.
     * Chi is the monk's primary damage multiplier:
     *   chi 1-2: 1.2x | chi 3: 1.5x | chi 4: 2x | chi 5: 2.5x | chi 6: 3x
     * Cost: 500 + (chi[CURRENT]+1)*20 move per activation step. */
    if ( ch->chi[CURRENT] < ch->chi[MAXIMUM]
      && ch->move >= 500 + ( (ch->chi[CURRENT] + 1) * 20 ) )
    {
        bot_cmd( ch, "chi" );
        return;
    }

    /* Priority 1 (always when triggered): God's Heal when HP < 40%.
     * Restores 150 HP in combat, 400 HP out.  Costs 400 mana. */
    if ( ch->pcdata->powers[PMONK] >= 12
      && ch->mana >= 400
      && ch->hit < ( ch->max_hit * 2 / 5 ) )
    {
        bot_cmd( ch, "godsheal" );
        return;
    }

    /* Priority 2 (30%): Wrath of God - 4x one_hit, NPCs only. */
    if ( ch->pcdata->powers[PMONK] >= 4
      && IS_NPC(target)
      && roll <= 30 )
    {
        bot_cmd( ch, "wrathofgod" );
        return;
    }

    /* Priority 3 (45%): Dark Blaze - blinds target and strips detect
     * invis/hidden.  Long wait state (18) so use at moderate probability. */
    if ( ch->pcdata->powers[PMONK] >= 8
      && roll <= 45 )
    {
        bot_cmd( ch, "darkblaze" );
        return;
    }

    /* Priority 4 (55%): Thrust Kick - starts or advances the thrust combo.
     * Two thrustkicks set COMB_THRUST1+COMB_THRUST2; spinkick then fires
     * the lightning kick (multi-hit scaled by chi) or raptor strike
     * (siphons victim's mana). */
    if ( IS_FS(ch, TECH_THRUST)
      && roll <= 55 )
    {
        bot_cmd( ch, "thrustkick" );
        return;
    }

    /* Priority 5 (65%): Spin Kick - combo finisher.
     * With COMB_THRUST1+COMB_THRUST2: lightning kick (up to 6 hits).
     * With COMB_SHIN+COMB_KNEE: tornado kick (hits all room chars). */
    if ( IS_FS(ch, TECH_SPIN)
      && roll <= 65 )
    {
        bot_cmd( ch, "spinkick" );
        return;
    }

    /* Priority 6 (75%): Shin Kick - starts the shin/knee tornado chain.
     * Sets COMB_SHIN; follow with knee then spinkick for tornado kick. */
    if ( IS_FS(ch, TECH_SHIN)
      && roll <= 75 )
    {
        bot_cmd( ch, "shinkick" );
        return;
    }

    /* Priority 7 (82%): Knee Strike - advances COMB_KNEE in the chain. */
    if ( IS_FS(ch, TECH_KNEE)
      && roll <= 82 )
    {
        bot_cmd( ch, "knee" );
        return;
    }

    /* Priority 8 (88%): Elbow Strike - reliable damage filler. */
    if ( IS_FS(ch, TECH_ELBOW)
      && roll <= 88 )
    {
        bot_cmd( ch, "elbow" );
        return;
    }

    /* Priority 9 (93%): Backfist - reliable damage filler. */
    if ( IS_FS(ch, TECH_BACK)
      && roll <= 93 )
    {
        bot_cmd( ch, "backfist" );
        return;
    }

    /* Fallback: basic combat continues via normal multi_hit loop */
}

/* -----------------------------------------------------------------------
 * Vtable: between_fights
 *
 * Called each tick the bot is grinding but not yet in combat.
 * Advances the training stance when the current one is mastered and
 * relaxes from the old stance so autodrop() re-enters the new one on
 * the next fight.
 *
 * Returns TRUE when a command was issued (skip mob-hunting this tick).
 * ----------------------------------------------------------------------- */

static bool bot_monk_between_fights( CHAR_DATA *ch )
{
    /* bot_set_autostance() is called for all classes in bot_state_grinding.
     * Only drop the stance when it differs from the autostance — this lets
     * autodrop() re-enter the updated stance on the next fight.  If the bot
     * is already in the correct autostance there is no reason to drop it,
     * which avoids fighting with no stance when the next mob is found quickly. */
    if ( ch->stance[0] > 0
      && ch->stance[0] != ch->stance[MONK_AUTODROP] )
    {
        do_stance( ch, "" );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_monk_ai = {
    bot_monk_should_train,   /* should_train   */
    bot_monk_do_train,       /* do_train       */
    bot_monk_buff_check,     /* buff_check     */
    bot_monk_combat_action,  /* combat_action  */
    bot_monk_between_fights  /* between_fights */
};
