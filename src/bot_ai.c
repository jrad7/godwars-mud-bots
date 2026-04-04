/*
 * bot_ai.c - Bot AI state machine for Dystopia MUD
 *
 * Controls what bots do each pulse: idle chat, exploring, grinding mobs,
 * resting to recover, and logging out naturally.
 */

#if defined(macintosh)
#include <types.h>
#else
#include <sys/types.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "merc.h"
#include "bot.h"

/* Class AI vtables - defined in bot_ai_{class}.c, registered below */
extern const BOT_CLASS_AI bot_vamp_ai;
extern const BOT_CLASS_AI bot_monk_ai;
extern const BOT_CLASS_AI bot_ninja_ai;
extern const BOT_CLASS_AI bot_demon_ai;
extern const BOT_CLASS_AI bot_drow_ai;
extern const BOT_CLASS_AI bot_werewolf_ai;

/*
 * bot_class_ai - vtable table indexed by BOT_CLASS_*
 * Add a row here when registering a new class.
 */
const BOT_CLASS_AI *bot_class_ai[BOT_CLASS_COUNT] = {
    &bot_vamp_ai,      /* BOT_CLASS_VAMPIRE   */
    &bot_monk_ai,      /* BOT_CLASS_MONK      */
    &bot_ninja_ai,     /* BOT_CLASS_NINJA     */
    &bot_demon_ai,     /* BOT_CLASS_DEMON     */
    &bot_drow_ai,      /* BOT_CLASS_DROW      */
    &bot_werewolf_ai   /* BOT_CLASS_WEREWOLF  */
};

/* Forward declarations for stance functions defined in kav_fight.c / fight.c */
void do_stance( CHAR_DATA *ch, char *argument );
void do_autostance( CHAR_DATA *ch, char *argument );

/* Index into ch->stance[] for the autostance setting */
#define MONK_AUTODROP  12

static CHAR_DATA *bot_find_pvp_target( CHAR_DATA *ch );

/* -----------------------------------------------------------------------
 * bot_pick_training_stance
 *
 * Returns the STANCE_* ID of the next stance to train, in order:
 *   basics first (viper, crane, crab, mongoose, bull),
 *   then advanced (mantis, dragon, tiger, monkey, swallow) once unlocked.
 * Returns 0 if all available stances are mastered (>= 200 XP).
 * ----------------------------------------------------------------------- */
static int bot_pick_training_stance( CHAR_DATA *ch )
{
    static const int basic[] = {
        STANCE_VIPER, STANCE_CRANE, STANCE_CRAB, STANCE_MONGOOSE, STANCE_BULL
    };
    int i, xp;

    /* Train all five basic stances first */
    for ( i = 0; i < 5; i++ )
    {
        xp = ch->stance[ basic[i] ];
        if ( xp < 0 ) xp = 0;   /* -1 means locked, treat as unstarted */
        if ( xp < 200 )
            return basic[i];
    }

    if ( ch->stance[STANCE_CRANE]    >= 200 && ch->stance[STANCE_VIPER]    >= 200
      && ch->stance[STANCE_MANTIS]   >= 0   && ch->stance[STANCE_MANTIS]   < 200 )
        return STANCE_MANTIS;

    if ( ch->stance[STANCE_CRAB]     >= 200 && ch->stance[STANCE_BULL]     >= 200
      && ch->stance[STANCE_DRAGON]   >= 0   && ch->stance[STANCE_DRAGON]   < 200 )
        return STANCE_DRAGON;

    if ( ch->stance[STANCE_BULL]     >= 200 && ch->stance[STANCE_VIPER]    >= 200
      && ch->stance[STANCE_TIGER]    >= 0   && ch->stance[STANCE_TIGER]    < 200 )
        return STANCE_TIGER;

    if ( ch->stance[STANCE_CRANE]    >= 200 && ch->stance[STANCE_MONGOOSE] >= 200
      && ch->stance[STANCE_MONKEY]   >= 0   && ch->stance[STANCE_MONKEY]   < 200 )
        return STANCE_MONKEY;

    if ( ch->stance[STANCE_CRAB]     >= 200 && ch->stance[STANCE_MONGOOSE] >= 200
      && ch->stance[STANCE_SWALLOW]  >= 0   && ch->stance[STANCE_SWALLOW]  < 200 )
        return STANCE_SWALLOW;

    /* Superstances */
    if ( ch->stance[19] != -1 && ch->stance[13] < 200 ) return 13; /* STANCE_SS1 */
    if ( ch->stance[20] != -1 && ch->stance[14] < 200 ) return 14; /* STANCE_SS2 */
    if ( ch->stance[21] != -1 && ch->stance[15] < 200 ) return 15; /* STANCE_SS3 */
    if ( ch->stance[22] != -1 && ch->stance[16] < 200 ) return 16; /* STANCE_SS4 */
    if ( ch->stance[23] != -1 && ch->stance[17] < 200 ) return 17; /* STANCE_SS5 */

    return 0;   /* all available stances mastered */
}

/* -----------------------------------------------------------------------
 * bot_set_autostance
 *
 * Sets the bot's autostance to the current training stance.
 * Stays on the current stance until it reaches 200 XP, then advances.
 * Called for all classes each grinding tick so every class trains stances.
 * ----------------------------------------------------------------------- */
static void bot_set_autostance( CHAR_DATA *ch )
{
    /* Indexed by STANCE_* value (0 unused, 1-10 = viper..swallow) */
    static const char *stance_names[] = {
        NULL,        /* 0  STANCE_NORMAL   */
        "viper",     /* 1  STANCE_VIPER    */
        "crane",     /* 2  STANCE_CRANE    */
        "crab",      /* 3  STANCE_CRAB     */
        "mongoose",  /* 4  STANCE_MONGOOSE */
        "bull",      /* 5  STANCE_BULL     */
        "mantis",    /* 6  STANCE_MANTIS   */
        "dragon",    /* 7  STANCE_DRAGON   */
        "tiger",     /* 8  STANCE_TIGER    */
        "monkey",    /* 9  STANCE_MONKEY   */
        "swallow",   /* 10 STANCE_SWALLOW  */
        "wolf",      /* 11 STANCE_WOLF     */
        NULL,        /* 12 MONK_AUTODROP   */
        "ss1",       /* 13 STANCE_SS1      */
        "ss2",       /* 14 STANCE_SS2      */
        "ss3",       /* 15 STANCE_SS3      */
        "ss4",       /* 16 STANCE_SS4      */
        "ss5",       /* 17 STANCE_SS5      */
    };
    int current = ch->stance[MONK_AUTODROP];
    int pick;

    /* Stick with current stance until fully mastered */
    if ( current >= STANCE_VIPER && current <= 17 /* STANCE_SS5 */
      && current != 11 /* STANCE_WOLF */
      && current != 12 /* MONK_AUTODROP */
      && ch->stance[current] < 200 )
        return;

    /* Current mastered (or unset) - advance to next unmastered */
    pick = bot_pick_training_stance( ch );
    if ( pick == 0 )
        return;   /* all mastered - keep last autostance */

    if ( ch->stance[MONK_AUTODROP] == pick )
        return;   /* already set correctly */

    do_autostance( ch, (char *)stance_names[ pick ] );
}

/* -----------------------------------------------------------------------
 * bot_cmd - inject a command into a bot as if it typed it
 * ----------------------------------------------------------------------- */

void bot_cmd( CHAR_DATA *ch, const char *cmd )
{
    char buf[MAX_INPUT_LENGTH];
    strncpy( buf, cmd, sizeof(buf)-1 );
    buf[sizeof(buf)-1] = '\0';

    /* If a player is watching this bot, echo the command before executing */
    if ( ch->desc != NULL && ch->desc->snoop_by != NULL )
    {
        char echo[MAX_INPUT_LENGTH + 128];
        snprintf( echo, sizeof(echo), "<%dhp %dm %dmv %dxp> %s> %s\n\r", 
                  ch->hit, ch->mana, ch->move, ch->exp,
                  ch->name, buf );
        write_to_buffer( ch->desc->snoop_by, echo, 0 );
    }

    interpret( ch, buf );
}

/* -----------------------------------------------------------------------
 * bot_watch_msg - send a tagged debug line to the watchbot watcher, if any.
 * msg must already include the trailing \n\r.
 * ----------------------------------------------------------------------- */
void bot_watch_msg( CHAR_DATA *ch, const char *msg )
{
    if ( ch->desc == NULL || ch->desc->snoop_by == NULL ) return;
    write_to_buffer( ch->desc->snoop_by, msg, 0 );
}

/* Queue a navigation command to be executed before normal AI resumes */
static void bot_nav_queue( BOT_DATA *bot, const char *cmd )
{
    if ( bot->nav_n < 32 )
    {
        strncpy( bot->nav_cmds[bot->nav_n], cmd, sizeof(bot->nav_cmds[0])-1 );
        bot->nav_cmds[bot->nav_n][sizeof(bot->nav_cmds[0])-1] = '\0';
        bot->nav_n++;
    }
}

/* -----------------------------------------------------------------------
 * Area zone rules - restrict bot movement in specific areas
 *
 * Add a row for each zone:  { first_vnum, last_vnum, forbidden_dirs }
 * forbidden_dirs is a bitmask of DIR_* bits (1<<DIR_NORTH etc.).
 * Use 0 in last row as terminator.
 * ----------------------------------------------------------------------- */

#define DIRMASK(d)  (1 << (d))

static const struct {
    int   vnum_lo;
    int   vnum_hi;
    int   forbidden_dirs;  /* bitmask of 1<<DIR_* */
} bot_area_rules[] = {
    /* Mud School / newbie arena (3700-3760): don't go up (exits back to temple) */
    { 3700, 3760, DIRMASK(DIR_UP) },
    /* Elemental Canyon entrance (9201): don't go north back out to the world */
    { 9201, 9201, DIRMASK(DIR_NORTH) },
    /* Jobo's Hell (30100-30200): don't go up (exits back through haon.are shaft via D4) */
    { 30100, 30200, DIRMASK(DIR_UP) },
    /* Jobo's Heaven entrance (99000): don't go down (exits back to temple via D5) */
    { 99000, 99000, DIRMASK(DIR_DOWN) },
    /* Smurf Village entrance path (101): south exits to room 3040, outside the zone */
    { 101, 101, DIRMASK(DIR_SOUTH) },
    /* Shire entrance path (1100): south exits back to Haon-Dor (6000) */
    { 1100, 1100, DIRMASK(DIR_SOUTH) },
    /* Weed entrance (30232): south exits to room 4773, outside the zone */
    { 30232, 30232, DIRMASK(DIR_SOUTH) },

    { 0, 0, 0 }   /* terminator */
};

/* Returns TRUE if the bot is allowed to move in the given direction */
static bool bot_dir_allowed( CHAR_DATA *ch, int door )
{
    int vnum, i;
    if ( ch->in_room == NULL ) return TRUE;
    vnum = ch->in_room->vnum;
    for ( i = 0; bot_area_rules[i].vnum_lo != 0; i++ )
    {
        if ( vnum >= bot_area_rules[i].vnum_lo
          && vnum <= bot_area_rules[i].vnum_hi )
        {
            if ( IS_SET( bot_area_rules[i].forbidden_dirs, DIRMASK(door) ) )
                return FALSE;
        }
    }
    return TRUE;
}

static void bot_navigate_to_grind_zone( BOT_DATA *bot, CHAR_DATA *ch );

/* -----------------------------------------------------------------------
 * bot_change_state - transition to a new AI state
 * ----------------------------------------------------------------------- */

void bot_change_state( CHAR_DATA *ch, BOT_DATA *bot, bot_state_t new_state )
{
    static const char *state_names[] = {
        "IDLE", "EXPLORING", "GRINDING", "TRAINING",
        "PVP_HUNT", "PVP_FIGHT", "SHOPPING", "RESTING", "LOGGING_OUT"
    };

    /* Save progress whenever leaving the training state */
    if ( bot->state == BOT_TRAINING && bot->state != new_state )
        bot_cmd( ch, "save" );

    if ( bot->state != new_state )
    {
#if BOT_DEBUG
        {
            char buf[MAX_STRING_LENGTH];
            sprintf( buf, "BOT DEBUG: %s changing state from %s to %s",
                ch->name,
                bot->state <= BOT_LOGGING_OUT ? state_names[bot->state] : "UNKNOWN",
                new_state <= BOT_LOGGING_OUT ? state_names[new_state] : "UNKNOWN" );
            log_string( buf );
        }
#endif

        /* Notify any watcher via watchbot */
        if ( ch->desc != NULL && ch->desc->snoop_by != NULL )
        {
            char echo[256];
            snprintf( echo, sizeof(echo), "[STATE] %s: %s --> %s\n\r",
                ch->name,
                bot->state <= BOT_LOGGING_OUT ? state_names[bot->state] : "UNKNOWN",
                new_state  <= BOT_LOGGING_OUT ? state_names[new_state]  : "UNKNOWN" );
            write_to_buffer( ch->desc->snoop_by, echo, 0 );
        }
    }

    bot->state       = new_state;
    bot->cmd_delay   = number_range( 1, 2 );

    /* Clear any pending nav commands on state change -- stale nav from a
     * previous GRINDING state would otherwise block the state machine from
     * running (e.g. after death, bot sits issuing "stand" forever). */
    if ( new_state != BOT_GRINDING )
        bot->nav_n = 0;

    switch ( new_state )
    {
    case BOT_IDLE:
        bot->state_timer = number_range( BOT_TIMER_IDLE_MIN, BOT_TIMER_IDLE_MAX );
        break;
    case BOT_EXPLORING:
        bot->state_timer = number_range( BOT_TIMER_EXPLORING_MIN, BOT_TIMER_EXPLORING_MAX );
        break;
    case BOT_GRINDING:
        bot->state_timer = number_range( BOT_TIMER_GRINDING_MIN, BOT_TIMER_GRINDING_MAX );
        bot->grind_attempts = 0;
        bot->scatter_steps = 0;
        bot_navigate_to_grind_zone( bot, ch );
        break;
    case BOT_TRAINING:
        bot->state_timer = number_range( BOT_TIMER_TRAINING_MIN, BOT_TIMER_TRAINING_MAX );
        break;
    case BOT_RESTING:
        bot->state_timer = number_range( BOT_TIMER_RESTING_MIN, BOT_TIMER_RESTING_MAX );
        break;
    case BOT_LOGGING_OUT:
        bot->state_timer = number_range( BOT_TIMER_LOGOUT_MIN, BOT_TIMER_LOGOUT_MAX );
        break;
    default:
        bot->state_timer = number_range( BOT_TIMER_DEFAULT_MIN, BOT_TIMER_DEFAULT_MAX );
        break;
    }
    bot->state_timer_max = bot->state_timer;
}

/* -----------------------------------------------------------------------
 * Grind zone navigation table
 *
 * Each zone_* array is a NULL-terminated list of commands to queue.
 * Each GRIND_TIER groups zones that are appropriate up to max_hit.
 * The last tier acts as a catch-all (use a large sentinel).
 * To add a zone: define its route array, add it to the right tier.
 * ----------------------------------------------------------------------- */

static const char *zone_mud_school[]  = { "recall", "up", "open door", "south", NULL };
static const char *zone_jobo_heaven[] = { "recall", "down", NULL };
static const char *zone_smurf[]       = { "recall", "south", "south", "west", "west", "west", "north", NULL };
/* 3001->3005->3014->3013->3012->3040->3052->6000->6001->6002->30200->30199->30100 */
static const char *zone_jobo_hell[]   = { "recall", "south", "south", "west", "west", "west", "west", "west", "west", "west", "down", "down", "down", NULL };
/* recall(3001)->2S->5W->N */
static const char *zone_shire[]       = { "recall", "south", "south", "west", "west", "west", "west", "west", "north", NULL };
/* recall(3001)->2S->6E->4S->2E->S->2E->D->S */
static const char *zone_canyon[]      = { "recall", "south", "south", "east", "east", "east", "east", "east", "east", "south", "south", "south", "south", "east", "east", "south", "east", "east", "down", "south", NULL };
static const char *zone_weed[]        = { "recall", "south", "south", "east", "east", "east", "east", "east", "east", "north", "north", "north", "east", "east", "up", "up", "up", "up", "up", "east", "east", "down", "east", "north", "east", "north", NULL };

typedef struct {
    int           max_hit;      /* use this tier when ch->max_hit < max_hit */
    const char  **routes[8];    /* NULL-sentinel-terminated command lists    */
    int           num_routes;
} GRIND_TIER;

static const GRIND_TIER grind_tiers[] = {
    { 5000,  { zone_mud_school, zone_jobo_heaven }, 2 },
    { 10000, { zone_smurf,      zone_jobo_hell   }, 2 },
    { 20000, { zone_canyon,     zone_shire       }, 2 },
    { 99999, { zone_weed                         }, 1 },
};
#define GRIND_TIER_COUNT ( (int)( sizeof(grind_tiers) / sizeof(grind_tiers[0]) ) )

/* Maps each route pointer to a human-readable zone name for watchbot output */
static const struct { const char **route; const char *name; const char *filename; } route_names[] = {
    { zone_mud_school,  "mud_school",  "school.are" },
    { zone_jobo_heaven, "jobo_heaven", "heaven.are" },
    { zone_smurf,       "smurf",       "smurf.are"  },
    { zone_jobo_hell,   "jobo_hell",   "hell.are"   },
    { zone_canyon,      "canyon",      "canyon.are" },
    { zone_shire,       "shire",       "shire.are"  },
    { zone_weed,        "weed",        "weed.are"   },
    { NULL, NULL, NULL }
};

static void bot_navigate_to_grind_zone( BOT_DATA *bot, CHAR_DATA *ch )
{
    int i;
    const GRIND_TIER *tier = &grind_tiers[GRIND_TIER_COUNT - 1];
    const char **route;
    const char **step;
    const char *zone_name = "unknown";
    const char *zone_filename = NULL;

    for ( i = 0; i < GRIND_TIER_COUNT; i++ )
    {
        if ( ch->max_hit < grind_tiers[i].max_hit )
        {
            tier = &grind_tiers[i];
            break;
        }
    }

    route = tier->routes[number_range( 0, tier->num_routes - 1 )];

    for ( i = 0; route_names[i].route != NULL; i++ )
    {
        if ( route_names[i].route == route )
        {
            zone_name = route_names[i].name;
            zone_filename = route_names[i].filename;
            break;
        }
    }

    /* Check if already in the target zone */
    if ( ch->in_room != NULL && ch->in_room->area != NULL && ch->in_room->area->filename != NULL && zone_filename != NULL )
    {
        if ( !str_cmp( ch->in_room->area->filename, zone_filename ) )
        {
            /* Notify watcher that we are skipping navigation */
            if ( ch->desc != NULL && ch->desc->snoop_by != NULL )
            {
                char echo[256];
                snprintf( echo, sizeof(echo),
                    "[NAV] %s: already in %s, skipping navigation\n\r",
                    ch->name, zone_name );
                write_to_buffer( ch->desc->snoop_by, echo, 0 );
            }
            bot->nav_n = 0;
            return;
        }
    }

    bot->nav_n = 0;
    for ( step = route; *step != NULL; step++ )
        bot_nav_queue( bot, *step );

    /* Notify watcher which zone was selected */
    if ( ch->desc != NULL && ch->desc->snoop_by != NULL )
    {
        char echo[256];
        snprintf( echo, sizeof(echo),
            "[NAV] %s: queued %d-step route to %s (tier max_hit=%d, bot max_hit=%d)\n\r",
            ch->name, bot->nav_n, zone_name, tier->max_hit, ch->max_hit );
        write_to_buffer( ch->desc->snoop_by, echo, 0 );
    }
}

/* -----------------------------------------------------------------------
 * State helpers
 * ----------------------------------------------------------------------- */

/* Map BOT_CLASS_* preference to the selfclass argument string */
static const char *bot_class_name( int class_pref )
{
    switch ( class_pref )
    {
    case BOT_CLASS_VAMPIRE: return "vampire";
    case BOT_CLASS_MONK:    return "monk";
    case BOT_CLASS_NINJA:   return "ninja";
    case BOT_CLASS_DEMON:   return "demon";
    case BOT_CLASS_DROW:      return "drow";
    case BOT_CLASS_WEREWOLF:  return "werewolf";
    default:                  return "demon";
    }
}

/*
 * bot_primal_target - how much primal this bot needs to afford one class gear piece.
 * Werewolf gear costs 150; all others cost 60.
 * Returns 0 if the bot has no class yet (unclassed bots don't need primal).
 */
static int bot_primal_target( CHAR_DATA *ch )
{
    BOT_DATA *bot = ch->pcdata->botdata;
    if ( ch->class == 0 || bot == NULL || bot->roster == NULL )
        return 0;
    return 60;   /* moonarmour costs 60 primal/piece — same as other classes */
}

/*
 * bot_should_train_primal - TRUE if bot needs more primal and can afford one point.
 * Cost for the next primal point is (current_practice + 1) * 500 exp.
 */
static bool bot_should_train_primal( CHAR_DATA *ch )
{
    int target = bot_primal_target( ch );
    int cost_next;
    if ( target == 0 || ch->practice >= target )
        return FALSE;
    cost_next = ( ch->practice + 1 ) * 500;
    return ( ch->exp >= cost_next );
}

static bool bot_should_practice( CHAR_DATA *ch, const char *spell_name )
{
    int sn = skill_lookup( spell_name );
    if ( sn < 0 ) return FALSE;
    if ( ch->level < skill_table[sn].skill_level ) return FALSE;
    if ( ch->pcdata->learned[sn] >= 100 ) return FALSE;
    
    /* We just check if they have at least 5000 exp.
     * The actual practice command handles the exact deduction. */
    if ( ch->exp >= 5000 ) return TRUE;
    
    return FALSE;
}

static bool bot_needs_repair( CHAR_DATA *ch )
{
    OBJ_DATA *obj;
    for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
    {
        if ( obj->condition < 100 && can_see_obj( ch, obj ) )
            return TRUE;
    }
    return FALSE;
}

static bool bot_generic_buff_check( CHAR_DATA *ch )
{
    static const char *buffs[] = {
        "stone", "sanctuary", "shield", "armor", "bless", "frenzy", NULL
    };
    int i;
    int sn;

    /* Out of combat generic spellcasting/buffing */
    if ( ch->position == POS_FIGHTING )
        return FALSE;

    if ( IS_AFFECTED(ch, AFF_CURSE) )
    {
        sn = skill_lookup("remove curse");
        if ( sn > 0 && ch->pcdata->learned[sn] > 0 )
        {
            bot_cmd( ch, "cast rem self" );
            return TRUE;
        }
    }

    if ( bot_needs_repair(ch) )
    {
        sn = skill_lookup("repair");
        if ( sn > 0 && ch->pcdata->learned[sn] > 0 )
        {
            bot_cmd( ch, "cast repair" );
            return TRUE;
        }
    }

    /* Target generic buffs */
    for ( i = 0; buffs[i] != NULL; i++ )
    {
        sn = skill_lookup(buffs[i]);
        if ( sn > 0 && ch->pcdata->learned[sn] > 0 && !is_affected(ch, sn) )
        {
            /* spell_sanctuary guards via IS_AFFECTED (bitvector), not is_affected
             * (AFFECT_DATA list), so gear-provided sanctuary won't appear in the
             * list — check the bitvector directly to avoid an infinite cast loop. */
            if ( !strcmp(buffs[i], "sanctuary") && IS_AFFECTED(ch, AFF_SANCTUARY) )
                continue;
            char cmd[64];
            sprintf(cmd, "cast %s", buffs[i]);
            bot_cmd(ch, cmd);
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * Returns TRUE if the bot has exp worth spending on stats or class rank.
 */
static bool bot_should_train( CHAR_DATA *ch )
{
    int hp_cap = UMIN( 120000, 20000 + 4000 * ch->pkill );

    if ( ch->level == 2 && ch->max_hit >= 2000 )               return TRUE;
    if ( ch->level == 3 && ch->class == 0 )                     return TRUE;

    /* Primal for class gear takes priority over hp/mana/move spending */
    if ( bot_should_train_primal(ch) )                           return TRUE;

    /* Check if we need to practice generic spells */
    {
        static const char *practice_spells[] = {
            "repair", "rem", "stone", "sanctuary", "shield", "armor", "bless", "frenzy", NULL
        };
        int i;
        for ( i = 0; practice_spells[i] != NULL; i++ )
        {
            if ( bot_should_practice(ch, practice_spells[i]) )
                return TRUE;
        }
    }

    /* Delegate class-specific rank/skill checks to the class AI */
    {
        BOT_DATA *bot = ch->pcdata->botdata;
        if ( bot && bot->roster )
        {
            const BOT_CLASS_AI *ai = bot_class_ai[bot->roster->class_pref];
            if ( ai && ai->should_train && ai->should_train(ch) )
                return TRUE;
        }
    }

    /* Check for Superstances pooling before training stats */
    {
        bool basic_maxed = TRUE;
        int i;
        int basics[] = { STANCE_VIPER, STANCE_CRANE, STANCE_CRAB, STANCE_MONGOOSE, STANCE_BULL,
                         STANCE_MANTIS, STANCE_DRAGON, STANCE_TIGER, STANCE_MONKEY, STANCE_SWALLOW };
        for ( i = 0; i < 10; i++ )
        {
            if ( ch->stance[basics[i]] < 200 ) { basic_maxed = FALSE; break; }
        }

        if ( basic_maxed && ch->stance[23] == -1 )
        {
            if ( ch->stance[18] != 0 ) return TRUE;
            if ( ch->stance[19] == -1 ) 
            {
                if ( ch->exp >= 80000000 ) return TRUE; else return FALSE; 
            }
            if ( ch->stance[19] != -1 && ch->stance[13] >= 200 && ch->stance[20] == -1 )
            {
                if ( ch->exp >= 120000000 ) return TRUE; else return FALSE;
            }
            if ( ch->stance[20] != -1 && ch->stance[14] >= 200 && ch->stance[21] == -1 )
            {
                if ( ch->exp >= 140000000 ) return TRUE; else return FALSE;
            }
            if ( ch->stance[21] != -1 && ch->stance[15] >= 200 && ch->stance[22] == -1 )
            {
                if ( ch->exp >= 200000000 ) return TRUE; else return FALSE;
            }
            if ( ch->stance[22] != -1 && ch->stance[16] >= 200 && ch->stance[23] == -1 )
            {
                if ( ch->exp >= 380000000 ) return TRUE; else return FALSE;
            }

            /* Block stats training if maxed basic but waiting for current ss mastery to hit 200 */
            return FALSE;
        }
    }

    /* Primary: dump all available exp into hp */
    if ( ch->max_hit  < hp_cap         && ch->exp >= ch->max_hit  + 1 ) return TRUE;
    /* Secondary: keep mana at parity with hp */
    if ( ch->max_mana < ch->max_hit    && ch->max_mana < hp_cap
      && ch->exp >= ch->max_mana + 1 )                                   return TRUE;
    /* Tertiary: keep move above a floor */
    if ( ch->max_move < 10000          && ch->exp >= ch->max_move + 1 ) return TRUE;

    return FALSE;
}

/*
 * Execute one round of training commands.
 * Returns TRUE if at least one train command was issued.
 */
static bool bot_do_train( CHAR_DATA *ch )
{
    if ( ch->position < POS_STANDING )
    {
        bot_cmd( ch, "stand" );
        return TRUE;
    }

    int hp_cap = UMIN( 120000, 20000 + 4000 * ch->pkill );

    /* Step 1: train avatar once at level 2 with enough hp */
    if ( ch->level == 2 && ch->max_hit >= 2000 )
    {
        bot_cmd( ch, "train avatar" );
        return TRUE;
    }

    /* Step 2: select class once avatar (level 3), no class yet */
    if ( ch->level == 3 && ch->class == 0 )
    {
        BOT_DATA *bot = ch->pcdata->botdata;
        int pref = ( bot && bot->roster ) ? bot->roster->class_pref : BOT_CLASS_DEMON;
        char cmd[64];
        sprintf( cmd, "selfclass %s", bot_class_name(pref) );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    /* Step 2.5: call all gear back after picking a class following a decap */
    if ( ch->level == 3 && ch->class != 0 )
    {
        BOT_DATA *bot = ch->pcdata->botdata;
        if ( bot && bot->decap_recovery )
        {
            bot->decap_recovery = FALSE;
            bot_cmd( ch, "call all" );
            return TRUE;
        }
    }

    /* Class-specific rank/skill progression (age, belts, disciplines, etc.) */
    {
        BOT_DATA *bot = ch->pcdata->botdata;
        if ( bot && bot->roster )
        {
            const BOT_CLASS_AI *ai = bot_class_ai[bot->roster->class_pref];
            if ( ai && ai->do_train && ai->do_train(ch) )
                return TRUE;
        }
    }

    /* Primal for class gear.
     * Buy as many primal points as we can afford in one shot, up to the
     * target ceiling.  The "train primal N" command loops internally and
     * stops when exp runs out, so passing the full remaining amount is safe. */
    if ( bot_should_train_primal(ch) )
    {
        int target  = bot_primal_target(ch);
        int needed  = target - ch->practice;   /* always >= 1 here */
        char cmd[32];
        sprintf( cmd, "train primal %d", needed );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    /* Generic spell practice */
    {
        static const char *practice_spells[] = {
            "repair", "remove curse", "stone skin", "sanctuary", "shield", "armor", "bless", "frenzy", NULL
        };
        int i;
        for ( i = 0; practice_spells[i] != NULL; i++ )
        {
            if ( bot_should_practice(ch, practice_spells[i]) )
            {
                char cmd[64];
                sprintf(cmd, "practice %s", practice_spells[i]);
                bot_cmd( ch, cmd );
                return TRUE;
            }
        }
    }

    /* Prioritize Superstances ahead of HP/Mana/Move */
    {
        bool basic_maxed = TRUE;
        int i;
        int basics[] = { STANCE_VIPER, STANCE_CRANE, STANCE_CRAB, STANCE_MONGOOSE, STANCE_BULL,
                         STANCE_MANTIS, STANCE_DRAGON, STANCE_TIGER, STANCE_MONKEY, STANCE_SWALLOW };
        for ( i = 0; i < 10; i++ )
        {
            if ( ch->stance[basics[i]] < 200 ) { basic_maxed = FALSE; break; }
        }

        if ( basic_maxed && ch->stance[23] == -1 )
        {
            if ( ch->stance[18] != 0 )
            {
                bot_cmd( ch, "setstance done" );
                return TRUE;
            }

            /* SS1 - 80m exp needed */
            if ( ch->stance[19] == -1 )
            {
                if ( ch->exp >= 80000000 )
                {
                    bot_cmd( ch, "setstance damcap lesser" );
                    bot_cmd( ch, "setstance rev_dc lesser" );
                    bot_cmd( ch, "setstance damage lesser" );
                    bot_cmd( ch, "setstance speed" );
                    bot_cmd( ch, "setstance done" );
                    return TRUE;
                }
                return FALSE; /* Pool if not enough */
            }
            /* SS2 - 120m exp needed */
            else if ( ch->stance[19] != -1 && ch->stance[13] >= 200 && ch->stance[20] == -1 )
            {
                if ( ch->exp >= 120000000 )
                {
                    bot_cmd( ch, "setstance damcap greater" );
                    bot_cmd( ch, "setstance rev_dc lesser" );
                    bot_cmd( ch, "setstance damage lesser" );
                    bot_cmd( ch, "setstance resist lesser" );
                    bot_cmd( ch, "setstance speed" );
                    bot_cmd( ch, "setstance done" );
                    return TRUE;
                }
                return FALSE; /* Pool */
            }
            /* SS3 - 140m exp needed */
            else if ( ch->stance[20] != -1 && ch->stance[14] >= 200 && ch->stance[21] == -1 )
            {
                if ( ch->exp >= 140000000 )
                {
                    bot_cmd( ch, "setstance damcap greater" );
                    bot_cmd( ch, "setstance rev_dc greater" );
                    bot_cmd( ch, "setstance damage lesser" );
                    bot_cmd( ch, "setstance resist lesser" );
                    bot_cmd( ch, "setstance speed" );
                    bot_cmd( ch, "setstance done" );
                    return TRUE;
                }
                return FALSE; /* Pool */
            }
            /* SS4 - 200m exp needed */
            else if ( ch->stance[21] != -1 && ch->stance[15] >= 200 && ch->stance[22] == -1 )
            {
                if ( ch->exp >= 200000000 )
                {
                    bot_cmd( ch, "setstance damcap supreme" );
                    bot_cmd( ch, "setstance rev_dc greater" );
                    bot_cmd( ch, "setstance damage greater" );
                    bot_cmd( ch, "setstance resist greater" );
                    bot_cmd( ch, "setstance speed" );
                    bot_cmd( ch, "setstance done" );
                    return TRUE;
                }
                return FALSE; /* Pool */
            }
            /* SS5 - 380m exp needed */
            else if ( ch->stance[22] != -1 && ch->stance[16] >= 200 && ch->stance[23] == -1 )
            {
                if ( ch->exp >= 380000000 )
                {
                    bot_cmd( ch, "setstance damcap supreme" );
                    bot_cmd( ch, "setstance rev_dc supreme" );
                    bot_cmd( ch, "setstance damage greater" );
                    bot_cmd( ch, "setstance resist lesser" );
                    bot_cmd( ch, "setstance speed" );
                    bot_cmd( ch, "setstance parry" );
                    bot_cmd( ch, "setstance dodge" );
                    bot_cmd( ch, "setstance bypass" );
                    bot_cmd( ch, "setstance done" );
                    return TRUE;
                }
                return FALSE; /* Pool */
            }
            
            /* Otherwise, wait for mastery */
            return FALSE;
        }
    }

    /* Primary: dump all available exp into hp */
    if ( ch->max_hit < hp_cap && ch->exp >= ch->max_hit + 1 )
        { bot_cmd( ch, "train hp all" );   return TRUE; }

    /* Secondary: keep mana at parity with hp */
    if ( ch->max_mana < ch->max_hit && ch->max_mana < hp_cap
      && ch->exp >= ch->max_mana + 1 )
        { bot_cmd( ch, "train mana all" ); return TRUE; }

    /* Tertiary: keep move above a floor */
    if ( ch->max_move < 10000 && ch->exp >= ch->max_move + 1 )
        { bot_cmd( ch, "train move all" ); return TRUE; }

    return FALSE;
}

static bool bot_is_healthy( CHAR_DATA *ch )
{
    return ( ch->hit  >= ch->max_hit  * 7 / 10
          && ch->mana >= ch->max_mana * 5 / 10
          && ch->move >= ch->max_move * 4 / 10 );
}

static bool bot_needs_rest( CHAR_DATA *ch )
{
    return ( ch->hit  < ch->max_hit  * 4 / 10
          || ch->mana < ch->max_mana * 3 / 10 );
}

/* Pick a random direction and try to move */
static void bot_try_move( CHAR_DATA *ch )
{
    int tries, door;
    EXIT_DATA *pexit;

    for ( tries = 0; tries < 6; tries++ )
    {
        door = number_range( 0, 5 );
        if ( ch->in_room == NULL ) return;

        if ( !bot_dir_allowed( ch, door ) )              continue;
        pexit = ch->in_room->exit[door];
        if ( pexit == NULL || pexit->to_room == NULL ) continue;
        if ( IS_SET(pexit->exit_info, EX_CLOSED) )      continue;
        if ( IS_SET(pexit->to_room->room_flags, ROOM_PRIVATE) ) continue;

        bot_cmd( ch, dir_name[door] );
        return;
    }
}

/* Scatter move: prefer directions that are not the reverse of the last move.
 * Falls back to the reverse direction only if no other exit is usable. */
static void bot_scatter_move( CHAR_DATA *ch, BOT_DATA *bot )
{
    extern const sh_int rev_dir[];
    int tries, door;
    int avoid = ( bot->scatter_last_dir >= 0 ) ? rev_dir[bot->scatter_last_dir] : -1;
    EXIT_DATA *pexit;

    /* First pass: try random directions, skipping the back-direction */
    for ( tries = 0; tries < 12; tries++ )
    {
        door = number_range( 0, 5 );
        if ( ch->in_room == NULL ) return;
        if ( door == avoid ) continue;

        if ( !bot_dir_allowed( ch, door ) )                      continue;
        pexit = ch->in_room->exit[door];
        if ( pexit == NULL || pexit->to_room == NULL )           continue;
        if ( IS_SET(pexit->exit_info, EX_CLOSED) )               continue;
        if ( IS_SET(pexit->to_room->room_flags, ROOM_PRIVATE) )  continue;

        bot->scatter_last_dir = door;
        bot_cmd( ch, dir_name[door] );
        return;
    }

    /* Second pass: allow the back-direction as a last resort */
    for ( tries = 0; tries < 6; tries++ )
    {
        door = number_range( 0, 5 );
        if ( ch->in_room == NULL ) return;

        if ( !bot_dir_allowed( ch, door ) )                      continue;
        pexit = ch->in_room->exit[door];
        if ( pexit == NULL || pexit->to_room == NULL )           continue;
        if ( IS_SET(pexit->exit_info, EX_CLOSED) )               continue;
        if ( IS_SET(pexit->to_room->room_flags, ROOM_PRIVATE) )  continue;

        bot->scatter_last_dir = door;
        bot_cmd( ch, dir_name[door] );
        return;
    }
}

/* Find a mob in the room that's safe to attack */
static CHAR_DATA *bot_find_mob_target( CHAR_DATA *ch )
{
    CHAR_DATA *victim;

    if ( ch->in_room == NULL ) return NULL;

    for ( victim = ch->in_room->people; victim != NULL; victim = victim->next_in_room )
    {
        char dbg[256];
        snprintf( dbg, sizeof(dbg), "[TARGET_DBG] checking '%s': IS_NPC=%d fighting=%s level=%d(max=%d) ACT_IS_NPC=%d\n\r",
            victim->name,
            IS_NPC(victim) ? 1 : 0,
            victim->fighting ? victim->fighting->name : "none",
            victim->level, ch->level + 15,
            IS_SET(victim->act, ACT_IS_NPC) ? 1 : 0 );
        bot_watch_msg( ch, dbg );
        if ( !IS_NPC(victim) )   continue;   /* Don't attack players */
        if ( victim->fighting )  continue;   /* Skip mobs already in combat */
        if ( IS_SET(victim->act, ACT_IS_NPC) ) return victim;
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * STATE HANDLERS
 * ----------------------------------------------------------------------- */

static void bot_state_idle( CHAR_DATA *ch, BOT_DATA *bot )
{
    /* Unprompted chat */
    bot->idle_chat_timer--;
    if ( bot->idle_chat_timer <= 0 )
    {
        bot_unprompted_chat( ch, bot );
        bot->idle_chat_timer = number_range( 30, 120 );   /* 30-120 seconds */
    }

    /* Random look or social */
    if ( number_percent() < 10 )
    {
        int r = number_range( 1, 5 );
        switch ( r )
        {
        case 1: bot_cmd( ch, "look" );   break;
        case 2: bot_cmd( ch, "nod" );    break;
        case 3: bot_cmd( ch, "grin" );   break;
        case 4: bot_cmd( ch, "yawn" );   break;
        case 5: bot_cmd( ch, "sit" );    break;
        }
    }

    /* Transition check */
    if ( bot->state_timer <= 0 )
    {
        /* Decapped bots are level-2 mortals with no HP regen -- training takes
         * priority over resting since resting will never resolve. */
        if ( ch->level == 2 && ch->max_hit >= 2000 )
        {
            bot_watch_msg( ch, "[REASON] mortal after decap, needs train avatar\n\r" );
            bot_change_state( ch, bot, BOT_TRAINING );
            return;
        }
        if ( bot_needs_rest(ch) )
        {
            char r[128];
            snprintf( r, sizeof(r), "[REASON] needs rest: hp %d%% mana %d%%\n\r",
                ch->hit  * 100 / UMAX(1, ch->max_hit),
                ch->mana * 100 / UMAX(1, ch->max_mana) );
            bot_watch_msg( ch, r );
            bot_change_state( ch, bot, BOT_RESTING );
            return;
        }
        if ( bot_should_train(ch) )
        {
            bot_watch_msg( ch, "[REASON] has exp/primal to spend\n\r" );
            bot_change_state( ch, bot, BOT_TRAINING );
            return;
        }

        /* Check for PVP */
        bool do_pvp = FALSE;
        if ( global_bot_pvp_mode == BOT_PVP_MODE_WAR )
            do_pvp = TRUE;
        else if ( global_bot_pvp_mode == BOT_PVP_MODE_NORMAL && bot->roster && bot->roster->aggression > 0 && number_percent() < bot->roster->aggression )
            do_pvp = TRUE;

        if ( do_pvp )
        {
            CHAR_DATA *target = bot_find_pvp_target(ch);
            if ( target != NULL )
            {
                char msg[256];
                strncpy(bot->pvp_target, target->name, sizeof(bot->pvp_target) - 1);
                bot->pvp_target[sizeof(bot->pvp_target) - 1] = '\0';
                snprintf(msg, sizeof(msg), "[PVP] Selected %s for hunting\n\r", bot->pvp_target);
                bot_watch_msg( ch, msg );
                bot_change_state( ch, bot, BOT_PVP_HUNT );
                return;
            }
        }

        /* Weighted state pick */
        int roll = number_range( 1, 100 );
        if ( roll <= 40 )
            bot_change_state( ch, bot, BOT_GRINDING );
        else if ( roll <= 70 )
            bot_change_state( ch, bot, BOT_EXPLORING );
        else
            bot_change_state( ch, bot, BOT_IDLE );   /* Stay idle */
    }
}

static bool bot_is_valid_pvp( CHAR_DATA *ch, CHAR_DATA *victim, char *dbg, size_t dbg_sz )
{
    if ( IS_SET(victim->in_room->room_flags, ROOM_ARENA) || IS_SET(victim->in_room->room_flags, ROOM_SAFE) )
    {
        if ( dbg ) snprintf(dbg, dbg_sz, "[PVP_DBG] %s rejected: in safe room\n\r", victim->name);
        return FALSE;
    }
    if ( is_safe(ch, victim) )
    {
        if ( dbg ) snprintf(dbg, dbg_sz, "[PVP_DBG] %s rejected: is_safe() is true\n\r", victim->name);
        return FALSE;
    }

    bool is_gensteal = FALSE;
    if ( ch->class == victim->class && victim->generation <= 7 && victim->generation > 1 )
    {
        if ( ch->generation >= victim->generation )
            is_gensteal = TRUE;
    }

    if ( !is_gensteal )
    {
        if ( !fair_fight(ch, victim) )
        {
            if ( dbg ) snprintf(dbg, dbg_sz, "[PVP_DBG] %s rejected: fair_fight() failed\n\r", victim->name);
            return FALSE;
        }
    }
    else
    {
        int my_m = getMight(ch);
        int vic_m = getMight(victim);
        if ( my_m * 100 < vic_m * 75 )
        {
            if ( dbg ) snprintf(dbg, dbg_sz, "[PVP_DBG] %s rejected: gensteal suicide blocked (M:%d vs %d)\n\r", victim->name, my_m, vic_m);
            return FALSE;
        }
    }
    
    return TRUE;
}

static CHAR_DATA *bot_find_pvp_target( CHAR_DATA *ch )
{
    CHAR_DATA *victim;
    CHAR_DATA *best_victim = NULL;
    int count = 0;
    char dbg[256];

    for ( victim = char_list; victim != NULL; victim = victim->next )
    {
        if ( IS_NPC(victim) ) continue;
        if ( victim == ch ) continue;
        if ( victim->in_room == NULL ) continue;

        if ( !bot_is_valid_pvp(ch, victim, dbg, sizeof(dbg)) )
        {
            bot_watch_msg(ch, dbg);
            continue;
        }
        
        /* Select randomly among valid victims */
        if ( number_range(0, count) == 0 )
            best_victim = victim;
        count++;
    }
    return best_victim;
}

static void bot_state_exploring( CHAR_DATA *ch, BOT_DATA *bot )
{
    if ( bot_needs_rest(ch) )
    {
        char r[128];
        snprintf( r, sizeof(r), "[REASON] needs rest: hp %d%% mana %d%%\n\r",
            ch->hit  * 100 / UMAX(1, ch->max_hit),
            ch->mana * 100 / UMAX(1, ch->max_mana) );
        bot_watch_msg( ch, r );
        bot_change_state( ch, bot, BOT_RESTING );
        return;
    }

    /* Move a random direction */
    if ( number_percent() < 60 )
        bot_try_move( ch );
    else if ( number_percent() < 30 )
        bot_cmd( ch, "look" );

    if ( bot->state_timer <= 0 )
        bot_change_state( ch, bot, BOT_IDLE );
}

static void bot_state_grinding( CHAR_DATA *ch, BOT_DATA *bot )
{
    CHAR_DATA *victim;

    /* Scatter: take random steps into the zone so bots don't all pile up at
     * the entrance.  Stop early if combat starts. */
    if ( bot->scatter_steps > 0 && ch->position != POS_FIGHTING )
    {
        bot_scatter_move( ch, bot );
        bot->scatter_steps--;
        return;
    }

    if ( bot_needs_rest(ch) )
    {
        char r[128];
        snprintf( r, sizeof(r), "[REASON] needs rest: hp %d%% mana %d%%\n\r",
            ch->hit  * 100 / UMAX(1, ch->max_hit),
            ch->mana * 100 / UMAX(1, ch->max_mana) );
        bot_watch_msg( ch, r );
        bot_change_state( ch, bot, BOT_RESTING );
        return;
    }

    /* Resolve the class AI vtable for this bot */
    {
        const BOT_CLASS_AI *ai = NULL;
        if ( bot->roster )
            ai = bot_class_ai[bot->roster->class_pref];

        /* Already fighting */
        if ( ch->position == POS_FIGHTING )
        {
            if ( ai && ai->combat_action )
                ai->combat_action( ch );
            bot->grind_attempts = 0;
            return;
        }

        /* Set autostance for stance training - applies to all classes */
        bot_set_autostance( ch );

        /* Between fights: buffs first, then any between-fight setup */
        if ( bot_generic_buff_check(ch) )
            return;
        if ( ai && ai->buff_check && ai->buff_check(ch) )
            return;   /* issued a buff command this tick */
        if ( ai && ai->between_fights && ai->between_fights(ch) )
            return;   /* issued a setup command this tick */
    }

    /* Timer expired: transition even if mobs are still present */
    if ( bot->state_timer <= 0 )
    {
        if ( bot_should_train(ch) )
        {
            bot_watch_msg( ch, "[REASON] has exp/primal to spend\n\r" );
            bot_change_state( ch, bot, BOT_TRAINING );
        }
        else
        {
            bool do_pvp = FALSE;
            if ( global_bot_pvp_mode == BOT_PVP_MODE_WAR ) do_pvp = TRUE;
            else if ( global_bot_pvp_mode == BOT_PVP_MODE_NORMAL && bot->roster && bot->roster->aggression > 0 && number_percent() < bot->roster->aggression ) do_pvp = TRUE;

            if ( do_pvp )
            {
                CHAR_DATA *target = bot_find_pvp_target(ch);
                if ( target != NULL )
                {
                    char msg[256];
                    strncpy(bot->pvp_target, target->name, sizeof(bot->pvp_target) - 1);
                    bot->pvp_target[sizeof(bot->pvp_target) - 1] = '\0';
                    snprintf(msg, sizeof(msg), "[PVP] Selected %s for hunting\n\r", bot->pvp_target);
                    bot_watch_msg( ch, msg );
                    bot_change_state( ch, bot, BOT_PVP_HUNT );
                }
                else
                {
                    bot_change_state( ch, bot, BOT_IDLE );
                }
            }
            else
            {
                bot_change_state( ch, bot, BOT_IDLE );
            }
        }
        return;
    }

    /* Find something to kill */
    victim = bot_find_mob_target( ch );
    if ( victim != NULL )
    {
        char cmd[MAX_INPUT_LENGTH];
        /* Don't start combat while still equipping newbiepack items */
        if ( bot_is_gearing( ch ) )
        {
            bot_watch_msg( ch, "[GEAR] still equipping — holding attack\n\r" );
            return;
        }
        sprintf( cmd, "kill %s", victim->name );
        bot_cmd( ch, cmd );
        bot->grind_attempts = 0;
        return;
    }

    /* No targets here - move to find some */
    bot->grind_attempts++;
    {
        char echo[256];
        snprintf( echo, sizeof(echo), "[GRIND] %s: no targets in room %d (attempt %d/3)\n\r",
            ch->name,
            ch->in_room ? ch->in_room->vnum : -1,
            bot->grind_attempts );
        bot_watch_msg( ch, echo );
    }
    if ( bot->grind_attempts >= 3 )
    {
        bot_try_move( ch );
        bot->grind_attempts = 0;
    }

    if ( bot->state_timer <= bot->state_timer_max / 2 )
    {
        if ( bot_should_train(ch) )
        {
            bot_watch_msg( ch, "[REASON] has exp/primal to spend\n\r" );
            bot_change_state( ch, bot, BOT_TRAINING );
        }
        else
        {
            bool do_pvp = FALSE;
            if ( global_bot_pvp_mode == BOT_PVP_MODE_WAR ) do_pvp = TRUE;
            else if ( global_bot_pvp_mode == BOT_PVP_MODE_NORMAL && bot->roster && bot->roster->aggression > 0 && number_percent() < bot->roster->aggression ) do_pvp = TRUE;

            if ( do_pvp )
            {
                CHAR_DATA *target = bot_find_pvp_target(ch);
                if ( target != NULL )
                {
                    char msg[256];
                    strncpy(bot->pvp_target, target->name, sizeof(bot->pvp_target) - 1);
                    bot->pvp_target[sizeof(bot->pvp_target) - 1] = '\0';
                    snprintf(msg, sizeof(msg), "[PVP] Selected %s for hunting\n\r", bot->pvp_target);
                    bot_watch_msg( ch, msg );
                    bot_change_state( ch, bot, BOT_PVP_HUNT );
                }
                else
                {
                    bot_change_state( ch, bot, BOT_IDLE );
                }
            }
            else
            {
                bot_change_state( ch, bot, BOT_IDLE );
            }
        }
    }
}

/* Find first step of shortest path from start to target. Returns direction 0-5, or -1 if no path. */
static int bot_find_path( ROOM_INDEX_DATA *start, ROOM_INDEX_DATA *target )
{
    struct bfs_queue {
        ROOM_INDEX_DATA *room;
        int first_dir;
    } *queue;
    bool *visited;
    int head = 0, tail = 0;
    int i, dir = -1;

    if ( start == NULL || target == NULL || start == target )
        return -1;

    /* A massive array of visited flags, indexed by vnum. Safe for up to vnum 200k */
    visited = calloc( 250000, sizeof(bool) );
    if ( visited == NULL ) return -1;

    /* A queue of rooms to check, max 20000 nodes searched */
    queue = calloc( 20000, sizeof(struct bfs_queue) );
    if ( queue == NULL )
    {
        free( visited );
        return -1;
    }

    visited[start->vnum] = TRUE;

    /* Enqueue initial available directions */
    for ( i = 0; i < 6; i++ )
    {
        EXIT_DATA *pexit = start->exit[i];
        if ( pexit != NULL && pexit->to_room != NULL 
          && !IS_SET(pexit->exit_info, EX_LOCKED) 
          && pexit->to_room->vnum < 250000 )
        {
            if ( pexit->to_room == target )
            {
                dir = i;
                goto done;
            }
            if ( !visited[pexit->to_room->vnum] )
            {
                visited[pexit->to_room->vnum] = TRUE;
                queue[tail].room = pexit->to_room;
                queue[tail].first_dir = i;
                tail++;
            }
        }
    }

    /* Process queue */
    while ( head < tail && tail < 19990 )
    {
        ROOM_INDEX_DATA *r = queue[head].room;
        int f_dir = queue[head].first_dir;
        head++;

        for ( i = 0; i < 6; i++ )
        {
            EXIT_DATA *pexit = r->exit[i];
            if ( pexit != NULL && pexit->to_room != NULL 
              && !IS_SET(pexit->exit_info, EX_LOCKED) 
              && pexit->to_room->vnum < 250000 )
            {
                if ( pexit->to_room == target )
                {
                    dir = f_dir;
                    goto done;
                }
                if ( !visited[pexit->to_room->vnum] )
                {
                    visited[pexit->to_room->vnum] = TRUE;
                    queue[tail].room = pexit->to_room;
                    queue[tail].first_dir = f_dir;
                    tail++;
                }
            }
        }
    }

done:
    free( visited );
    free( queue );
    return dir;
}

static void bot_state_pvp_hunt( CHAR_DATA *ch, BOT_DATA *bot )
{
    CHAR_DATA *victim;
    int dir;

    if ( ch->position == POS_FIGHTING )
    {
        bot_watch_msg( ch, "[PVP] Stuck in combat during hunt, fleeing\n\r" );
        do_flee( ch, "" );
        return;
    }

    if ( bot->pvp_target[0] == '\0' )
    {
        bot_change_state( ch, bot, BOT_GRINDING );
        return;
    }

    victim = get_char_world( ch, bot->pvp_target );
    if ( victim == NULL || !bot_is_valid_pvp(ch, victim, NULL, 0) )
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "[PVP] Target %s lost or no longer valid.\n\r", bot->pvp_target);
        bot_watch_msg( ch, msg );
        bot->pvp_target[0] = '\0';
        bot_change_state( ch, bot, BOT_GRINDING );
        return;
    }

    if ( ch->in_room == victim->in_room )
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "[PVP] Target %s found! Attacking.\n\r", bot->pvp_target);
        bot_watch_msg( ch, msg );
        bot_change_state( ch, bot, BOT_PVP_FIGHT );
        return;
    }

    /* Move towards target */
    dir = bot_find_path( ch->in_room, victim->in_room );
    
    if ( dir != -1 )
    {
        EXIT_DATA *pexit = ch->in_room->exit[dir];
        char echo[256];
        if ( pexit != NULL && IS_SET(pexit->exit_info, EX_CLOSED) )
        {
            char cmd[64];
            sprintf(cmd, "open %s", dir_name[dir]);
            bot_cmd( ch, cmd );
        }
        snprintf( echo, sizeof(echo), "[PVP] BFS found path -- stepping %s to reach %s\n\r", dir_name[dir], bot->pvp_target );
        bot_watch_msg( ch, echo );
        bot_cmd( ch, dir_name[dir] );
    }
    else
    {
        bot_watch_msg( ch, "[PVP] BFS failed -- target unreachable. Halting hunt.\n\r" );
        bot->pvp_target[0] = '\0';
        bot_change_state( ch, bot, BOT_GRINDING );
    }
}

static void bot_state_pvp_fight( CHAR_DATA *ch, BOT_DATA *bot )
{
    CHAR_DATA *victim;

    /* Wait out fight timer before leaving PVP_FIGHT state if target is gone */
    if ( bot->pvp_target[0] == '\0' )
    {
        if ( ch->fight_timer > 0 ) return;
        bot_change_state( ch, bot, BOT_GRINDING );
        return;
    }

    victim = get_char_world( ch, bot->pvp_target );
    if ( victim == NULL || !bot_is_valid_pvp(ch, victim, NULL, 0) )
    {
        /* Target dead or invalid */
        bot->pvp_target[0] = '\0';
        if ( ch->fight_timer > 0 ) return;
        bot_change_state( ch, bot, BOT_GRINDING );
        return;
    }

    if ( victim->in_room != ch->in_room )
    {
        bot_change_state( ch, bot, BOT_PVP_HUNT );
        return;
    }

    if ( victim->position <= POS_STUNNED )
    {
        /* Time to finish them */
        char cmd[256];
        if ( ch->class == victim->class && ch->generation >= victim->generation && victim->generation < 7 && victim->generation > 1 )
        {
            sprintf( cmd, "gensteal %s", victim->name );
            bot_cmd( ch, cmd );
        }
        else
        {
            sprintf( cmd, "decapitate %s", victim->name );
            bot_cmd( ch, cmd );
        }
        return;
    }

    /* Keep attacking if not fighting */
    if ( ch->position != POS_FIGHTING )
    {
        char cmd[256];
        sprintf( cmd, "kill %s", victim->name );
        bot_cmd( ch, cmd );
    }
    
    /* Execute class combat action */
    {
        const BOT_CLASS_AI *ai = NULL;
        if ( bot->roster ) ai = bot_class_ai[bot->roster->class_pref];
        if ( ch->position == POS_FIGHTING && ai && ai->combat_action )
            ai->combat_action( ch );
    }
}

static void bot_state_resting( CHAR_DATA *ch, BOT_DATA *bot )
{
    /* Fight back if attacked */
    if ( ch->position == POS_FIGHTING )
    {
        bot_watch_msg( ch, "[REASON] attacked while resting\n\r" );
        bot_change_state( ch, bot, BOT_IDLE );
        return;
    }

    /* Decapped bots are mortals (level 2) with no HP regen -- skip waiting and
     * go straight to training so they can 'train avatar' back to their class. */
    if ( ch->level == 2 && ch->max_hit >= 2000 )
    {
        bot_watch_msg( ch, "[REASON] mortal after decap, needs train avatar\n\r" );
        bot_change_state( ch, bot, BOT_TRAINING );
        return;
    }

    /* Demons without WARP_REGENERATE must heal in Hell (93420) */
    if ( IS_CLASS(ch, CLASS_DEMON) && !IS_SET(ch->warp, WARP_REGENERATE) )
    {
        if ( ch->in_room != NULL && !(ch->in_room->vnum >= ROOM_VNUM_HELL && ch->in_room->vnum <= ROOM_VNUM_HELL + 6) )
        {
            bot_watch_msg( ch, "[REASON] Demon retreating to Hell to heal\n\r" );
            char_from_room(ch);
            char_to_room(ch, get_room_index(ROOM_VNUM_HELL));
            bot_cmd(ch, "look");
        }
    }

    /* Just wait for HP to recover -- bot_ensure_geared handles standing/gearing */
    if ( bot_is_healthy(ch) || bot->state_timer <= 0 )
        bot_change_state( ch, bot, BOT_IDLE );
}

static void bot_state_training( CHAR_DATA *ch, BOT_DATA *bot )
{
    /* Abort if attacked */
    if ( ch->position == POS_FIGHTING )
    {
        bot_watch_msg( ch, "[REASON] attacked while training\n\r" );
        bot_change_state( ch, bot, BOT_GRINDING );
        return;
    }

    /* Attempt to train; if nothing left or timer expired, resume grinding */
    if ( !bot_do_train(ch) || bot->state_timer <= 0 )
        bot_change_state( ch, bot, BOT_GRINDING );
}

static void bot_state_logging_out( CHAR_DATA *ch, BOT_DATA *bot )
{
    if ( bot->state_timer <= 0 )
    {
        /* Say something before leaving */
        int r = number_range( 1, 4 );
        switch ( r )
        {
        case 1: bot_cmd( ch, "say Later everyone." );      break;
        case 2: bot_cmd( ch, "say gotta go, later" );      break;
        case 3: bot_cmd( ch, "say time to head out" );     break;
        case 4: bot_cmd( ch, "wave" );                     break;
        }

        /* Stand before quitting */
        if ( ch->position != POS_STANDING )
            bot_cmd( ch, "stand" );

        /* bot_logout handles save + do_quit */
        bot_logout( ch );
        /* ch is freed - do not access after this */
        return;
    }
}

/* -----------------------------------------------------------------------
 * bot_ensure_geared - delegates to bot_gear_check (bot_gear.c)
 *
 * bot_gear_check handles all position checks, surplus cleanup, newbiepack
 * fallback, and class-gear upgrades — one action per call.
 * ----------------------------------------------------------------------- */
static void bot_ensure_geared( CHAR_DATA *ch )
{
    bot_gear_check( ch );
}

/* -----------------------------------------------------------------------
 * bot_update - main per-bot update, called each PULSE_BOT_MANAGER
 * ----------------------------------------------------------------------- */

void bot_update( CHAR_DATA *ch )
{
    BOT_DATA *bot;

    if ( ch == NULL || ch->pcdata == NULL ) return;
    bot = ch->pcdata->botdata;
    if ( bot == NULL ) return;

    /* While in "head" state (LOST_HEAD set after a decap) the bot has no body
     * and cannot act.  behead() already called "call all" so class gear is
     * already in inventory; skip all AI and gear management until the head
     * respawns with a body and LOST_HEAD is cleared. */
    if ( IS_HEAD( ch, LOST_HEAD ) )
    {
        bot->decap_recovery = TRUE;
        bot_change_state( ch, bot, BOT_TRAINING );
        return;
    }

    /* Skip all gear management during decap recovery — the bot has class gear
     * in inventory from behead() and must not touch newbiepack or class gear
     * until after call all is issued and decap_recovery is cleared. */
    if ( !bot->decap_recovery )
        bot_ensure_geared( ch );

    /* Decrement timers */
    bot->state_timer--;
    if ( bot->cmd_delay > 0 ) { bot->cmd_delay--; return; }  /* Wait before acting */

    /* Reset cmd delay - human-like pause between commands */
    bot->cmd_delay = number_range( 1, 2 );

    /* Drain navigation queue before normal AI */
    if ( bot->nav_n > 0 )
    {
        int j;
#if BOT_DEBUG
        ROOM_INDEX_DATA *before_room = ch->in_room;
        sprintf( log_buf,
            "BOT_NAV %s: pos=%d room=%d cmd='%s' exits[N=%s E=%s S=%s W=%s U=%s D=%s]",
            ch->name, ch->position,
            before_room ? before_room->vnum : -1,
            bot->nav_cmds[0],
            (before_room && before_room->exit[DIR_NORTH] && before_room->exit[DIR_NORTH]->to_room) ? "y" : "n",
            (before_room && before_room->exit[DIR_EAST]  && before_room->exit[DIR_EAST]->to_room)  ? "y" : "n",
            (before_room && before_room->exit[DIR_SOUTH] && before_room->exit[DIR_SOUTH]->to_room) ? "y" : "n",
            (before_room && before_room->exit[DIR_WEST]  && before_room->exit[DIR_WEST]->to_room)  ? "y" : "n",
            (before_room && before_room->exit[DIR_UP]    && before_room->exit[DIR_UP]->to_room)    ? "y" : "n",
            (before_room && before_room->exit[DIR_DOWN]  && before_room->exit[DIR_DOWN]->to_room)  ? "y" : "n"
        );
        log_string( log_buf );
#endif

        if ( ch->position == POS_FIGHTING )
        {
#if BOT_DEBUG
            log_string( "BOT_NAV: blocked - fighting" );
#endif
            bot_watch_msg( ch, "[NAV] blocked -- in combat, holding route\n\r" );
            return;   /* keep queue intact, retry after combat ends */
        }
        if ( ch->position < POS_STANDING )
        {
#if BOT_DEBUG
            log_string( "BOT_NAV: blocked - not standing, issuing stand" );
#endif
            bot_watch_msg( ch, "[NAV] blocked -- not standing, standing up\n\r" );
            bot_cmd( ch, "stand" );
            return;
        }

        bot_cmd( ch, bot->nav_cmds[0] );

#if BOT_DEBUG
        sprintf( log_buf, "BOT_NAV %s: after cmd='%s' now in room=%d",
            ch->name, bot->nav_cmds[0],
            ch->in_room ? ch->in_room->vnum : -1 );
        log_string( log_buf );
#endif

        for ( j = 0; j < bot->nav_n - 1; j++ )
            strcpy( bot->nav_cmds[j], bot->nav_cmds[j+1] );
        bot->nav_n--;

        /* Arrived at grind zone: reset the grind timer and scatter the bot a
         * few random steps so all bots don't crowd the same entrance room. */
        if ( bot->nav_n == 0 && bot->state == BOT_GRINDING )
        {
            bot->state_timer     = number_range( BOT_TIMER_GRINDING_MIN, BOT_TIMER_GRINDING_MAX );
            bot->state_timer_max = bot->state_timer;
            bot->scatter_steps   = number_range( 4, 12 );
            bot->scatter_last_dir = -1;
            bot_watch_msg( ch, "[NAV] arrived at grind zone -- timer reset, scattering\n\r" );
        }
        return;
    }

    /* Continuous WAR MODE check */
    if ( global_bot_pvp_mode == BOT_PVP_MODE_WAR 
      && bot->state != BOT_PVP_HUNT 
      && bot->state != BOT_PVP_FIGHT 
      && bot->state != BOT_RESTING 
      && bot->state != BOT_TRAINING 
      && bot->state != BOT_LOGGING_OUT )
    {
        /* Check randomly to spread CPU load from scanning the MUD */
        if ( number_percent() <= 25 )
        {
            CHAR_DATA *target = bot_find_pvp_target(ch);
            if ( target != NULL )
            {
                char msg[256];
                strncpy(bot->pvp_target, target->name, sizeof(bot->pvp_target) - 1);
                bot->pvp_target[sizeof(bot->pvp_target) - 1] = '\0';
                snprintf(msg, sizeof(msg), "[PVP] WAR MODE ongoing override -> hunting %s\n\r", bot->pvp_target);
                bot_watch_msg( ch, msg );
                bot_change_state( ch, bot, BOT_PVP_HUNT );
                return;
            }
        }
    }

    /* State dispatch */
    switch ( bot->state )
    {
    case BOT_IDLE:        bot_state_idle(       ch, bot ); break;
    case BOT_EXPLORING:   bot_state_exploring(  ch, bot ); break;
    case BOT_GRINDING:    bot_state_grinding(   ch, bot ); break;
    case BOT_TRAINING:    bot_state_training(   ch, bot ); break;
    case BOT_PVP_HUNT:    bot_state_pvp_hunt(   ch, bot ); break;
    case BOT_PVP_FIGHT:   bot_state_pvp_fight(  ch, bot ); break;
    case BOT_RESTING:     bot_state_resting(    ch, bot ); break;
    case BOT_LOGGING_OUT: bot_state_logging_out(ch, bot ); break;
    default:
        bot_change_state( ch, bot, BOT_IDLE );
        break;
    }
}
