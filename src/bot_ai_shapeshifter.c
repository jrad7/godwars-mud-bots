/*
 * bot_ai_shapeshifter.c - Shapeshifter class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_SHAPESHIFTER.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_SHAPESHIFTER].
 *
 * Progression overview:
 *   - Gear creation via 'shapearmor' (costs 150 primal each piece).
 *   - 'formlearn shiftpowers' to 5
 *   - 'formlearn hydra' to 5 for grinding (best passive DPS form)
 *   - 'formlearn tiger' to 5 for dodge/phase utility
 *   - 'formlearn bull' to 5 for PvP stun/stomp
 *   - 'formlearn faerie' to 5 for evasion/magic PvP
 *
 * Combat loop:
 *   - Grind: Shift into Hydra form, use 'breath' + massive passive fang hits.
 *   - PvP:  Shift into Bull form, use 'charge' (stun) and 'stomp'.
 *           Switch to Faerie if low HP for evasion + faerieblink burst.
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

    /* 1. Shiftpowers — utility passives needed across all forms */
    if ( ch->pcdata->powers[SHAPE_POWERS] < 5 )
    {
        cost = 80 * ch->pcdata->powers[SHAPE_POWERS] + 80;
        if ( ch->practice >= cost ) return "formlearn shiftpowers";
        else return NULL; /* wait for primal */
    }

    /* 2. Hydra Form — best grinding form: +450 dam/hit, 1.6x mult, 5x fang hits */
    if ( ch->pcdata->powers[HYDRA_LEVEL] < 5 )
    {
        cost = 80 * ch->pcdata->powers[HYDRA_LEVEL] + 80;
        if ( ch->practice >= cost ) return "formlearn hydra";
        else return NULL;
    }

    /* 3. Tiger Form — dodge/phase utility, good secondary form */
    if ( ch->pcdata->powers[TIGER_LEVEL] < 5 )
    {
        cost = 80 * ch->pcdata->powers[TIGER_LEVEL] + 80;
        if ( ch->practice >= cost ) return "formlearn tiger";
        else return NULL;
    }

    /* 4. Bull Form — PvP stun/stomp */
    if ( ch->pcdata->powers[BULL_LEVEL] < 5 )
    {
        cost = 80 * ch->pcdata->powers[BULL_LEVEL] + 80;
        if ( ch->practice >= cost ) return "formlearn bull";
        else return NULL;
    }

    /* 5. Faerie Form — evasion/magic PvP */
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
    if ( ch->pcdata->powers[HYDRA_LEVEL] < 5 )
    {
        lvl = ch->pcdata->powers[HYDRA_LEVEL];
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
 * bot_shapeshifter_pick_form
 *
 * Selects the best form for the current bot state:
 *   - Grinding:  Hydra (best passive DPS: +450 dam/hit, 5x fang hits, 1.6x mult)
 *   - PvP hunt:  Bull  (charge stun + stomp for burst/disable)
 *   - PvP fight: Bull normally, but switch to Faerie if low HP for evasion
 *   - PvP flee:  Faerie (best evasion to survive disengagement)
 */
static int bot_shapeshifter_pick_form( CHAR_DATA *ch, BOT_DATA *bot )
{
    if ( bot->state == BOT_PVP_FIGHT )
    {
        /* Switch to Faerie for evasion if we're losing the fight */
        if ( ch->hit < (ch->max_hit * 4 / 10)
          && ch->pcdata->powers[FAERIE_LEVEL] >= 3 )
            return FAERIE_FORM;
        return BULL_FORM;
    }

    if ( bot->state == BOT_PVP_HUNT )
        return BULL_FORM;

    if ( bot->state == BOT_PVP_FLEE )
    {
        /* Faerie evasion helps survive while fleeing */
        if ( ch->pcdata->powers[FAERIE_LEVEL] >= 1 )
            return FAERIE_FORM;
        return BULL_FORM;
    }

    /* Grinding / exploring / idle: Hydra for maximum sustained DPS */
    if ( ch->pcdata->powers[HYDRA_LEVEL] >= 1 )
        return HYDRA_FORM;

    /* Fallback if Hydra isn't trained yet */
    if ( ch->pcdata->powers[TIGER_LEVEL] >= 1 )
        return TIGER_FORM;

    return BULL_FORM;
}

/*
 * Out of combat, shape into the right form depending on bot state.
 */
static bool bot_shapeshifter_between_fights( CHAR_DATA *ch )
{
    BOT_DATA *bot;
    int desired_form;

    if ( !IS_CLASS(ch, CLASS_SHAPESHIFTER) ) return FALSE;

    bot = ch->pcdata->botdata;
    if ( bot == NULL ) return FALSE;

    desired_form = bot_shapeshifter_pick_form( ch, bot );

    /* Wait out fatigue if we're stressed from shape shifting */
    if ( ch->pcdata->powers[SHAPE_COUNTER] > 35 )
        return FALSE;

    if ( ch->pcdata->powers[SHAPE_FORM] != desired_form )
    {
        if ( IS_SET(ch->affected_by, AFF_POLYMORPH) )
        {
            /* Must shift to human first before changing forms */
            if ( ch->pcdata->powers[SHAPE_FORM] != 0 )
            {
                bot_cmd( ch, "shift human" );
                return TRUE;
            }
        }
        else
        {
            if ( desired_form == HYDRA_FORM )
                bot_cmd( ch, "shift hydra" );
            else if ( desired_form == BULL_FORM )
                bot_cmd( ch, "shift bull" );
            else if ( desired_form == TIGER_FORM )
                bot_cmd( ch, "shift tiger" );
            else if ( desired_form == FAERIE_FORM )
                bot_cmd( ch, "shift faerie" );
            return TRUE;
        }
    }

    return FALSE;
}

static void bot_shapeshifter_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA *target = ch->fighting;
    BOT_DATA  *bot;
    int        roll;
    bool       is_pvp;
    char       buf[MAX_INPUT_LENGTH];

    if ( target == NULL || ch->pcdata == NULL ) return;

    bot    = ch->pcdata->botdata;
    roll   = number_range( 1, 100 );
    is_pvp = ( bot != NULL && ( bot->state == BOT_PVP_HUNT
            || bot->state == BOT_PVP_FIGHT || bot->state == BOT_PVP_FLEE ) );

    /* ------------------------------------------------------------------
     * Mid-combat form switching (PvP only):
     * If we're in Bull and getting low, switch to Faerie for evasion.
     * Cost: one combat tick for the shift, but survival is worth it.
     * Only attempt if shift fatigue allows it.
     * ------------------------------------------------------------------ */
    if ( is_pvp
      && ch->pcdata->powers[SHAPE_FORM] == BULL_FORM
      && ch->hit < (ch->max_hit * 3 / 10)
      && ch->pcdata->powers[FAERIE_LEVEL] >= 3
      && ch->pcdata->powers[SHAPE_COUNTER] <= 25 )
    {
        bot_cmd( ch, "shift faerie" );
        return;
    }

    if ( ch->pcdata->powers[SHAPE_FORM] == HYDRA_FORM )
    {
        /* Breath: 5x fire breath at max level, strong burst damage.
         * High usage rate (70%) — this is the primary grinding ability. */
        if ( ch->pcdata->powers[HYDRA_LEVEL] >= 1 && roll <= 70 )
        {
            bot_cmd( ch, "breath" );
            return;
        }
        /* Remaining 30%: let passive fang hits carry the damage */
    }
    else if ( ch->pcdata->powers[SHAPE_FORM] == BULL_FORM )
    {
        /* Stomp: 500 damage + limb removal, but heavy wait state (24).
         * Use aggressively in PvP (40%), conservatively in grind (20%). */
        if ( ch->pcdata->powers[BULL_LEVEL] >= 5 && roll <= (is_pvp ? 40 : 20) )
        {
            bot_cmd( ch, "stomp" );
            return;
        }
        /* Charge: stun + headbutt/hooves hits, costs 2000 move.
         * Prioritize in PvP for the stun lockdown. */
        if ( ch->pcdata->powers[BULL_LEVEL] >= 4 && ch->move >= 2000 && roll <= (is_pvp ? 70 : 40) )
        {
            bot_cmd( ch, "charge" );
            return;
        }
    }
    else if ( ch->pcdata->powers[SHAPE_FORM] == TIGER_FORM )
    {
        /* Phase: damage avoidance, useful when taking heavy hits */
        if ( ch->pcdata->powers[TIGER_LEVEL] >= 5
          && ch->pcdata->powers[PHASE_COUNTER] <= 0
          && ch->hit < (ch->max_hit * 6 / 10)
          && roll <= 40 )
        {
            bot_cmd( ch, "phase" );
            return;
        }
        /* Shaperoar: only use in PvP to force opponent flee when desperate */
        if ( is_pvp
          && ch->pcdata->powers[TIGER_LEVEL] >= 3
          && ch->hit < (ch->max_hit * 3 / 10)
          && roll <= 50 )
        {
            bot_cmd( ch, "shaperoar" );
            return;
        }
    }
    else if ( ch->pcdata->powers[SHAPE_FORM] == FAERIE_FORM )
    {
        /* Faerieblink: massive backstab burst for 2500 mana */
        if ( ch->pcdata->powers[FAERIE_LEVEL] >= 5 && ch->mana >= 2500 && roll <= 50 )
        {
            bot_cmd( ch, "faerieblink" );
            return;
        }
        /* Faeriecurse: web + curse debuff, costs 1000 mana + 500 move */
        if ( ch->pcdata->powers[FAERIE_LEVEL] >= 4
          && ch->mana >= 1000 && ch->move >= 500 && roll <= 60 )
        {
            snprintf( buf, sizeof(buf), "faeriecurse %s",
                      target->name ? target->name : "none" );
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
