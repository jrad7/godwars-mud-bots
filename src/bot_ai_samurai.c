/*
 * bot_ai_samurai.c - Samurai class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_SAMURAI.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_SAMURAI].
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
#include "samurai.h"

/* -----------------------------------------------------------------------
 * Vtable: should_train
 * ----------------------------------------------------------------------- */
static bool bot_samurai_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_SAMURAI) ) return FALSE;

    if ( ch->max_hit >= 20000 )
    {
        if ( !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_SLIDE)
          || !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_SIDESTEP)
          || !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_BLOCK)
          || !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_COUNTERMOVE) )
        {
            if ( ch->exp >= 150000000 ) return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 * ----------------------------------------------------------------------- */
static bool bot_samurai_do_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_SAMURAI) ) return FALSE;

    if ( ch->max_hit >= 20000 && ch->exp >= 150000000 )
    {
        if ( !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_SLIDE) )
        {
            bot_cmd(ch, "martial slide");
            return TRUE;
        }
        if ( !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_SIDESTEP) )
        {
            bot_cmd(ch, "martial sidestep");
            return TRUE;
        }
        if ( !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_BLOCK) )
        {
            bot_cmd(ch, "martial block");
            return TRUE;
        }
        if ( !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_COUNTERMOVE) )
        {
            bot_cmd(ch, "martial countermove");
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 * ----------------------------------------------------------------------- */
static bool bot_samurai_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_SAMURAI) ) return FALSE;

    if ( !IS_SET(ch->newbits, NEW_BLADESPIN) )
    {
        if ( ch->wpn[0] >= 1000 && ch->wpn[1] >= 1000 && ch->wpn[3] >= 1000 )
        {
            bot_cmd(ch, "bladespin");
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 * ----------------------------------------------------------------------- */
static void bot_samurai_combat_action( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_SAMURAI) ) return;

    int focus = ch->pcdata->powers[SAMURAI_FOCUS];

    if ( focus > 35 )
    {
        bot_cmd(ch, "focus");
        return;
    }

    int martials = ch->pcdata->powers[SAMURAI_MARTIAL];

    /* Randomize the attack we use if we have it, to mix things up. */
    int move = number_range(1, 4);
    
    switch(move) {
        case 1:
            if (IS_SET(martials, SAM_SLIDE)) { bot_cmd(ch, "martial slide"); return; }
            break;
        case 2:
            if (IS_SET(martials, SAM_SIDESTEP)) { bot_cmd(ch, "martial sidestep"); return; }
            break;
        case 3:
            if (IS_SET(martials, SAM_BLOCK)) { bot_cmd(ch, "martial block"); return; }
            break;
        case 4:
            if (IS_SET(martials, SAM_COUNTERMOVE)) { bot_cmd(ch, "martial countermove"); return; }
            break;
    }
    
    if (IS_SET(martials, SAM_SLIDE)) bot_cmd(ch, "martial slide");
    if (IS_SET(martials, SAM_SIDESTEP)) bot_cmd(ch, "martial sidestep");
    if (IS_SET(martials, SAM_BLOCK)) bot_cmd(ch, "martial block");
    if (IS_SET(martials, SAM_COUNTERMOVE)) bot_cmd(ch, "martial countermove");
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */
const BOT_CLASS_AI bot_samurai_ai = {
    bot_samurai_should_train,
    bot_samurai_do_train,
    bot_samurai_buff_check,
    bot_samurai_combat_action,
    NULL
};
