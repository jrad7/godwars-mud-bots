/*
 * bot_ai_ninja.c - Ninja class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_NINJA.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_NINJA].
 *
 * Currently implements: should_train, do_train (belt progression).
 * TODO: buff_check, combat_action, between_fights.
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
 * Vtable: should_train
 *
 * Returns TRUE if the ninja has enough exp to buy the next belt rank.
 * Cheapest belt is 5M exp, so that's the floor.
 * ----------------------------------------------------------------------- */

static bool bot_ninja_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_NINJA) ) return FALSE;
    return ( ch->pcdata->rank < BELT_TEN && ch->exp >= 5000000 );
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Buys the next belt rank if the bot can afford it.
 * Returns TRUE if a command was issued.
 * ----------------------------------------------------------------------- */

static bool bot_ninja_do_train( CHAR_DATA *ch )
{
    static const struct {
        int         from_rank;
        int         cost;
        const char *cmd;
    } belts[] = {
        { 0,          5000000,  "train belt1"  },
        { BELT_ONE,   10000000, "train belt2"  },
        { BELT_TWO,   15000000, "train belt3"  },
        { BELT_THREE, 20000000, "train belt4"  },
        { BELT_FOUR,  25000000, "train belt5"  },
        { BELT_FIVE,  30000000, "train belt6"  },
        { BELT_SIX,   35000000, "train belt7"  },
        { BELT_SEVEN, 40000000, "train belt8"  },
        { BELT_EIGHT, 45000000, "train belt9"  },
        { BELT_NINE,  50000000, "train belt10" },
        { -1, 0, NULL }
    };
    int i;

    if ( !IS_CLASS(ch, CLASS_NINJA) ) return FALSE;

    for ( i = 0; belts[i].from_rank >= 0; i++ )
    {
        if ( ch->pcdata->rank == belts[i].from_rank
          && ch->exp >= belts[i].cost )
        {
            bot_cmd( ch, belts[i].cmd );
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_ninja_ai = {
    bot_ninja_should_train,  /* should_train   */
    bot_ninja_do_train,      /* do_train       */
    NULL,                    /* buff_check     - TODO */
    NULL,                    /* combat_action  - TODO */
    NULL                     /* between_fights - TODO */
};
