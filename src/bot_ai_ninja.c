/*
 * bot_ai_ninja.c - Ninja class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_NINJA.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_NINJA].
 *
 * Functions here are intentionally file-scoped (static) except for the
 * exported vtable object at the bottom.  Do not call them directly from
 * other translation units; go through bot_class_ai[BOT_CLASS_NINJA].
 *
 * Ninja combat power comes from:
 *   - Belt rank:    extra attacks per round (1–5 depending on belt)
 *   - Chikyu:       incoming damage /= 2.2 at level 1; +500 max_dam at 2;
 *                   +3 extra attacks at level 3
 *   - Ningenno:     tsume claws (level 1); passive shiroken 3–5 hits (level 5)
 *   - Rage/michi:   max_dam += rage*4; full michi (+100 rage) = +400 max_dam
 *
 * Active abilities: tsume (claws toggle, POS_FIGHTING only) and michi (rage).
 * hakunetsu (strangle) requires the target at full HP — not viable mid-fight.
 * circle requires a piercing weapon, which conflicts with tsume; skipped.
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
#include "ninja.h"

/* -----------------------------------------------------------------------
 * Belt progression table
 * ----------------------------------------------------------------------- */

static const struct {
    int         from_rank;
    long        cost;
    const char *cmd;
} belt_table[] = {
    { 0,          5000000L,  "train belt1"  },
    { BELT_ONE,   10000000L, "train belt2"  },
    { BELT_TWO,   15000000L, "train belt3"  },
    { BELT_THREE, 20000000L, "train belt4"  },
    { BELT_FOUR,  25000000L, "train belt5"  },
    { BELT_FIVE,  30000000L, "train belt6"  },
    { BELT_SIX,   35000000L, "train belt7"  },
    { BELT_SEVEN, 40000000L, "train belt8"  },
    { BELT_EIGHT, 45000000L, "train belt9"  },
    { BELT_NINE,  50000000L, "train belt10" },
    { -1, 0L, NULL }
};

/* -----------------------------------------------------------------------
 * bot_ninja_pick_principle
 *
 * Returns the index into principle_prio[] of the next principle step the
 * ninja bot should buy, or -1 if the full priority list is complete.
 *
 * Priority rationale:
 *   1. Chikyu 1   – incoming damage /= 2.2 (largest single survivability gain)
 *   2. Chikyu 2   – +500 max_dam
 *   3. Chikyu 3   – +3 extra attacks per round
 *   4. Ningenno 1 – tsume: unarmed claws toggle
 *   5. Ningenno 2 – hakunetsu: 4× backstab opener vs full-HP target
 *   6. Sora 5     – kanzuite: truesight toggle (buys Sora 1–5 in sequence)
 *   7. Ningenno 5 – passive shiroken: 3–5 auto-hits per combat round
 *   8. Ningenno 6 – circle (lower priority; conflicts with tsume claws)
 *   9. Chikyu 6   – max (harakiri unlocked; bot does not call harakiri)
 *  10. Sora 6     – completes the tree (bomuzite + remainder)
 * ----------------------------------------------------------------------- */

static const struct {
    int         npower;
    int         target;
    const char *name;
} principle_prio[] = {
    { NPOWER_CHIKYU,   1, "chikyu"   },
    { NPOWER_CHIKYU,   2, "chikyu"   },
    { NPOWER_CHIKYU,   3, "chikyu"   },
    { NPOWER_NINGENNO, 1, "ningenno" },
    { NPOWER_NINGENNO, 2, "ningenno" },
    { NPOWER_SORA,     5, "sora"     },
    { NPOWER_NINGENNO, 5, "ningenno" },
    { NPOWER_NINGENNO, 6, "ningenno" },
    { NPOWER_CHIKYU,   6, "chikyu"   },
    { NPOWER_SORA,     6, "sora"     },
    { -1, 0, NULL }
};

static int bot_ninja_pick_principle( CHAR_DATA *ch )
{
    int i;
    for ( i = 0; principle_prio[i].npower >= 0; i++ )
    {
        int cur    = ch->pcdata->powers[ principle_prio[i].npower ];
        int target = principle_prio[i].target;

        if ( cur < target && cur < 6 ) return i;
    }
    return -1;
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE when either:
 *   - A belt rank is affordable (exp-based), or
 *   - A principle step is affordable (primal-based).
 *
 * Both resources are independent so either readiness triggers training.
 * ----------------------------------------------------------------------- */

static bool bot_ninja_should_train( CHAR_DATA *ch )
{
    int i, pi;

    if ( !IS_CLASS(ch, CLASS_NINJA) ) return FALSE;

    /* Belt affordable? */
    for ( i = 0; belt_table[i].from_rank >= 0; i++ )
    {
        if ( ch->pcdata->rank == belt_table[i].from_rank
          && ch->exp >= belt_table[i].cost )
            return TRUE;
    }

    /* Principle step affordable? */
    pi = bot_ninja_pick_principle(ch);
    if ( pi >= 0 )
    {
        int cur  = ch->pcdata->powers[ principle_prio[pi].npower ];
        int cost = (cur + 1) * 10;
        if ( ch->practice >= cost ) return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Issues one training command per call.
 * Priority: principles first (cheap primal wins), belts second (exp).
 * Returns TRUE if a command was issued.
 * Called by bot_do_train() before the generic hp/mana/move spending.
 * ----------------------------------------------------------------------- */

static bool bot_ninja_do_train( CHAR_DATA *ch )
{
    int i, pi;

    if ( !IS_CLASS(ch, CLASS_NINJA) ) return FALSE;

    /* Principle buy: primal is the cheap resource — buy first */
    pi = bot_ninja_pick_principle(ch);
    if ( pi >= 0 )
    {
        int cur  = ch->pcdata->powers[ principle_prio[pi].npower ];
        int cost = (cur + 1) * 10;
        if ( ch->practice >= cost )
        {
            char cmd[64];
            sprintf( cmd, "principles %s improve", principle_prio[pi].name );
            bot_cmd( ch, cmd );
            return TRUE;
        }
    }

    /* Belt buy: exp-based rank advancement */
    for ( i = 0; belt_table[i].from_rank >= 0; i++ )
    {
        if ( ch->pcdata->rank == belt_table[i].from_rank
          && ch->exp >= belt_table[i].cost )
        {
            bot_cmd( ch, belt_table[i].cmd );
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Maintains toggle buffs that can be activated outside of combat.
 * Issues at most one command per call; returns TRUE when it does.
 * Called each grinding tick between fights.
 *
 * Buff priority:
 *   1. kanzuite – truesight (Sora 5); requires move ≥ 500.
 *                 POS_MEDITATING in interp.c — usable outside combat.
 *
 * Note: tsume (Ningenno 1) requires POS_FIGHTING and cannot be issued
 * here.  It is handled in combat_action instead.
 * ----------------------------------------------------------------------- */

static bool bot_ninja_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_NINJA) ) return FALSE;

    /* Michi: must be up at all times for regen; critical when resting.
     * Now allowed at POS_RESTING so it can fire between fights. */
    if ( ch->rage < 100 && ch->move >= 500 )
    { bot_cmd( ch, "michi" ); return TRUE; }

    /* Kanzuite (Sora 5): truesight — see invisible / detect hidden */
    if ( ch->pcdata->powers[NPOWER_SORA] >= 5
      && !IS_SET(ch->act, PLR_HOLYLIGHT)
      && ch->move >= 500 )
    { bot_cmd( ch, "kanzuite" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one active combat ability per tick.
 * Called each pulse while ch->position == POS_FIGHTING.
 *
 * Most damage is passive (Chikyu bonuses, belt attacks, shiroken auto-fire,
 * rage multiplier), so only two active abilities are needed:
 *
 * Priority order:
 *   1. tsume  – activate IronClaws if not already on (Ningenno 1).
 *               Must be handled here because tsume requires POS_FIGHTING.
 *               Highest priority: claws are needed for every fight.
 *   2. michi  – raise rage by 100; max_dam += rage*4 → +400 at cap.
 *               Costs 500 move; skipped when already at full rage or low move.
 * ----------------------------------------------------------------------- */

static void bot_ninja_combat_action( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_NINJA) ) return;

    /* Priority 1: tsume — IronClaws toggle (POS_FIGHTING required) */
    if ( ch->pcdata->powers[NPOWER_NINGENNO] >= 1
      && !IS_VAMPAFF(ch, VAM_CLAWS) )
    {
        bot_cmd( ch, "tsume" );
        return;
    }

    /* Priority 2: michi — rage burst (+100 rage, +400 max_dam at full) */
    if ( ch->rage < 100 && ch->move >= 500 )
    {
        bot_cmd( ch, "michi" );
        return;
    }

    /* Fallback: normal multi_hit loop handles the rest */
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_ninja_ai = {
    bot_ninja_should_train,   /* should_train   */
    bot_ninja_do_train,       /* do_train       */
    bot_ninja_buff_check,     /* buff_check     */
    bot_ninja_combat_action,  /* combat_action  */
    NULL                      /* between_fights */
};
