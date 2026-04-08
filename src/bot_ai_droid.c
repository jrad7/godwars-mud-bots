/*
 * bot_ai_droid.c - Spider Droid class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_DROID.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_DROID].
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
#include "spiderdroid.h"

/* -----------------------------------------------------------------------
 * Balanced Implant Progression Sequence
 *
 * Requirements:
 * - Body implants require at least 1 leg implant
 * - Face implant level 4 requires at least body implant level 4
 *
 * Costs: Level 1=25k, 2=50k, 3=100k, 4=200k, 5=400k, 6=800k.
 * ----------------------------------------------------------------------- */

static const struct {
    int         implant;
    int         target_level;
    const char *cmd;
} implant_prio[] = {
    { CYBORG_LIMBS, 1, "implant legs improve" },
    { CYBORG_BODY,  1, "implant body improve" },
    { CYBORG_FACE,  1, "implant face improve" },
    { CYBORG_LIMBS, 2, "implant legs improve" },
    { CYBORG_BODY,  2, "implant body improve" },
    { CYBORG_FACE,  2, "implant face improve" },
    { CYBORG_LIMBS, 3, "implant legs improve" },
    { CYBORG_BODY,  3, "implant body improve" },
    { CYBORG_FACE,  3, "implant face improve" },
    { CYBORG_LIMBS, 4, "implant legs improve" },
    { CYBORG_BODY,  4, "implant body improve" },
    { CYBORG_FACE,  4, "implant face improve" },
    { CYBORG_LIMBS, 5, "implant legs improve" },
    { CYBORG_BODY,  5, "implant body improve" },
    { CYBORG_FACE,  5, "implant face improve" },
    { CYBORG_BODY,  6, "implant body improve" },
    { -1, 0, NULL }
};

static int get_implant_cost(int current_level)
{
    if (current_level == 0) return 25000;
    if (current_level == 1) return 50000;
    if (current_level == 2) return 100000;
    if (current_level == 3) return 200000;
    if (current_level == 4) return 400000;
    if (current_level == 5) return 800000;
    return 99999999;
}

static int bot_droid_pick_implant( CHAR_DATA *ch )
{
    int i;
    for ( i = 0; implant_prio[i].implant >= 0; i++ )
    {
        int cur = ch->pcdata->powers[ implant_prio[i].implant ];
        if ( cur < implant_prio[i].target_level )
            return i;
    }
    return -1;
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 * ----------------------------------------------------------------------- */
static bool bot_droid_should_train( CHAR_DATA *ch )
{
    int pi;

    if ( !IS_CLASS(ch, CLASS_DROID) ) return FALSE;

    pi = bot_droid_pick_implant(ch);
    if ( pi >= 0 )
    {
        int cur = ch->pcdata->powers[ implant_prio[pi].implant ];
        int cost = get_implant_cost(cur);
        if ( ch->pcdata->stats[DROID_POWER] >= cost )
            return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 * ----------------------------------------------------------------------- */
static bool bot_droid_do_train( CHAR_DATA *ch )
{
    int pi;

    if ( !IS_CLASS(ch, CLASS_DROID) ) return FALSE;

    pi = bot_droid_pick_implant(ch);
    if ( pi >= 0 )
    {
        int cur = ch->pcdata->powers[ implant_prio[pi].implant ];
        int cost = get_implant_cost(cur);
        if ( ch->pcdata->stats[DROID_POWER] >= cost )
        {
            bot_cmd( ch, implant_prio[pi].cmd );
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 * ----------------------------------------------------------------------- */
static bool bot_droid_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_DROID) ) return FALSE;

    /* Infravision (unholy sight) requires Face 1+ */
    if ( ch->pcdata->powers[CYBORG_FACE] >= 1 )
    {
        /* Depending on face level, we get holistic or shadow sight */
        if ( ch->pcdata->powers[CYBORG_FACE] > 2 )
        {
            if ( !IS_SET(ch->act, PLR_HOLYLIGHT) )
            {
                bot_cmd( ch, "llothsight" );
                return TRUE;
            }
        }
        else if ( ch->pcdata->powers[CYBORG_FACE] == 2 )
        {
            if ( !IS_SET(ch->affected_by, AFF_SHADOWSIGHT) || !IS_SET(ch->pcdata->stats[UNI_AFF], VAM_NIGHTSIGHT) )
            {
                bot_cmd( ch, "llothsight" );
                return TRUE;
            }
        }
        else if ( ch->pcdata->powers[CYBORG_FACE] == 1 )
        {
            if ( !IS_SET(ch->pcdata->stats[UNI_AFF], VAM_NIGHTSIGHT) )
            {
                bot_cmd( ch, "llothsight" );
                return TRUE;
            }
        }
    }

    /* Cubeform (Avatar of Lloth) requires Body 5+ */
    if ( ch->pcdata->powers[CYBORG_BODY] >= 5 )
    {
        if ( !IS_SET(ch->newbits, NEW_CUBEFORM) && ch->mana >= 2000 && ch->move >= 2000 )
        {
            bot_cmd( ch, "cubeform" );
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Throttle stuntubes to avoid draining move. stuntubes requires Leg 5
 * and Body 5, hits 3x, adds POISON and FLAMING, costs 1000 move. We'll
 * throttle by only using it if we have at least 2500 move so we still
 * maintain some move points.
 * ----------------------------------------------------------------------- */
static void bot_droid_combat_action( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_DROID) ) return;

    if ( ch->pcdata->powers[CYBORG_BODY] >= 5 && ch->pcdata->powers[CYBORG_LIMBS] >= 5 )
    {
        /* Throttled usage: need plenty of move so we don't zero out completely, and 
         * we can't use it faster than move regenerates. */
        if ( ch->move >= 2500 )
        {
            /* Add some randomization to throttle it mechanically over rounds */
            if ( number_range(1, 3) == 1 )
            {
                bot_cmd( ch, "stuntubes" );
                return;
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */
const BOT_CLASS_AI bot_droid_ai = {
    bot_droid_should_train,
    bot_droid_do_train,
    bot_droid_buff_check,
    bot_droid_combat_action,
    NULL
};
