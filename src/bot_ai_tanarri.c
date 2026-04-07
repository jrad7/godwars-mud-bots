/*
 * bot_ai_tanarri.c - Tanar'ri class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_TANARRI.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_TANARRI].
 *
 * Tanarri is a Demon upgrade class with 6 ranks (Fodder -> Fighter ->
 * Elite -> Captain -> Warlord -> Balor) and 18 powers unlocked 3 per rank
 * via bloodsacrifice using TPOINTS (stats[8], earned from mob kills).
 *
 * Rank advancement uses "train <rankname>" and requires all powers for the
 * current rank to be unlocked plus an exp threshold:
 *   fodder 10M | fighter 20M | elite 40M | captain 80M | warlord 160M | balor 320M
 *
 * Key passives (active once learned, no toggle):
 *   EXOSKELETON: 80% physical damage reduction
 *   SPEED: +3 attacks/round, improved parry/dodge
 *   MIGHT: x1.5 damage, +500 max damage
 *   FIERY: auto fire proc each combat strike
 *   HEAD: extra fang attack (with FANGS), improved parry/dodge
 *   TENDRILS: 70% chance to block enemy flee
 *
 * Toggle buffs (managed by buff_check):
 *   truesight  - PLR_HOLYLIGHT (rank 1)
 *   claws      - VAM_CLAWS unarmed attacks (rank 1); dropping weapon is OK
 *                because the bot gear system re-equips it and claws persist
 *   fury       - +250 hit/dam (rank 4); BLOCKS FLEE when active, so
 *                combat_action drops it early when HP/mana is low
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
#include "tanarri.h"
#include "bot.h"

/* -----------------------------------------------------------------------
 * Rank promotion table
 *
 * Each entry: the rank the bot is currently at, the number of powers it
 * must have unlocked, the exp cost, and the "train" argument.
 * Checked in order; only the first matching row is acted on.
 * ----------------------------------------------------------------------- */
static const struct {
    int  from_rank;       /* ch->pcdata->rank must equal this     */
    int  powers_needed;   /* TANARRI_POWER_COUNTER must be >= this */
    long exp_needed;      /* ch->exp must be >= this               */
    const char *arg;      /* argument passed to "train"            */
} promote_table[] = {
    { 0,  0,  10000000L, "fodder"  },
    { 1,  3,  20000000L, "fighter" },
    { 2,  6,  40000000L, "elite"   },
    { 3,  9,  80000000L, "captain" },
    { 4, 12, 160000000L, "warlord" },
    { 5, 15, 320000000L, "balor"   },
    { -1, 0,  0L,        NULL      }
};

/* -----------------------------------------------------------------------
 * bot_tan_tpoints_cost
 *
 * Returns the TPOINTS cost for the next bloodsacrifice at the given rank.
 * Matches the formula in do_bloodsac(): cost = 2 * 2^(rank-1) * 10000
 * which simplifies to 10000 * (1 << rank).
 * ----------------------------------------------------------------------- */
static int bot_tan_tpoints_cost( int rank )
{
    return 10000 * ( 1 << rank );
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE if there is a Tanarri-specific training step available:
 *   1. bloodsacrifice: rank > 0, more powers to unlock, enough TPOINTS
 *   2. train <rank>: all current-rank powers unlocked, exp threshold met
 * ----------------------------------------------------------------------- */
static bool bot_tan_should_train( CHAR_DATA *ch )
{
    int rank, counter, i;

    if ( !IS_CLASS(ch, CLASS_TANARRI) ) return FALSE;

    rank    = ch->pcdata->rank;
    counter = ch->pcdata->powers[TANARRI_POWER_COUNTER];

    /* bloodsacrifice available? */
    if ( rank > 0
      && counter < rank * 3
      && ch->pcdata->stats[TPOINTS] >= bot_tan_tpoints_cost(rank) )
        return TRUE;

    /* rank promotion available? */
    for ( i = 0; promote_table[i].from_rank != -1; i++ )
    {
        if ( promote_table[i].from_rank != rank ) continue;
        if ( counter < promote_table[i].powers_needed ) continue;
        if ( ch->exp  < promote_table[i].exp_needed   ) continue;
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Executes one Tanarri-specific training step.
 * Returns TRUE if a command was issued.
 *
 * Priority:
 *   1. bloodsacrifice - spend TPOINTS to unlock the next power
 *   2. train <rank>   - advance to next rank once all powers are unlocked
 * ----------------------------------------------------------------------- */
static bool bot_tan_do_train( CHAR_DATA *ch )
{
    int rank, counter, i;
    char cmd[64];

    if ( !IS_CLASS(ch, CLASS_TANARRI) ) return FALSE;

    rank    = ch->pcdata->rank;
    counter = ch->pcdata->powers[TANARRI_POWER_COUNTER];

    /* Priority 1: unlock the next power via bloodsacrifice */
    if ( rank > 0
      && counter < rank * 3
      && ch->pcdata->stats[TPOINTS] >= bot_tan_tpoints_cost(rank) )
    {
        bot_cmd( ch, "bloodsacrifice" );
        return TRUE;
    }

    /* Priority 2: advance rank once all current powers are unlocked */
    for ( i = 0; promote_table[i].from_rank != -1; i++ )
    {
        if ( promote_table[i].from_rank != rank ) continue;
        if ( counter < promote_table[i].powers_needed ) continue;
        if ( ch->exp  < promote_table[i].exp_needed   ) continue;

        sprintf( cmd, "train %s", promote_table[i].arg );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Ensures all Tanarri toggle buffs are active.
 * Issues at most one command per call; returns TRUE when it does.
 * Called each grinding tick between fights.
 *
 * Buff priority (highest first):
 *   1. truesight - PLR_HOLYLIGHT: see invisible/hidden (TANARRI_TRUESIGHT)
 *   2. claws     - VAM_CLAWS unarmed attacks (TANARRI_CLAWS)
 *                  Enabling claws drops wielded weapons, but the bot gear
 *                  system re-equips them on the next gear tick while claws
 *                  remain active — net result is both claws and weapon hit.
 *   3. fury      - +250 hitroll/damroll (TANARRI_FURY)
 *                  NOTE: fury blocks do_flee. combat_action drops fury when
 *                  HP/mana is low so the engine can flee successfully.
 * ----------------------------------------------------------------------- */
static bool bot_tan_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_TANARRI) ) return FALSE;

    /* truesight: see invisible and hidden */
    if ( IS_SET(ch->pcdata->powers[TANARRI_POWER], TANARRI_TRUESIGHT)
      && !IS_SET(ch->act, PLR_HOLYLIGHT) )
    { bot_cmd( ch, "truesight" ); return TRUE; }

    /* claws: unarmed claw attacks (additive with weapon once re-equipped) */
    if ( IS_SET(ch->pcdata->powers[TANARRI_POWER], TANARRI_CLAWS)
      && !IS_VAMPAFF(ch, VAM_CLAWS) )
    { bot_cmd( ch, "claws" ); return TRUE; }

    /* fury: +250 hitroll/damroll combat bonus */
    if ( IS_SET(ch->pcdata->powers[TANARRI_POWER], TANARRI_FURY)
      && ch->pcdata->powers[TANARRI_FURY_ON] != 1 )
    { bot_cmd( ch, "fury" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one active combat ability per tick using probability thresholds.
 * Called each pulse while ch->position == POS_FIGHTING.
 *
 * Most of Tanarri's combat output is passive (SPEED/MIGHT/FIERY/HEAD),
 * so active abilities supplement the base multi_hit loop.
 *
 * FURY/FLEE INTERACTION:
 *   fury blocks do_flee() with "Only cowards retreat from combat."
 *   When the bot hits the flee threshold (HP < 40% or mana < 30%),
 *   bot_needs_rest() will call do_flee() — which fails while fury is on.
 *   To prevent being trapped, we drop fury first so the next tick's
 *   bot_needs_rest check can successfully issue do_flee().
 *
 * Priority order:
 *   0. Drop fury (if low HP/mana and fury is blocking flee)
 *   1. booming   - single-target stun (25%) + rank-scaled damage (rank 5+)
 *   2. lavablast - triple magma x1.5 + AFF_FLAMING (rank 6+)
 *   3. infernal  - AoE fire vs all in room (rank 5+)
 *   4. earthquake - AoE physical vs non-flying (rank 1+)
 * ----------------------------------------------------------------------- */
static void bot_tan_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA  *target = ch->fighting;
    int         roll;
    char        cmd[MAX_INPUT_LENGTH];
    const char *tname;

    if ( target == NULL ) return;

    /*
     * Priority 0: fury blocks do_flee. Drop it when the bot is low so
     * the engine can flee on the next bot_needs_rest check.
     * Thresholds match bot_needs_rest(): HP < 40%, mana < 30%.
     */
    if ( ch->pcdata->powers[TANARRI_FURY_ON] == 1
      && ( ch->hit  < ch->max_hit  * 4 / 10
        || ch->mana < ch->max_mana * 3 / 10 ) )
    {
        bot_cmd( ch, "fury" );   /* toggle off */
        return;
    }

    roll  = number_range( 1, 100 );
    tname = target->name;

    /* Priority 1 (35%): booming - physical damage + 25% stun chance */
    if ( IS_SET(ch->pcdata->powers[TANARRI_POWER], TANARRI_BOOMING)
      && roll <= 35 )
    {
        sprintf( cmd, "booming %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 2 (55%): lavablast - triple magma + AFF_FLAMING
     * Costs 1000 mana and 1000 move; skip if not affordable. */
    if ( IS_SET(ch->pcdata->powers[TANARRI_POWER], TANARRI_LAVA)
      && roll <= 55
      && ch->mana >= 1000
      && ch->move >= 1000 )
    {
        bot_cmd( ch, "lavablast" );
        return;
    }

    /* Priority 3 (75%): infernal - AoE fireball vs all in room
     * Costs 2000 mana; skip if not affordable. */
    if ( IS_SET(ch->pcdata->powers[TANARRI_POWER], TANARRI_FLAMES)
      && roll <= 75
      && ch->mana >= 2000 )
    {
        //bot_cmd( ch, "infernal" );
        return;
    }

    /* Priority 4 (90%): earthquake - AoE physical vs non-flying targets
     * Costs 1000 mana; skip if not affordable. */
    if ( IS_SET(ch->pcdata->powers[TANARRI_POWER], TANARRI_EARTHQUAKE)
      && roll <= 90
      && ch->mana >= 1000 )
    {
        //bot_cmd( ch, "earthquake" );
        return;
    }

    /* Fallback: normal multi_hit loop handles the rest */
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_tanarri_ai = {
    bot_tan_should_train,   /* should_train   */
    bot_tan_do_train,       /* do_train       */
    bot_tan_buff_check,     /* buff_check     */
    bot_tan_combat_action,  /* combat_action  */
    NULL                    /* between_fights */
};
