/*
 * bot_ai_demon.c - Demon class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_DEMON.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_DEMON].
 *
 * Functions here are intentionally file-scoped (static) except for the
 * exported vtable object at the bottom.  Do not call them directly from
 * other translation units; go through bot_class_ai[BOT_CLASS_DEMON].
 *
 * Demon combat power comes primarily from passives:
 *   - DISC_DAEM_ATTA: +50 max_dam/level and +level/2 extra attacks
 *   - DISC_DAEM_IMMU: (100 - power*4)% of incoming damage
 *   - DISC_DAEM_HELL > 3: automatic fire proc each combat round (requires level 4+)
 * Active abilities (leech, frostbreath, unnerve, blink) supplement these.
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

/* -----------------------------------------------------------------------
 * bot_dem_pick_research
 *
 * Returns the DISC_DAEM_* index of the next discipline the demon bot
 * should research, or -1 if everything is maxed.
 *
 * Priority rationale:
 *   1. Attack 1-5  – claws (1), fangs (2), tail-grow (3), horns (4),
 *                    wings (5).  Also +50 max_dam and +extra attacks/level.
 *   2. Immunae 5   – 20% passive damage reduction; survivability while
 *                    grinding through early progression.
 *   3. Nether 2+4  – deathsense (truesight) then leech (HP drain).
 *   4. Discord 1   – unnerve strips target's combat stance.
 *   5. Hellfire 3  – activates the automatic fire proc in fight.c.
 *   6. Geluge 2    – frostbreath combat attack (timer-gated by engine).
 *   7. Attack 7    – blink burst attack.
 *   8. Immunae 10  – max damage reduction.
 *   9. Geluge 6    – entomb (ice-wall room trap).
 *  10. Attack 10   – full +500 max_dam, +5 extra attacks.
 *  11. Hellfire 8  – hellfire room walls.
 *  12. Everything else to 10.
 * ----------------------------------------------------------------------- */

static int bot_dem_pick_research( CHAR_DATA *ch )
{
    static const struct { int disc; int target; } prio[] = {
        /* Core melee: body parts + damage/attack scaling */
        { DISC_DAEM_ATTA, 5  },
        /* Push ATTA immediately: +100 max_dam, +1x unarmed mult, unlocks blink */
        { DISC_DAEM_ATTA, 7  },
        /* Survivability: 20% damage reduction before the expensive push to 10 */
        { DISC_DAEM_IMMU, 5  },
        /* Max ATTA: full +500 max_dam, 5x unarmed multiplier, +5 extra attacks */
        { DISC_DAEM_ATTA, 10 },
        /* Nether 2: deathsense toggle (truesight) */
        { DISC_DAEM_NETH, 2  },
        /* Hellfire 4: fire proc every combat round (fight.c requires > 3) */
        { DISC_DAEM_HELL, 4  },
        /* Discord 1: unnerve */
        { DISC_DAEM_DISC, 1  },
        /* Nether 4: leech (complete to unlock HP drain) */
        { DISC_DAEM_NETH, 4  },
        /* Geluge 2: frostbreath */
        { DISC_DAEM_GELU, 2  },
        /* Immunae max: 40% damage reduction */
        { DISC_DAEM_IMMU, 10 },
        /* Geluge 6: entomb */
        { DISC_DAEM_GELU, 6  },
        /* Attack max already done above */
        /* Hellfire max: room fire walls */
        { DISC_DAEM_HELL, 8  },
        /* Remaining disciplines to max */
        { DISC_DAEM_CORR, 10 },
        { DISC_DAEM_NETH, 10 },
        { DISC_DAEM_DISC, 10 },
        { DISC_DAEM_GELU, 10 },
        { DISC_DAEM_TEMP, 10 },
        { DISC_DAEM_MORP, 10 },
        { -1, 0 }
    };

    int i;
    for ( i = 0; prio[i].disc != -1; i++ )
    {
        int disc   = prio[i].disc;
        int target = prio[i].target;

        if ( ch->power[disc] < 0  ) continue;   /* not available */
        if ( ch->power[disc] >= target ) continue;
        if ( ch->power[disc] >= 10     ) continue;

        return disc;
    }

    return -1;
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE if there is any demon-specific training step to take:
 *   - Warp slots available AND enough demon class points (>= 15000), or
 *   - Discipline research complete (disc_points == 999), or
 *   - No active research and a discipline still needs advancement.
 *
 * Demons have no age rank — warps and disciplines are the class-specific
 * training resources.
 * ----------------------------------------------------------------------- */

static bool bot_dem_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_DEMON) ) return FALSE;

    /* Warp slots still available and enough demon points to buy one */
    if ( ch->warpcount < 18
      && ch->pcdata->stats[DEMON_CURRENT] >= 15000
      && ch->pcdata->stats[DEMON_TOTAL]   >= 15000 )
        return TRUE;

    /* Research complete - ready to spend the point */
    if ( ch->pcdata->disc_points == 999 ) return TRUE;

    /* No active research - start the next one if anything remains */
    if ( ch->pcdata->disc_research == -1
      && bot_dem_pick_research(ch) != -1 )
        return TRUE;

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Executes one demon-specific training step.
 * Returns TRUE if a command was issued.
 * Called by bot_do_train() before the generic hp/mana/move spending.
 *
 * Order of priority:
 *   1. WARP_REGENERATE — buy this first for sustained HP recovery.
 *   2. Any remaining warp slot (random order via plain "obtain").
 *   3. Discipline research — spend disc_points or start next research.
 * ----------------------------------------------------------------------- */

static bool bot_dem_do_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_DEMON) ) return FALSE;

    /*
     * Priority 1: obtain WARP_REGENERATE before spending points elsewhere.
     * Uses the bot-only targeted path in do_obtain() to skip the random roll.
     */
    if ( !IS_SET(ch->warp, WARP_REGENERATE)
      && ch->warpcount < 18
      && ch->pcdata->stats[DEMON_CURRENT] >= 15000
      && ch->pcdata->stats[DEMON_TOTAL]   >= 15000 )
    {
        bot_cmd( ch, "obtain regenerate" );
        return TRUE;
    }

    /*
     * Priority 2: fill remaining warp slots in random order.
     * Plain "obtain" (no arg) uses the original random-roll path in do_obtain().
     */
    if ( ch->warpcount < 18
      && ch->pcdata->stats[DEMON_CURRENT] >= 15000
      && ch->pcdata->stats[DEMON_TOTAL]   >= 15000 )
    {
        bot_cmd( ch, "obtain" );
        return TRUE;
    }

    /* Priority 3: discipline research */

    /* Research complete - spend the point */
    if ( ch->pcdata->disc_points == 999 && ch->pcdata->disc_research > 0 )
    {
        char cmd[64];
        sprintf( cmd, "train %s", discipline[ch->pcdata->disc_research] );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    /* No active research - start the highest-priority unfinished discipline */
    if ( ch->pcdata->disc_research == -1 )
    {
        int disc = bot_dem_pick_research(ch);
        if ( disc > 0 )
        {
            char cmd[64];
            sprintf( cmd, "research %s", discipline[disc] );
            bot_cmd( ch, cmd );
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Ensures all demon toggle buffs are active.
 * Issues at most one command per call; returns TRUE when it does.
 * Called each grinding tick between fights.
 *
 * Buff priority (highest first):
 *   1. deathsense  – unholy truesight (Nether 2)
 *   2. fangs       – bite attack (Atta 2)
 *   3. horns       – gore attack (Atta 4 or DEM_HORNS inpart)
 *   4. wings       – wing sweep (Atta 5 or DEM_WINGS inpart)
 *   5. tail        – tail whip (DEM_TAIL inpart only)
 * Note: claws are NOT activated — longsword/shortsword have 18x more base damage.
 * ----------------------------------------------------------------------- */

static bool bot_dem_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_DEMON) ) return FALSE;

    /* Deathsense (Nether 2): unholy truesight — see invisible / detect hidden */
    if ( ch->power[DISC_DAEM_NETH] >= 2
      && !IS_SET(ch->act, PLR_HOLYLIGHT) )
    { bot_cmd( ch, "deathsense" ); return TRUE; }

    /* Claws intentionally not used: demonarmour longsword/shortsword have
     * far higher damage dice (50d75 vs 10d20) and activating claws
     * would drop the wielded weapons. */

    /* Fangs (Attack 2): bite attack in combat */
    if ( ch->power[DISC_DAEM_ATTA] >= 2
      && !IS_VAMPAFF(ch, VAM_FANGS) )
    { bot_cmd( ch, "fangs" ); return TRUE; }

    /* Horns (Attack 4 or DEM_HORNS inpart): gore attack */
    if ( ( ch->power[DISC_DAEM_ATTA] >= 4 || IS_DEMPOWER(ch, DEM_HORNS) )
      && !IS_DEMAFF(ch, DEM_HORNS) )
    { bot_cmd( ch, "horns" ); return TRUE; }

    /* Wings (Attack 5 or DEM_WINGS inpart): wing sweep attack */
    if ( ( ch->power[DISC_DAEM_ATTA] >= 5 || IS_DEMPOWER(ch, DEM_WINGS) )
      && !IS_DEMAFF(ch, DEM_WINGS) )
    { bot_cmd( ch, "wings" ); return TRUE; }

    /* Tail (DEM_TAIL inpart required): tail whip */
    if ( IS_DEMPOWER(ch, DEM_TAIL)
      && !IS_DEMAFF(ch, DEM_TAIL) )
    { bot_cmd( ch, "tail" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one active combat ability per tick using probability thresholds.
 * Called each pulse while ch->position == POS_FIGHTING.
 *
 * Most of a demon's combat output is passive (extra attacks from ATTA,
 * fire proc from HELL > 3, damage reduction from IMMU), so the active
 * abilities here supplement rather than drive the damage output.
 *
 * Priority order:
 *   1. leech      – drains target HP and heals self (Nether 4)
 *   2. frostbreath – frost burst (Geluge 2, engine enforces cooldown)
 *   3. unnerve    – strips target's combat stance (Discord 1)
 *   4. blink      – pop-out then 2x multi_hit on next pulse (Atta 7)
 * ----------------------------------------------------------------------- */

static void bot_dem_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA  *target = ch->fighting;
    int         roll;
    char        cmd[MAX_INPUT_LENGTH];
    const char *tname;

    if ( target == NULL ) return;

    roll  = number_range( 1, 100 );
    tname = target->name;

    /* Priority 1 (35%): leech - damages target and heals self by up to 300 HP */
    if ( ch->power[DISC_DAEM_NETH] >= 4 && roll <= 35 )
    {
        sprintf( cmd, "leech %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 2 (60%): frostbreath - frost breath damage
     * If on cooldown, the engine rejects it gracefully; bot wastes one tick. */
    if ( ch->power[DISC_DAEM_GELU] >= 2 && roll <= 60 )
    {
        sprintf( cmd, "frostbreath %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 3 (80%): unnerve - forces target to drop combat stance */
    if ( ch->power[DISC_DAEM_DISC] >= 1 && roll <= 80 )
    {
        sprintf( cmd, "unnerve %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 4 (92%): blink - pop out and burst 2x multi_hit next pulse
     * Only against NPCs (ch->fighting is typically an NPC during grinding).
     * 40-beat wait state applies but the burst fires on the very next pulse. */
    if ( ch->power[DISC_DAEM_ATTA] >= 7 && IS_NPC(target) && roll <= 92 )
    {
        sprintf( cmd, "blink %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Fallback: normal multi_hit loop handles the rest */
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_demon_ai = {
    bot_dem_should_train,   /* should_train   */
    bot_dem_do_train,       /* do_train       */
    bot_dem_buff_check,     /* buff_check     */
    bot_dem_combat_action,  /* combat_action  */
    NULL                    /* between_fights */
};
