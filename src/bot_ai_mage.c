/*
 * bot_ai_mage.c - Battlemage class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_MAGE.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_MAGE].
 *
 * Functions here are intentionally file-scoped (static) except for the
 * exported vtable object at the bottom.  Do not call them directly from
 * other translation units; go through bot_class_ai[BOT_CLASS_MAGE].
 *
 * --- Class Overview ---
 * Battlemage is a caster/melee hybrid gated behind hard prereqs: all five
 * spell colors (purple/red/blue/green/yellow) must reach 100 and max_mana
 * must reach 5000 before selfclass can fire.
 *
 * Color training works through the standard improve_spl() mechanism in
 * magic.c: every cast of a spell whose skill_table[sn].target matches a
 * color index has a random chance to increment spl[color].  The five target
 * values map as follows (merc.h):
 *
 *   PURPLE_MAGIC (0) = TAR_IGNORE          -> "faerie fog"
 *   RED_MAGIC    (1) = TAR_CHAR_OFFENSIVE  -> "curse" (self-cast)
 *   BLUE_MAGIC   (2) = TAR_CHAR_DEFENSIVE  -> "cure light" (self-cast)
 *   GREEN_MAGIC  (3) = TAR_CHAR_SELF       -> "detect hidden"
 *   YELLOW_MAGIC (4) = TAR_OBJ_INV         -> "identify" (ring)
 *
 * YELLOW requires the ring to be in inventory (TAR_OBJ_INV).  The bot removes
 * the ring once and keeps it off until YELLOW hits cap, then re-wears it once.
 * bot->spell_training=TRUE during this period, causing bot_ensure_geared() to
 * skip all gear management so the ring stays in inventory undisturbed.
 *
 * improve_spl fires after every cast attempt regardless of whether the
 * spell effect applied (e.g. already cursed, already detected), so spells
 * can be spammed repeatedly for color grinding.
 *
 * Post-class training unlocks Invoke powers 1-10 via "invoke learn", each
 * costing (current_PINVOKE+1)*20 primal.  Active buffs (mageshield etc.)
 * are toggled on by buff_check each time they are detected as missing.
 * Mageshield is intentionally re-applied on the tick AFTER a discharge.
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
 * Color training tables
 * ----------------------------------------------------------------------- */

typedef struct { int color; const char *spell; const char *arg; } COLOR_TRAIN;

/* One representative spell per color.  The arg is appended to the cast
 * command; empty string means no argument (TAR_IGNORE / TAR_CHAR_SELF). */
static const COLOR_TRAIN color_train[5] = {
    { PURPLE_MAGIC, "faerie fog",    ""     },  /* TAR_IGNORE          */
    { RED_MAGIC,    "curse",         "self" },  /* TAR_CHAR_OFFENSIVE  */
    { BLUE_MAGIC,   "cure light",    "self" },  /* TAR_CHAR_DEFENSIVE  */
    { GREEN_MAGIC,  "detect hidden", ""     },  /* TAR_CHAR_SELF       */
    { YELLOW_MAGIC, "identify",      "ring" },  /* TAR_OBJ_INV         */
};

/* Spells the bot must practice before between_fights can cast them.
 * Practicing costs 5000 exp the first time (see do_practice in act_info.c). */
static const char *pre_class_practice[] = {
    "faerie fog",
    "curse",
    "remove curse",
    "cure light",
    "armor",
    "bless",
    "detect hidden",
    "detect invis",
    "stone skin",
    "identify",
    NULL
};

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

/* Mirrors bot_should_practice() from bot_ai.c (which is static there). */
static bool mage_can_practice( CHAR_DATA *ch, const char *spell_name )
{
    int sn = skill_lookup( spell_name );
    if ( sn < 0 ) return FALSE;
    if ( ch->level < skill_table[sn].skill_level ) return FALSE;
    if ( ch->pcdata->learned[sn] >= 100 ) return FALSE;
    if ( ch->exp < 5000 ) return FALSE;
    return TRUE;
}

/* Returns the index (0-4) of the color with the lowest spl[] value that
 * has not yet reached cap.  Returns -1 if all colors are at or above cap. */
static int mage_lowest_color( CHAR_DATA *ch, int cap )
{
    int i, min_val = cap, min_idx = -1;
    for ( i = 0; i < 5; i++ )
    {
        if ( ch->spl[i] < min_val )
        {
            min_val = ch->spl[i];
            min_idx = i;
        }
    }
    return min_idx;
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 * ----------------------------------------------------------------------- */

static bool bot_mage_should_train( CHAR_DATA *ch )
{
    int i;

    /* Pre-class: always grinding colors / mana */
    if ( ch->level == 3 && ch->class == 0 )
        return TRUE;

    if ( !IS_CLASS(ch, CLASS_MAGE) ) return FALSE;

    /* Colors below cap */
    for ( i = 0; i < 5; i++ )
        if ( ch->spl[i] < 240 ) return TRUE;

    /* Invokes not fully unlocked */
    if ( ch->pcdata->powers[PINVOKE] < 10 ) return TRUE;

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Pre-class: practices required spells and trains mana; returns TRUE while
 * still grinding prereqs so bot_do_train does not fire selfclass early.
 * Returns FALSE when all five colors >= 100 and max_mana >= 5000, letting
 * the selfclass block in bot_do_train proceed normally.
 *
 * Post-class: buys next invoke level when primal is available.
 * ----------------------------------------------------------------------- */

static bool bot_mage_do_train( CHAR_DATA *ch )
{
    int i;

    /* ---- Pre-class gate ---- */
    if ( ch->level == 3 && ch->class == 0 )
    {
        /* Practice required spells first so between_fights can cast them */
        for ( i = 0; pre_class_practice[i] != NULL; i++ )
        {
            if ( mage_can_practice(ch, pre_class_practice[i]) )
            {
                char cmd[64];
                sprintf( cmd, "practice %s", pre_class_practice[i] );
                bot_cmd( ch, cmd );
                return TRUE;
            }
        }

        /* Train mana toward 5000 if we can afford it */
        if ( ch->max_mana < 5000 && ch->exp >= ch->max_mana + 1 )
        {
            bot_cmd( ch, "train mana all" );
            return TRUE;
        }

        /* Block selfclass until max_mana >= 5000 even if we can't afford to
         * train right now — keep grinding and wait for exp to accumulate */
        if ( ch->max_mana < 5000 ) return TRUE;

        /* Still waiting on colors (between_fights casts the training spells) */
        for ( i = 0; i < 5; i++ )
            if ( ch->spl[i] < 100 ) return TRUE;

        /* All prereqs met - return FALSE so selfclass fires */
        return FALSE;
    }

    /* ---- Post-class: invoke ladder ---- */
    if ( !IS_CLASS(ch, CLASS_MAGE) ) return FALSE;

    /* Mages are incredibly mana-hungry (1000 per attack). Prioritize 
     * training mana up to 15,000 so they can smoothly sustain combat 
     * before falling through to generic Superstance/HP training. */
    if ( ch->max_mana < 15000 && ch->exp >= ch->max_mana + 1 )
    {
        bot_cmd( ch, "train mana all" );
        return TRUE;
    }

    if ( ch->pcdata->powers[PINVOKE] < 10 )
    {
        int cost = (ch->pcdata->powers[PINVOKE] + 1) * 20;
        if ( ch->practice >= cost )
        {
            bot_cmd( ch, "invoke learn" );
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * bot_mage_primal_needed
 *
 * Returns the primal cost of the next pending invoke step, or 0 if all
 * invokes are maxed.  Used by bot_primal_target() to raise the accumulation
 * goal so the bot can afford invoke levels whose cost exceeds 60 primal.
 * Invoke N->N+1 costs (N+1)*20 primal; max is level 9->10 = 200 primal.
 * ----------------------------------------------------------------------- */
int bot_mage_primal_needed( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_MAGE) || ch->pcdata == NULL ) return 0;
    if ( ch->pcdata->powers[PINVOKE] >= 10 ) return 0;
    return (ch->pcdata->powers[PINVOKE] + 1) * 20;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Activates invoke buffs that are missing.  Issues at most one command per
 * call.  Mageshield is re-applied here on the tick after discharge removes
 * it, which is the intended aggressive cycling pattern.
 * ----------------------------------------------------------------------- */

static bool bot_mage_buff_check( CHAR_DATA *ch )
{
    int pinvoke, totalcost;

    if ( !IS_CLASS(ch, CLASS_MAGE) ) return FALSE;

    pinvoke = ch->pcdata->powers[PINVOKE];

    /* Once all five invokes are unlocked, use "invoke all" for efficiency */
    if ( pinvoke >= 9 )
    {
        totalcost = 0;
        if ( !IS_ITEMAFF(ch, ITEMA_MAGESHIELD) )  totalcost += 25;
        if ( !IS_ITEMAFF(ch, ITEMA_DEFLECTOR) )   totalcost += 5;
        if ( !IS_ITEMAFF(ch, ITEMA_STEELSHIELD) ) totalcost += 5;
        if ( !IS_ITEMAFF(ch, ITEMA_ILLUSIONS) )   totalcost += 5;
        if ( !IS_ITEMAFF(ch, ITEMA_BEAST) )       totalcost += 10;

        if ( totalcost > 0 && ch->practice >= totalcost )
        {
            bot_cmd( ch, "invoke all" );
            return TRUE;
        }
        return FALSE;
    }

    /* Individual activation in priority order */
    if ( pinvoke >= 2
      && !IS_ITEMAFF(ch, ITEMA_MAGESHIELD)
      && ch->practice >= 25 )
    { bot_cmd( ch, "invoke mageshield" ); return TRUE; }

    if ( pinvoke >= 5
      && !IS_ITEMAFF(ch, ITEMA_DEFLECTOR)
      && ch->practice >= 5 )
    { bot_cmd( ch, "invoke deflector" ); return TRUE; }

    if ( pinvoke >= 6
      && !IS_ITEMAFF(ch, ITEMA_STEELSHIELD)
      && ch->practice >= 5 )
    { bot_cmd( ch, "invoke steelshield" ); return TRUE; }

    if ( pinvoke >= 8
      && !IS_ITEMAFF(ch, ITEMA_ILLUSIONS)
      && ch->practice >= 5 )
    { bot_cmd( ch, "invoke illusions" ); return TRUE; }

    if ( pinvoke >= 9
      && !IS_ITEMAFF(ch, ITEMA_BEAST)
      && ch->practice >= 10 )
    { bot_cmd( ch, "invoke beast" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Priority:
 *   0. chant heal  - when HP < 40% and mana >= 1500
 *   1. chant damage - default when mana >= 1000
 * ----------------------------------------------------------------------- */

static void bot_mage_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA *target = ch->fighting;

    if ( target == NULL || ch->pcdata == NULL ) return;
    if ( !IS_CLASS(ch, CLASS_MAGE) ) return;

    /* Priority 0: emergency heal */
    if ( ch->hit < ch->max_hit * 2 / 5 && ch->mana >= 1500 )
    {
        bot_cmd( ch, "chant heal" );
        return;
    }

    /* Default: chant damage (5 elemental hits scaled by color levels) */
    if ( ch->mana >= 1000 )
    {
        bot_cmd( ch, "chant damage" );
        return;
    }
}

/* -----------------------------------------------------------------------
 * Vtable: between_fights
 *
 * Spams color training spells one per call, targeting the lowest color
 * that hasn't yet reached the current cap (100 pre-class, 240 post-class).
 *
 * YELLOW training (TAR_OBJ_INV) requires the ring to be in inventory, not
 * worn.  The ring is removed ONCE and kept off until YELLOW reaches cap,
 * regardless of whether other colors temporarily become lower in between.
 * The ring is only re-worn after YELLOW fully caps out.
 *
 *   Enter: spell_training=FALSE, YELLOW < cap → "remove ring", set flag
 *   While spell_training=TRUE:
 *     YELLOW < cap → "cast 'identify' ring"  (stays off the whole time)
 *     YELLOW >= cap → "wear ring", clear flag
 *
 * bot_ensure_geared skips the gear check while spell_training is set so
 * the ring stays in inventory undisturbed.
 *
 * Returns TRUE when a command was issued, FALSE otherwise.
 * ----------------------------------------------------------------------- */

static bool bot_mage_between_fights( CHAR_DATA *ch )
{
    BOT_DATA *bot = ch->pcdata ? ch->pcdata->botdata : NULL;
    int       cap, color_idx, sn;
    char      cmd[MAX_INPUT_LENGTH];
    OBJ_DATA *obj;

    if ( !bot ) return FALSE;

    /* Determine training cap */
    if ( ch->class == 0 )
        cap = 100;
    else if ( IS_CLASS(ch, CLASS_MAGE) )
        cap = 240;
    else
        return FALSE;

    /* ---- YELLOW identify cycle ---- */
    if ( bot->spell_training )
    {
        /* YELLOW reached cap: re-wear the ring and end the cycle */
        if ( ch->spl[YELLOW_MAGIC] >= cap )
        {
            if ( get_eq_char(ch, WEAR_FINGER_L) == NULL
              || get_eq_char(ch, WEAR_FINGER_R) == NULL )
            {
                bot_cmd( ch, "wear ring" );
            }
            bot->spell_training = FALSE;
            return TRUE;
        }

        /* Find the ring in inventory and keep casting identify */
        for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
        {
            if ( obj->wear_loc == WEAR_NONE && is_name("ring", obj->name) )
            {
                bot_cmd( ch, "cast 'identify' ring" );
                return TRUE;
            }
        }

        /* Ring vanished somehow — end cycle cleanly */
        bot->spell_training = FALSE;
        return FALSE;
    }

    /* ---- Normal color training ---- */
    color_idx = mage_lowest_color(ch, cap);
    if ( color_idx < 0 )
    {
        /* All colors at cap — end any active training cycle */
        bot->spell_training = FALSE;
        return FALSE;
    }

    if ( color_idx == YELLOW_MAGIC )
    {
        /* Find a worn ring to remove and start the cycle */
        OBJ_DATA *worn_ring = get_eq_char(ch, WEAR_FINGER_L);
        if ( worn_ring == NULL ) worn_ring = get_eq_char(ch, WEAR_FINGER_R);

        if ( worn_ring != NULL )
        {
            bot->spell_training = TRUE;
            bot_cmd( ch, "remove ring" );
            return TRUE;
        }

        /* Ring already in inventory — enter cycle directly */
        for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
        {
            if ( obj->wear_loc == WEAR_NONE && is_name("ring", obj->name) )
            {
                bot->spell_training = TRUE;
                bot_cmd( ch, "cast 'identify' ring" );
                return TRUE;
            }
        }

        /* No ring available this tick — skip */
        return FALSE;
    }

    /* All other colors: cast the training spell and set the flag */
    sn = skill_lookup( color_train[color_idx].spell );
    if ( sn < 0 || ch->pcdata->learned[sn] < 1 ) return FALSE;

    bot->spell_training = TRUE;
    if ( color_train[color_idx].arg[0] != '\0' )
        sprintf( cmd, "cast '%s' %s", color_train[color_idx].spell,
                 color_train[color_idx].arg );
    else
        sprintf( cmd, "cast '%s'", color_train[color_idx].spell );

    bot_cmd( ch, cmd );
    return TRUE;
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_mage_ai = {
    bot_mage_should_train,   /* should_train   */
    bot_mage_do_train,       /* do_train       */
    bot_mage_buff_check,     /* buff_check     */
    bot_mage_combat_action,  /* combat_action  */
    bot_mage_between_fights  /* between_fights */
};
