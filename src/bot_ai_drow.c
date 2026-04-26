/*
 * bot_ai_drow.c - Drow class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_DROW.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_DROW].
 *
 * Functions here are intentionally file-scoped (static) except for the
 * exported vtable object at the bottom.  Do not call them directly from
 * other translation units; go through bot_class_ai[BOT_CLASS_DROW].
 *
 * Drow power comes from the DPOWER_* bitmask (pcdata->powers[1]).
 * Powers are purchased with class points (pcdata->stats[DROW_POWER])
 * earned per kill.  Self-grant costs the base price; granting to others
 * costs 5x more.
 *
 * Key passive benefits once unlocked:
 *   - DPOWER_TOUGHSKIN:  incoming damage /= 3
 *   - DPOWER_SPEED:      +3/+5 extra attacks, -50 to attacker parry/dodge
 *   - DPOWER_DROWPOISON: auto-poison on every hit
 *   - NEW_DROWHATE:      +650 max_dam (toggle)
 *   - NEW_DFORM:         +650 max_dam, +400 hitroll/damroll (spiderform toggle)
 *   - CLASS_DROW base:   +500 max_dam flat
 *
 * See doc/bot_ai/drow.md for full power reference.
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
#include "bot.h"
#include "drow.h"

/* -----------------------------------------------------------------------
 * Grant priority table
 *
 * Powers are purchased in this order as class points accumulate.
 * Self-grant cost equals the base price (no multiplier when ch == victim).
 *
 * Priority rationale:
 *   1. levitation    (1000)  — trip/sweep immunity; cheapest power
 *   2. drowpoison    (2500)  — passive poison on every hit
 *   3. drowsight     (5000)  — truesight (PLR_HOLYLIGHT)
 *   4. toughskin     (7500)  — incoming damage /= 3; biggest survivability gain
 *   5. speed         (7500)  — +3/+5 extra attacks, major dodge advantage
 *   6. garotte       (5000)  — burst damage / NPC instakill (needs whip)
 *   7. earthshatter  (7500)  — AoE room damage
 *   8. drowfire      (2500)  — ranged fire attack
 *   9. web           (5000)  — immobilize target
 *  10. confuse       (2500)  — force flee (combat opener)
 *  11. fightdance   (10000)  — auto-attack each round with whip (50% chance)
 *  12. darkness      (7500)  — escape utility; also enables dark garotte
 *  13. dgarotte      (2500)  — enhanced garotte in darkness
 *  14. drowhate     (20000)  — toggle +650 max_dam
 *  15. darktendrils (25000)  — toggle extra multi_hit + dodge proc
 *  16. spiderarms   (25000)  — passive disarm resistance
 *  17. spiderform   (25000)  — spider transform (+400 hit/dam, +650 max_dam)
 *  18. drowshield    (5000)  — PvP aura hide (low combat value vs NPCs)
 *  19. shadowwalk   (10000)  — teleport (mobility, low grinder value)
 *  (glamour skipped — cosmetic only)
 * ----------------------------------------------------------------------- */

static const struct {
    int         flag;   /* DPOWER_* bitmask bit                   */
    int         cost;   /* class points for self-grant            */
    const char *name;   /* argument to "grant self <name>"        */
} drow_grant_prio[] = {
    { DPOWER_LEVITATION,    1000,  "levitation"   },
    { DPOWER_DROWPOISON,    2500,  "drowpoison"   },
    { DPOWER_DROWSIGHT,     5000,  "drowsight"    },
    { DPOWER_TOUGHSKIN,     7500,  "toughskin"    },
    { DPOWER_SPEED,         7500,  "speed"        },
    { DPOWER_GAROTTE,       5000,  "garotte"      },
    { DPOWER_EARTHSHATTER,  7500,  "earthshatter" },
    { DPOWER_DROWFIRE,      2500,  "drowfire"     },
    { DPOWER_WEB,           5000,  "web"          },
    { DPOWER_CONFUSE,       2500,  "confuse"      },
    { DPOWER_FIGHTDANCE,   10000,  "fightdance"   },
    { DPOWER_DARKNESS,      7500,  "darkness"     },
    { DPOWER_DGAROTTE,      2500,  "dgarotte"     },
    { DPOWER_DROWHATE,     20000,  "drowhate"     },
    { DPOWER_DARKTENDRILS, 25000,  "darktendrils" },
    { DPOWER_ARMS,         25000,  "spiderarms"   },
    { DPOWER_SPIDERFORM,   25000,  "spiderform"   },
    { DPOWER_DROWSHIELD,    5000,  "drowshield"   },
    { DPOWER_SHADOWWALK,   10000,  "shadowwalk"   },
    { 0, 0, NULL }
};

/* -----------------------------------------------------------------------
 * bot_drow_next_grant
 *
 * Returns the index into drow_grant_prio[] for the next power to buy,
 * or -1 if all powers are already owned or none are affordable.
 * ----------------------------------------------------------------------- */

static int bot_drow_next_grant( CHAR_DATA *ch )
{
    int i;
    for ( i = 0; drow_grant_prio[i].flag != 0; i++ )
    {
        /* Already owned */
        if ( IS_SET(ch->pcdata->powers[1], drow_grant_prio[i].flag) )
            continue;
        /* Can afford it */
        if ( ch->pcdata->stats[DROW_POWER] >= drow_grant_prio[i].cost )
            return i;
        /* Can't afford this one; keep looking in case a cheaper one is next */
    }
    return -1;
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE when there is a power grant the bot can afford, or when
 * the bot qualifies for a profession but hasn't taken one yet.
 * ----------------------------------------------------------------------- */

static bool bot_drow_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_DROW) ) return FALSE;

    /* Next power affordable */
    if ( bot_drow_next_grant(ch) >= 0 ) return TRUE;

    /* Profession not yet assigned and generation qualifies */
    if ( ch->generation >= 3
      && !IS_SET(ch->special, SPC_DROW_WAR)
      && !IS_SET(ch->special, SPC_DROW_MAG)
      && !IS_SET(ch->special, SPC_DROW_CLE) )
        return TRUE;

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Issues one grant or profession command per call.
 * Called by bot_do_train() before the generic hp/mana/move spending.
 * Returns TRUE if a command was issued.
 * ----------------------------------------------------------------------- */

static bool bot_drow_do_train( CHAR_DATA *ch )
{
    int   idx;
    char  cmd[64];

    if ( !IS_CLASS(ch, CLASS_DROW) ) return FALSE;

    /* Warrior profession: self-grant once generation allows it */
    if ( ch->generation >= 3
      && !IS_SET(ch->special, SPC_DROW_WAR)
      && !IS_SET(ch->special, SPC_DROW_MAG)
      && !IS_SET(ch->special, SPC_DROW_CLE) )
    {
        bot_cmd( ch, "grant self warrior" );
        return TRUE;
    }

    /* Next power in priority order */
    idx = bot_drow_next_grant(ch);
    if ( idx >= 0 )
    {
        sprintf( cmd, "grant self %s", drow_grant_prio[idx].name );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * bot_drow_has_whip
 *
 * Returns TRUE if the bot has a whip (weapon type 4) equipped in
 * WEAR_WIELD or WEAR_HOLD — required for garotte and fightdance.
 * ----------------------------------------------------------------------- */

static bool bot_drow_has_whip( CHAR_DATA *ch )
{
    OBJ_DATA *obj;

    obj = get_eq_char( ch, WEAR_WIELD );
    if ( obj != NULL && obj->item_type == ITEM_WEAPON && obj->value[3] == 4 )
        return TRUE;

    obj = get_eq_char( ch, WEAR_HOLD );
    if ( obj != NULL && obj->item_type == ITEM_WEAPON && obj->value[3] == 4 )
        return TRUE;

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Ensures all drow toggle buffs are active.
 * Issues at most one command per call; returns TRUE when it does.
 * Called each grinding tick between fights.
 *
 * Buff priority (highest first):
 *   1. drowsight    — truesight (DPOWER_DROWSIGHT)
 *   2. drowhate     — +650 max_dam (DPOWER_DROWHATE)
 *   3. darktendrils — extra attacks + dodge (DPOWER_DARKTENDRILS)
 *   4. fightdance   — auto-whip attack; only if whip is equipped
 *   5. spiderform   — +400 hitroll/damroll, +650 max_dam; blocked by polymorph
 * ----------------------------------------------------------------------- */

static bool bot_drow_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_DROW) ) return FALSE;

    /* Drowsight: truesight — see invisible and hidden */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_DROWSIGHT)
      && !IS_SET(ch->act, PLR_HOLYLIGHT) )
    { bot_cmd( ch, "drowsight" ); return TRUE; }

    /* Drowhate: +650 max_dam 
     * Must be disabled during navigation to prevent random attacks. */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_DROWHATE) )
    {
        bool navigating = (ch->pcdata->botdata != NULL && ch->pcdata->botdata->nav_n > 0);
        bool active = IS_SET(ch->newbits, NEW_DROWHATE);

        if ( navigating && active )
        {
            bot_cmd( ch, "drowhate" );
            /* Do not return TRUE; we don't want to delay the PvP hunt */
        }
        else if ( !navigating && !active )
        {
            bot_cmd( ch, "drowhate" );
            return TRUE;
        }
    }

    /* Darktendrils: extra multi_hit attack + dodge proc each combat round */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_DARKTENDRILS)
      && !IS_SET(ch->newbits, NEW_DARKTENDRILS) )
    { bot_cmd( ch, "darktendrils" ); return TRUE; }

    /* Fightdance: auto-attack each round; only useful with whip equipped */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_FIGHTDANCE)
      && !IS_SET(ch->newbits, NEW_FIGHTDANCE)
      && bot_drow_has_whip(ch) )
    { bot_cmd( ch, "fightdance" ); return TRUE; }

    /* Spiderform: +400 hitroll/damroll, +650 max_dam, extra hands
     * Blocked if already polymorphed by another effect. */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_SPIDERFORM)
      && !IS_SET(ch->newbits, NEW_DFORM)
      && !IS_AFFECTED(ch, AFF_POLYMORPH) )
    { bot_cmd( ch, "spiderform" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one active combat ability per tick.
 * Called each pulse while ch->position == POS_FIGHTING.
 *
 * Priority order:
 *   1 (30%): garotte    — burst damage; auto-kills NPCs on lucky roll
 *   2 (55%): drowfire   — ranged fire spell (100 mana)
 *   3 (70%): earthshatter — room AoE (150 mana)
 *   4 (83%): web        — immobilize
 *   5 (90%): confuse    — force flee (75 move)
 *
 * Most drow damage output comes from passives (TOUGHSKIN, SPEED, DROWPOISON,
 * DARKTENDRILS, FIGHTDANCE, base +500 max_dam) so active abilities supplement.
 * ----------------------------------------------------------------------- */

static void bot_drow_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA  *target = ch->fighting;
    int         roll;
    char        cmd[MAX_INPUT_LENGTH];
    const char *tname;

    if ( target == NULL ) return;

    /* drowhate requires POS_STANDING (interp.c) so it cannot be toggled
     * during combat.  The buff_check vtable handles it between fights. */

    roll  = number_range( 1, 100 );
    tname = target->name;

    /* Priority 1 (30%): garotte — instakill NPCs on lucky percent rolls;
     * requires a whip in WEAR_WIELD or WEAR_HOLD. */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_GAROTTE)
      && bot_drow_has_whip(ch)
      && roll <= 30 )
    {
        sprintf( cmd, "garotte %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 2 (55%): drowfire — fire spell, 100 mana */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_DROWFIRE)
      && ch->mana >= 100
      && roll <= 55 )
    {
        sprintf( cmd, "drowfire %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 3 (70%): earthshatter — room AoE, 150 mana */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_EARTHSHATTER)
      && ch->mana >= 150
      && roll <= 70 )
    {
        //bot_cmd( ch, "earthshatter" );
        return;
    }

    /* Priority 4 (83%): web — immobilize target */
    if ( IS_SET(ch->pcdata->powers[1], DPOWER_WEB)
      && roll <= 83 )
    {
        sprintf( cmd, "web %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 5 (90%): confuse — force target to flee (75 move cost) */
    //if ( IS_SET(ch->pcdata->powers[1], DPOWER_CONFUSE)
    //  && ch->move >= 75
    //  && roll <= 90 )
    //{
    //    bot_cmd( ch, "confuse" );
    //    return;
    //}

    /* Fallback: normal multi_hit loop handles the rest */
}

/* -----------------------------------------------------------------------
 * Vtable: between_fights
 *
 * Setup performed between kills during the grinding loop.
 * Returns TRUE if a command was issued (stops hunt processing that tick).
 *
 * Steps:
 *   1. If whip in inventory but not wielded -> wield it.
 *   2. If no whip anywhere and practice >= 60 -> drowcreate whip.
 * ----------------------------------------------------------------------- */

static bool bot_drow_between_fights( CHAR_DATA *ch )
{
    OBJ_DATA *obj;

    if ( !IS_CLASS(ch, CLASS_DROW) ) return FALSE;

    /* Step 1: look for an unwielded whip in inventory */
    if ( !bot_drow_has_whip(ch) )
    {
        for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
        {
            if ( obj->item_type == ITEM_WEAPON && obj->value[3] == 4 )
            {
                bot_cmd( ch, "wield whip" );
                return TRUE;
            }
        }

        /* Step 2: no whip at all — create one if we have enough primal */
        if ( ch->practice >= 60 )
        {
            bot_cmd( ch, "drowcreate whip" );
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_drow_ai = {
    bot_drow_should_train,   /* should_train   */
    bot_drow_do_train,       /* do_train       */
    bot_drow_buff_check,     /* buff_check     */
    bot_drow_combat_action,  /* combat_action  */
    bot_drow_between_fights  /* between_fights */
};
