/*
 * bot_ai_shapeshifter.c - Shapeshifter class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_SHAPESHIFTER.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_SHAPESHIFTER].
 *
 * Progression overview:
 *   - Gear creation via 'shapearmor' (costs 150 primal each piece).
 *   - 'formlearn shiftpowers' to 4 (requires primal)
 *   - 'formlearn tiger' to 4 for grinding
 *   - 'formlearn bull' to 4 for PvP
 *   - 'formlearn hydra' and 'formlearn faerie' to 4 
 *
 * Combat loop:
 *   - Grind: Shift into Tiger form, use normal attacks + 'shaperoar' to cause fleeing if desired.
 *   - PvP: Shift into Bull form, use 'charge' (stun) and 'stomp'.
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
#include "shapeshifter.h"

/* Returns TRUE when every slot in the shapeshifter gear table has a class-gear piece equipped */
static bool bot_shapeshifter_gear_complete( CHAR_DATA *ch )
{
    const BOT_GEAR_PIECE *entry;
    OBJ_DATA             *obj;

    /* Assumes bot_class_gear[BOT_CLASS_SHAPESHIFTER] is initialized properly */
    for ( entry = bot_class_gear[BOT_CLASS_SHAPESHIFTER]; entry->wear_slot != WEAR_NONE; entry++ )
    {
        obj = get_eq_char( ch, entry->wear_slot );
        if ( obj == NULL
          || obj->pIndexData->vnum < 33000
          || obj->pIndexData->vnum > 33299 )
            return FALSE;
    }
    return TRUE;
}

static const char *bot_shapeshifter_pick_train( CHAR_DATA *ch )
{
    int cost;
    
    if ( ch->pcdata == NULL ) return NULL;

    /* Must complete gear first */
    if ( !bot_shapeshifter_gear_complete(ch) ) return NULL;

    /* 1. Shiftpowers */
    if ( ch->pcdata->powers[SHAPE_POWERS] < 5 )
    {
        cost = 80 * ch->pcdata->powers[SHAPE_POWERS] + 80;
        if ( ch->practice >= cost ) return "formlearn shiftpowers";
        else return NULL; /* wait for primal */
    }

    /* 2. Tiger Form (for Grinding) */
    if ( ch->pcdata->powers[TIGER_LEVEL] < 5 )
    {
        cost = 80 * ch->pcdata->powers[TIGER_LEVEL] + 80;
        if ( ch->practice >= cost ) return "formlearn tiger";
        else return NULL;
    }

    /* 3. Bull Form (for PvP) */
    if ( ch->pcdata->powers[BULL_LEVEL] < 5 )
    {
        cost = 80 * ch->pcdata->powers[BULL_LEVEL] + 80;
        if ( ch->practice >= cost ) return "formlearn bull";
        else return NULL;
    }

    /* 4. Hydra Form */
    if ( ch->pcdata->powers[HYDRA_LEVEL] < 5 )
    {
        cost = 80 * ch->pcdata->powers[HYDRA_LEVEL] + 80;
        if ( ch->practice >= cost ) return "formlearn hydra";
        else return NULL;
    }

    /* 5. Faerie Form */
    if ( ch->pcdata->powers[FAERIE_LEVEL] < 5 )
    {
        cost = 80 * ch->pcdata->powers[FAERIE_LEVEL] + 80;
        if ( ch->practice >= cost ) return "formlearn faerie";
        else return NULL;
    }

    return NULL;
}

/* -----------------------------------------------------------------------
 * bot_ss_primal_needed
 *
 * Returns the primal cost of the next pending formlearn step, or 0 if gear
 * is not yet complete (150 is sufficient while gearing) or all forms are
 * maxed.  Used by bot_primal_target() to raise the accumulation goal above
 * 150 once the bot starts training forms.
 * ----------------------------------------------------------------------- */
int bot_ss_primal_needed( CHAR_DATA *ch )
{
    int lvl;

    if ( !IS_CLASS(ch, CLASS_SHAPESHIFTER) || ch->pcdata == NULL ) return 0;

    /* While gearing, 150 primal per piece is enough — don't over-accumulate */
    if ( !bot_shapeshifter_gear_complete(ch) ) return 0;

    /* Return cost of the next pending step in priority order */
    if ( ch->pcdata->powers[SHAPE_POWERS] < 5 )
    {
        lvl = ch->pcdata->powers[SHAPE_POWERS];
        return 80 * lvl + 80;
    }
    if ( ch->pcdata->powers[TIGER_LEVEL] < 5 )
    {
        lvl = ch->pcdata->powers[TIGER_LEVEL];
        return 80 * lvl + 80;
    }
    if ( ch->pcdata->powers[BULL_LEVEL] < 5 )
    {
        lvl = ch->pcdata->powers[BULL_LEVEL];
        return 80 * lvl + 80;
    }
    if ( ch->pcdata->powers[HYDRA_LEVEL] < 5 )
    {
        lvl = ch->pcdata->powers[HYDRA_LEVEL];
        return 80 * lvl + 80;
    }
    if ( ch->pcdata->powers[FAERIE_LEVEL] < 5 )
    {
        lvl = ch->pcdata->powers[FAERIE_LEVEL];
        return 80 * lvl + 80;
    }

    return 0;  /* all forms fully trained */
}

static bool bot_shapeshifter_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_SHAPESHIFTER) ) return FALSE;
    return bot_shapeshifter_pick_train( ch ) != NULL;
}

static bool bot_shapeshifter_do_train( CHAR_DATA *ch )
{
    const char *cmd;

    if ( !IS_CLASS(ch, CLASS_SHAPESHIFTER) ) return FALSE;

    cmd = bot_shapeshifter_pick_train( ch );
    if ( cmd == NULL ) return FALSE;

    bot_cmd( ch, cmd );
    return TRUE;
}

static bool bot_shapeshifter_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_SHAPESHIFTER) ) return FALSE;
    return FALSE; /* Currently no active buffs for Shapeshifter out of forms */
}

/* 
 * Out of combat, shape into the right form depending on bot state.
 */
static bool bot_shapeshifter_between_fights( CHAR_DATA *ch )
{
    BOT_DATA *bot;
    int desired_form = TIGER_FORM;

    if ( !IS_CLASS(ch, CLASS_SHAPESHIFTER) ) return FALSE;

    bot = ch->pcdata->botdata;
    if ( bot == NULL ) return FALSE;

    /* Determine desired form based on state: Bull for PvP, Tiger for Grind */
    if ( bot->state == BOT_PVP_HUNT || bot->state == BOT_PVP_FIGHT || bot->state == BOT_PVP_FLEE )
        desired_form = BULL_FORM;

    /* Wait out fatigue if we're stressed from shape shifting */
    if ( ch->pcdata->powers[SHAPE_COUNTER] > 35 )
        return FALSE;

    if ( ch->pcdata->powers[SHAPE_FORM] != desired_form )
    {
        if ( IS_SET(ch->affected_by, AFF_POLYMORPH) )
        {
            /* Check if we are physically the right form first (we could be polymorphed via hatform/etc) */
            if ( ch->pcdata->powers[SHAPE_FORM] != 0 ) 
            {
                bot_cmd( ch, "shift human" );
                return TRUE;
            }
        }
        else
        {
            if ( desired_form == TIGER_FORM )
                bot_cmd( ch, "shift tiger" );
            else if ( desired_form == BULL_FORM )
                bot_cmd( ch, "shift bull" );
            return TRUE;
        }
    }

    return FALSE; 
}

static void bot_shapeshifter_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA *target = ch->fighting;
    int        roll;

    if ( target == NULL || ch->pcdata == NULL ) return;

    roll = number_range( 1, 100 );

    if ( ch->pcdata->powers[SHAPE_FORM] == BULL_FORM )
    {
        /* Stomp removes limbs and deals massive damage but wait states the user. 25% chance if max level. */
        if ( ch->pcdata->powers[BULL_LEVEL] >= 5 && roll <= 25 )
        {
            bot_cmd( ch, "stomp" );
            return;
        }
        /* Charge does stuns but uses 2000 move. */
        if ( ch->pcdata->powers[BULL_LEVEL] >= 4 && ch->move >= 2000 && roll <= 50 )
        {
            bot_cmd( ch, "charge" );
            return;
        }
    }
    else if ( ch->pcdata->powers[SHAPE_FORM] == TIGER_FORM )
    {
        /* Phase costs fatigue, wait state, and avoids damage. */
        if ( ch->pcdata->powers[TIGER_LEVEL] >= 5 && ch->pcdata->powers[PHASE_COUNTER] <= 0 && roll <= 15 )
        {
            bot_cmd( ch, "phase" );
            return;
        }
        /* Shaperoar might make opponent flee (good for panic/PVP_FLEE or just occasionally) */
        if ( ch->pcdata->powers[TIGER_LEVEL] >= 3 && ch->hit < (ch->max_hit * 0.4) && roll <= 20 )
        {
            bot_cmd( ch, "shaperoar" );
            return;
        }
    }
    else if ( ch->pcdata->powers[SHAPE_FORM] == HYDRA_FORM )
    {
        /* Breath does good damage based on hydra level */
        if ( ch->pcdata->powers[HYDRA_LEVEL] >= 1 && roll <= 50 )
        {
            bot_cmd( ch, "breath" );
            return;
        }
    }
    else if ( ch->pcdata->powers[SHAPE_FORM] == FAERIE_FORM )
    {
        /* Faerieblink deals big backstab damage for 2500 mana */
        if ( ch->pcdata->powers[FAERIE_LEVEL] >= 5 && ch->mana >= 2500 && roll <= 30 )
        {
            bot_cmd( ch, "faerieblink" );
            return;
        }
        if ( ch->pcdata->powers[FAERIE_LEVEL] >= 4 && ch->mana >= 1000 && ch->move >= 500 && roll <= 50 )
        {
            bot_cmd( ch, "faeriecurse target" ); /* MUD uses get_char_room with argument */
            /* wait do_faeriecurse needs the target's name explicitly */
            /* We'll use target->name but safely formatting */
            char buf[128];
            snprintf(buf, sizeof(buf), "faeriecurse %s", target->name ? target->name : "none");
            bot_cmd( ch, buf );
            return;
        }
    }
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_shapeshifter_ai = {
    bot_shapeshifter_should_train,   /* should_train   */
    bot_shapeshifter_do_train,       /* do_train       */
    bot_shapeshifter_buff_check,     /* buff_check     */
    bot_shapeshifter_combat_action,  /* combat_action  */
    bot_shapeshifter_between_fights  /* between_fights */
};
