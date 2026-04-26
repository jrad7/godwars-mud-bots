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
#include "shapeshifter.h"

/* Class AI vtables - defined in bot_ai_{class}.c, registered below */
extern const BOT_CLASS_AI bot_vamp_ai;
extern const BOT_CLASS_AI bot_monk_ai;
extern const BOT_CLASS_AI bot_ninja_ai;
extern const BOT_CLASS_AI bot_demon_ai;
extern const BOT_CLASS_AI bot_drow_ai;
extern const BOT_CLASS_AI bot_werewolf_ai;
extern const BOT_CLASS_AI bot_mage_ai;
extern const BOT_CLASS_AI bot_tanarri_ai;
extern const BOT_CLASS_AI bot_angel_ai;
extern const BOT_CLASS_AI bot_undead_knight_ai;
extern const BOT_CLASS_AI bot_shapeshifter_ai;
extern const BOT_CLASS_AI bot_droid_ai;
extern const BOT_CLASS_AI bot_samurai_ai;
extern const BOT_CLASS_AI bot_lich_ai;

/*
 * bot_class_ai - vtable table indexed by BOT_CLASS_*
 * Add a row here when registering a new class.
 */
const BOT_CLASS_AI *bot_class_ai[BOT_CLASS_COUNT] = {
    &bot_vamp_ai,           /* BOT_CLASS_VAMPIRE        */
    &bot_monk_ai,           /* BOT_CLASS_MONK           */
    &bot_ninja_ai,          /* BOT_CLASS_NINJA          */
    &bot_demon_ai,          /* BOT_CLASS_DEMON          */
    &bot_drow_ai,           /* BOT_CLASS_DROW           */
    &bot_werewolf_ai,       /* BOT_CLASS_WEREWOLF       */
    &bot_mage_ai,           /* BOT_CLASS_MAGE           */
    &bot_tanarri_ai,        /* BOT_CLASS_TANARRI        */
    &bot_angel_ai,          /* BOT_CLASS_ANGEL          */
    &bot_undead_knight_ai,  /* BOT_CLASS_UNDEAD_KNIGHT  */
    &bot_shapeshifter_ai,       /* BOT_CLASS_SHAPESHIFTER   */
    &bot_droid_ai,              /* BOT_CLASS_DROID          */
    &bot_samurai_ai,            /* BOT_CLASS_SAMURAI        */
    &bot_lich_ai                /* BOT_CLASS_LICH           */
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

static bool bot_generic_buff_check( CHAR_DATA *ch );
static const char *bot_state_str( bot_state_t state );

/* -----------------------------------------------------------------------
 * bot_do_recall
 * Single entry point for any bot recall attempt.  If the bot is cursed
 * and knows "remove curse", casts it this tick and returns FALSE so the
 * caller skips post-recall work (e.g. state changes) until next tick.
 * If not cursed (or unable to cure), issues the recall command.  After
 * recalling, verifies the bot landed in its home room (3001); if not,
 * logs a bug and retries once.  Returns TRUE when the recall was
 * attempted (whether or not it succeeded), FALSE when decursing.
 * ----------------------------------------------------------------------- */
static bool bot_do_recall( CHAR_DATA *ch )
{
    if ( IS_HEAD(ch, LOST_HEAD) )
        return FALSE;

    if ( ch->position < POS_FIGHTING && ch->position >= POS_SLEEPING )
        bot_cmd( ch, "stand" );

    if ( IS_AFFECTED(ch, AFF_CURSE) )
    {
        bot_cmd( ch, "cast 'remove curse' self" );
        return FALSE;   /* curing curse this tick; recall next tick */
    }

    bot_cmd( ch, "recall" );

    if ( ch->in_room == NULL || ch->in_room->vnum != ch->home )
    {
        char logbuf[256];
        snprintf( logbuf, sizeof(logbuf),
            "[RECALL] failed to reach home (%d) -- still in room %d -- retrying",
            ch->home, ch->in_room ? ch->in_room->vnum : -1 );
        do_bug( ch, logbuf );
        bot_watch_msg( ch, logbuf );
        bot_watch_msg( ch, "\n\r" );
        bot_cmd( ch, "recall" );
    }

    return TRUE;
}

/* -----------------------------------------------------------------------
 * bot_cmd - inject a command into a bot as if it typed it
 * ----------------------------------------------------------------------- */

void bot_cmd( CHAR_DATA *ch, const char *cmd )
{
    char buf[MAX_INPUT_LENGTH];
    strncpy( buf, cmd, sizeof(buf)-1 );
    buf[sizeof(buf)-1] = '\0';

    /* Stuck detection: track last 10 commands in a ring buffer */
    if ( ch->pcdata != NULL && ch->pcdata->botdata != NULL )
    {
        BOT_DATA *bot = ch->pcdata->botdata;
        int slot = bot->cmd_history_head;

        strncpy( bot->cmd_history[slot], buf, sizeof(bot->cmd_history[0])-1 );
        bot->cmd_history[slot][sizeof(bot->cmd_history[0])-1] = '\0';
        bot->cmd_history_head = (slot + 1) % 10;
        if ( bot->cmd_history_count < 10 )
            bot->cmd_history_count++;

        if ( bot->cmd_history_count >= 10 && !bot->spell_training && bot->nav_n == 0
          && ch->fighting == NULL )
        {
            bool stuck = TRUE;
            int i;
            for ( i = 0; i < 10; i++ )
            {
                if ( strcmp( bot->cmd_history[i], bot->cmd_history[0] ) != 0 )
                {
                    stuck = FALSE;
                    break;
                }
            }
            /* "chant heal" is legitimately repeated by liches (no natural
             * HP regen) -- don't treat it as being stuck. */
            if ( stuck && !str_cmp( bot->cmd_history[0], "chant heal" ) )
                stuck = FALSE;

            if ( stuck )
            {
                char logbuf[512];
                const char *area_name = ( ch->in_room && ch->in_room->area )
                                        ? ch->in_room->area->name : "unknown area";
                const char *room_name = ch->in_room ? ch->in_room->name : "unknown room";
                int         vnum      = ch->in_room ? ch->in_room->vnum : -1;

                snprintf( logbuf, sizeof(logbuf),
                    "[STUCK] stuck on '%s' -- state: %s -- room: %s (%d) in %s -- recalling",
                    buf, bot_state_str(bot->state), room_name, vnum, area_name );
                do_bug( ch, logbuf );
                strncat( logbuf, "\n\r", sizeof(logbuf) - strlen(logbuf) - 1 );
                bot_watch_msg( ch, logbuf );

                /* Clear history so we don't spam recalls */
                bot->cmd_history_count = 0;
                bot->cmd_history_head  = 0;
                if ( bot_do_recall(ch) )
                    bot_change_state( ch, bot, BOT_IDLE );
                return;
            }
        }
    }

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

static const char *bot_class_str( CHAR_DATA *ch )
{
    if ( IS_CLASS(ch, CLASS_ANGEL) )        return "angel";
    if ( IS_CLASS(ch, CLASS_TANARRI) )      return "tanarri";
    if ( IS_CLASS(ch, CLASS_UNDEAD_KNIGHT)) return "undead_knight";
    if ( IS_CLASS(ch, CLASS_DROID) )        return "droid";
    if ( IS_CLASS(ch, CLASS_LICH) )         return "lich";
    if ( IS_CLASS(ch, CLASS_SHAPESHIFTER) ) return "shapeshifter";
    if ( IS_CLASS(ch, CLASS_NINJA) )        return "ninja";
    if ( IS_CLASS(ch, CLASS_MONK) )         return "monk";
    if ( IS_CLASS(ch, CLASS_DROW) )         return "drow";
    if ( IS_CLASS(ch, CLASS_SAMURAI) )      return "samurai";
    if ( IS_CLASS(ch, CLASS_VAMPIRE) )      return "vampire";
    if ( IS_CLASS(ch, CLASS_WEREWOLF) )     return "werewolf";
    if ( IS_CLASS(ch, CLASS_MAGE) )         return "mage";
    if ( IS_CLASS(ch, CLASS_DEMON) )        return "demon";
    return "mortal";
}

static const char *bot_state_str( bot_state_t state )
{
    switch ( state )
    {
    case BOT_IDLE:        return "IDLE";
    case BOT_EXPLORING:   return "EXPLORING";
    case BOT_GRINDING:    return "GRINDING";
    case BOT_TRAINING:    return "TRAINING";
    case BOT_PVP_HUNT:    return "PVP_HUNT";
    case BOT_PVP_FIGHT:   return "PVP_FIGHT";
    case BOT_SHOPPING:    return "SHOPPING";
    case BOT_RESTING:     return "RESTING";
    case BOT_LOGGING_OUT: return "LOGGING_OUT";
    case BOT_PVP_FLEE:    return "PVP_FLEE";
    default:              return "UNKNOWN";
    }
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
    /* Mud School entrance (3700): down leads back to Temple of Midgaard (D5->3001) */
    { 3700, 3700, DIRMASK(DIR_DOWN) },
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
    /* Sewer entrance (7030): up exits back to The Dump in Midgaard (3030) */
    { 7030, 7030, DIRMASK(DIR_UP) },
    /* Moria entrance (3900): south exits back to west gate of Midgaard (3052) */
    { 3900, 3900, DIRMASK(DIR_SOUTH) },
    /* Plains entrance (300): south exits back into Moria (3904) */
    { 300, 300, DIRMASK(DIR_SOUTH) },
    /* Thalos entrance (5200): east exits back toward Midennir (5256) */
    { 5200, 5200, DIRMASK(DIR_EAST) },
    /* Daycare entrance (6601): north exits back toward Midgaard */
    { 6601, 6601, DIRMASK(DIR_NORTH) },
    /* Mega City entrance (8001): north exits back toward Midgaard */
    { 8001, 8001, DIRMASK(DIR_NORTH) },
    /* Galaxy entrance (9301): up exits to Midgaard, west to Hitower */
    { 9301, 9301, DIRMASK(DIR_UP) | DIRMASK(DIR_WEST) },
    /* Air entrance (1017): down exits back to Midgaard (3057) */
    { 1017, 1017, DIRMASK(DIR_DOWN) },
    /* Drow entrance (5100): up exits back toward Thalos */
    { 5100, 5100, DIRMASK(DIR_UP) },
    /* Disney entrance (50000): down exits back to Midgaard (3005) */
    { 50000, 50000, DIRMASK(DIR_DOWN) },

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
static void bot_navigate_to_any_grind_zone( BOT_DATA *bot, CHAR_DATA *ch );
static void bot_handle_pvp_attack( CHAR_DATA *ch, BOT_DATA *bot, CHAR_DATA *attacker );

/* -----------------------------------------------------------------------
 * bot_change_state - transition to a new AI state
 * ----------------------------------------------------------------------- */

void bot_change_state( CHAR_DATA *ch, BOT_DATA *bot, bot_state_t new_state )
{
    static const char *state_names[] = {
        "IDLE", "EXPLORING", "GRINDING", "TRAINING",
        "PVP_HUNT", "PVP_FIGHT", "SHOPPING", "RESTING", "LOGGING_OUT", "PVP_FLEE"
    };
    #define BOT_STATE_MAX BOT_PVP_FLEE

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
                bot->state <= BOT_STATE_MAX ? state_names[bot->state] : "UNKNOWN",
                new_state <= BOT_STATE_MAX ? state_names[new_state] : "UNKNOWN" );
            log_string( buf );
        }
#endif

        /* Notify any watcher via watchbot */
        if ( ch->desc != NULL && ch->desc->snoop_by != NULL )
        {
            char echo[256];
            snprintf( echo, sizeof(echo), "[STATE] %s: %s --> %s\n\r",
                ch->name,
                (int)bot->state <= (int)BOT_STATE_MAX ? state_names[bot->state] : "UNKNOWN",
                (int)new_state  <= (int)BOT_STATE_MAX ? state_names[new_state]  : "UNKNOWN" );
            write_to_buffer( ch->desc->snoop_by, echo, 0 );
        }
    }

    bot->state            = new_state;
    bot->cmd_delay        = number_range( 1, 2 );
    bot->cmd_history_head  = 0;
    bot->cmd_history_count = 0;

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
        bot->state_timer     = number_range( BOT_TIMER_RESTING_MIN, BOT_TIMER_RESTING_MAX );
        bot->needs_meditate  = ( IS_CLASS(ch, CLASS_MAGE) || IS_CLASS(ch, CLASS_MONK)
                               || IS_CLASS(ch, CLASS_DROW)
                               || IS_CLASS(ch, CLASS_LICH) || IS_CLASS(ch, CLASS_ANGEL)
                               || IS_CLASS(ch, CLASS_TANARRI) );
        bot->ready_meditate  = FALSE;
        break;
    case BOT_LOGGING_OUT:
        bot->state_timer = number_range( BOT_TIMER_LOGOUT_MIN, BOT_TIMER_LOGOUT_MAX );
        break;
    case BOT_PVP_FLEE:
        /* No fixed timer -- state exits when fight_timer reaches 0 */
        bot->state_timer = 9999;
        break;
    default:
        bot->state_timer = number_range( BOT_TIMER_DEFAULT_MIN, BOT_TIMER_DEFAULT_MAX );
        break;
    }
    bot->state_timer_max = bot->state_timer;
}


/* Maps each route pointer to a human-readable zone name for watchbot output */
static const struct { const char **route; const char *name; const char *filename; } route_names[] = {
    { zone_mud_school,  "mud_school",  "school.are" },
    { zone_jobo_heaven, "jobo_heaven", "heaven.are" },
    { zone_smurf,       "smurf",       "smurf.are"  },
    { zone_jobo_hell,   "jobo_hell",   "hell.are"   },
    { zone_canyon,      "canyon",      "canyon.are" },
    { zone_shire,       "shire",       "shire.are"  },
    { zone_weed,        "weed",        "weed.are"   },
    { zone_sewer,       "sewer",       "sewer.are"  },
    { zone_moria,       "moria",       "moria.are"  },
    { zone_plains,      "plains",      "plains.are" },
    { zone_thalos,      "thalos",      "thalos.are" },
    { zone_daycare,     "daycare",     "daycare.are" },
    { zone_mega,        "mega",        "mega1.are"  },
    { zone_galaxy,      "galaxy",      "galaxy.are" },
    { zone_air,         "air",         "air.are"    },
    { zone_drow,        "drow",        "drow.are"   },
    { zone_disney,      "disney",      "disney.are" },
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
 * bot_navigate_to_any_grind_zone
 *
 * Like bot_navigate_to_grind_zone but ignores tier limits and picks from
 * every zone in the table, skipping the zone stored in bot->pvp_flee_zone.
 * Used by the flee state so bots hide somewhere far from where they were
 * attacked -- mobs are not aggressive so any zone is equally safe.
 * ----------------------------------------------------------------------- */
static void bot_navigate_to_any_grind_zone( BOT_DATA *bot, CHAR_DATA *ch )
{
    const char **candidates[32];
    int          count = 0;
    int          tier_i, route_i, rn;

    for ( tier_i = 0; tier_i < GRIND_TIER_COUNT && count < 32; tier_i++ )
    {
        const GRIND_TIER *tier = &grind_tiers[tier_i];
        for ( route_i = 0; route_i < tier->num_routes && count < 32; route_i++ )
        {
            const char **route = tier->routes[route_i];
            if ( route == NULL ) continue;

            /* Skip the zone we fled from */
            if ( bot->pvp_flee_zone[0] != '\0' )
            {
                bool is_flee_zone = FALSE;
                for ( rn = 0; route_names[rn].route != NULL; rn++ )
                {
                    if ( route_names[rn].route == route
                      && route_names[rn].filename != NULL
                      && !str_cmp( route_names[rn].filename, bot->pvp_flee_zone ) )
                    {
                        is_flee_zone = TRUE;
                        break;
                    }
                }
                if ( is_flee_zone ) continue;
            }

            candidates[count++] = route;
        }
    }

    if ( count == 0 )
    {
        /* Absolute fallback: just recall to the altar */
        bot_do_recall( ch );
        bot_watch_msg( ch, "[PVP_FLEE] No alternate zone found -- recalling.\n\r" );
        return;
    }

    const char **route = candidates[number_range( 0, count - 1 )];
    const char *zone_name = "unknown";

    for ( rn = 0; route_names[rn].route != NULL; rn++ )
    {
        if ( route_names[rn].route == route )
        {
            zone_name = route_names[rn].name;
            break;
        }
    }

    bot->nav_n = 0;
    for ( ; *route != NULL; route++ )
        bot_nav_queue( bot, *route );

    char msg[128];
    snprintf( msg, sizeof(msg), "[PVP_FLEE] Hiding in %s (%d steps)\n\r", zone_name, bot->nav_n );
    bot_watch_msg( ch, msg );
}

/* -----------------------------------------------------------------------
 * PvP grudge / nemesis / blacklist helpers
 * ----------------------------------------------------------------------- */

/* Recalculate bot->nemesis from the current grudge list (highest attack_count). */
static void bot_pvp_update_nemesis( BOT_DATA *bot )
{
    int i, best;
    if ( bot->grudge_count == 0 )
    {
        bot->nemesis[0] = '\0';
        return;
    }
    best = 0;
    for ( i = 1; i < bot->grudge_count; i++ )
        if ( bot->grudge_list[i].attack_count > bot->grudge_list[best].attack_count )
            best = i;
    strncpy( bot->nemesis, bot->grudge_list[best].name, sizeof(bot->nemesis)-1 );
    bot->nemesis[sizeof(bot->nemesis)-1] = '\0';
}

/* Add attacker to grudge list (or increment their count) and refresh nemesis. */
static void bot_pvp_add_grudge( BOT_DATA *bot, const char *name )
{
    int i;
    char msg[128];
    for ( i = 0; i < bot->grudge_count; i++ )
    {
        if ( !str_cmp( bot->grudge_list[i].name, name ) )
        {
            bot->grudge_list[i].attack_count++;
            bot_pvp_update_nemesis( bot );
            snprintf( msg, sizeof(msg), "[GRUDGE] %s attack count now %d\n\r",
                      name, bot->grudge_list[i].attack_count );
            /* bot_watch_msg needs a char*, set later via the calling function */
            return;
        }
    }
    if ( bot->grudge_count >= BOT_PVP_GRUDGE_MAX ) return;
    strncpy( bot->grudge_list[bot->grudge_count].name, name,
             sizeof(bot->grudge_list[0].name)-1 );
    bot->grudge_list[bot->grudge_count].name[sizeof(bot->grudge_list[0].name)-1] = '\0';
    bot->grudge_list[bot->grudge_count].attack_count = 1;
    bot->grudge_count++;
    bot_pvp_update_nemesis( bot );
}

/* Remove a name from the grudge list (revenge achieved) and refresh nemesis. */
static void bot_pvp_remove_grudge( BOT_DATA *bot, const char *name )
{
    int i, j;
    for ( i = 0; i < bot->grudge_count; i++ )
    {
        if ( !str_cmp( bot->grudge_list[i].name, name ) )
        {
            for ( j = i; j < bot->grudge_count - 1; j++ )
                bot->grudge_list[j] = bot->grudge_list[j+1];
            bot->grudge_count--;
            bot_pvp_update_nemesis( bot );
            return;
        }
    }
}

/* Add a name to the blacklist (no duplicates). */
static void bot_pvp_add_blacklist( BOT_DATA *bot, const char *name )
{
    int i;
    if ( name[0] == '\0' ) return;
    for ( i = 0; i < bot->blacklist_count; i++ )
        if ( !str_cmp( bot->blacklist[i], name ) ) return;
    if ( bot->blacklist_count >= BOT_PVP_BLACKLIST_MAX ) return;
    strncpy( bot->blacklist[bot->blacklist_count], name, sizeof(bot->blacklist[0])-1 );
    bot->blacklist[bot->blacklist_count][sizeof(bot->blacklist[0])-1] = '\0';
    bot->blacklist_count++;
}

/* Return TRUE if name is on the blacklist. */
static bool bot_pvp_is_blacklisted( BOT_DATA *bot, const char *name )
{
    int i;
    for ( i = 0; i < bot->blacklist_count; i++ )
        if ( !str_cmp( bot->blacklist[i], name ) ) return TRUE;
    return FALSE;
}

/* Return TRUE if name is on the grudge list. */
static bool bot_pvp_is_grudge( BOT_DATA *bot, const char *name )
{
    int i;
    for ( i = 0; i < bot->grudge_count; i++ )
        if ( !str_cmp( bot->grudge_list[i].name, name ) ) return TRUE;
    return FALSE;
}

/* -----------------------------------------------------------------------
 * bot_handle_pvp_attack
 *
 * Called the first time we detect a player/bot attacking us.  Decides
 * whether to fight back (aggression roll) or run (BOT_PVP_FLEE).
 * ----------------------------------------------------------------------- */
static void bot_handle_pvp_attack( CHAR_DATA *ch, BOT_DATA *bot, CHAR_DATA *attacker )
{
    char msg[256];

    /* Remember where we were attacked so we won't hide there */
    if ( ch->in_room && ch->in_room->area && ch->in_room->area->filename )
    {
        strncpy( bot->pvp_flee_zone, ch->in_room->area->filename,
                 sizeof(bot->pvp_flee_zone) - 1 );
        bot->pvp_flee_zone[sizeof(bot->pvp_flee_zone)-1] = '\0';
    }

    snprintf( msg, sizeof(msg), "[PVP] Attacked by %s!\n\r", attacker->name );
    bot_watch_msg( ch, msg );

    /* Track this attacker in the grudge list; update nemesis */
    bot_pvp_add_grudge( bot, attacker->name );
    if ( bot->nemesis[0] != '\0' )
    {
        snprintf( msg, sizeof(msg), "[GRUDGE] Nemesis is now: %s\n\r", bot->nemesis );
        bot_watch_msg( ch, msg );
    }

    /* Fight-back decision: aggression rating ± 20% chaos */
    int chance = bot->roster ? bot->roster->aggression : 30;
    chance += number_range( -20, 20 );
    chance = UMAX( 0, UMIN( 100, chance ) );

    if ( number_percent() < chance && fair_fight( ch, attacker ) )
    {
        /* Fight back -- treat attacker as the PvP target.
         * NOT bot-initiated: don't blacklist on loss. */
        strncpy( bot->pvp_target, attacker->name, sizeof(bot->pvp_target) - 1 );
        bot->pvp_target[sizeof(bot->pvp_target)-1] = '\0';
        bot->pvp_bot_initiated = FALSE;
        snprintf( msg, sizeof(msg), "[PVP] Fighting back vs %s! (chance=%d)\n\r",
                  bot->pvp_target, chance );
        bot_watch_msg( ch, msg );
        bot_change_state( ch, bot, BOT_PVP_FIGHT );
    }
    else
    {
        /* Flee -- record attacker so we can detect if they follow */
        strncpy( bot->pvp_attacker, attacker->name, sizeof(bot->pvp_attacker) - 1 );
        bot->pvp_attacker[sizeof(bot->pvp_attacker)-1] = '\0';
        snprintf( msg, sizeof(msg), "[PVP] Fleeing from %s! (chance=%d)\n\r",
                  bot->pvp_attacker, chance );
        bot_watch_msg( ch, msg );
        bot_change_state( ch, bot, BOT_PVP_FLEE );
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
    case BOT_CLASS_MAGE:      return "mage";
    case BOT_CLASS_TANARRI:        return "tanarri";
    case BOT_CLASS_ANGEL:          return "angel";
    case BOT_CLASS_UNDEAD_KNIGHT:  return "undead_knight";
    case BOT_CLASS_SHAPESHIFTER:  return "shapeshifter";
    case BOT_CLASS_DROID:  return "droid";
    case BOT_CLASS_SAMURAI: return "samurai";
    case BOT_CLASS_LICH:    return "lich";
    default:
        bug( "bot_class_name: unknown class_pref %d", class_pref );
        return NULL;
    }
}

/*
 * bot_primal_target - how much primal this bot needs to afford one class gear piece.
 * Special upgrade classes have their gear cost at 150 primal/piece.
 * All other classed bots cost 60 primal/piece.
 * Returns 0 if the bot has no class yet (unclassed bots don't need primal).
 */
int bot_primal_target( CHAR_DATA *ch )
{
    BOT_DATA *bot = ch->pcdata->botdata;
    if ( ch->class == 0 || bot == NULL || bot->roster == NULL )
        return 0;
    /* Katana costs 250 primal per piece */
    if ( bot->roster->class_pref == BOT_CLASS_SAMURAI )
        return 250;
    /* UK trains its power tracks with primal; costs can reach 600 (level 9->10).
     * Return the actual next step cost so the bot accumulates enough. */
    if ( bot->roster->class_pref == BOT_CLASS_UNDEAD_KNIGHT )
    {
        int class_cost = bot_uk_primal_needed( ch );
        return class_cost > 150 ? class_cost : 150;
    }
    /* Shapeshifter formlearn costs reach 400 (level 4->5); same treatment. */
    if ( bot->roster->class_pref == BOT_CLASS_SHAPESHIFTER )
    {
        int class_cost = bot_ss_primal_needed( ch );
        return class_cost > 150 ? class_cost : 150;
    }
    /* Mage invoke costs reach 200 (level 9->10); accumulate enough. */
    if ( bot->roster->class_pref == BOT_CLASS_MAGE )
    {
        int class_cost = bot_mage_primal_needed( ch );
        return class_cost > 150 ? class_cost : 150;
    }
    /* Monk mantra costs reach 140 (level 13->14); accumulate enough. */
    if ( bot->roster->class_pref == BOT_CLASS_MONK )
    {
        int class_cost = bot_monk_primal_needed( ch );
        return class_cost > 60 ? class_cost : 60;
    }
    if ( bot->roster->class_pref == BOT_CLASS_TANARRI
      || bot->roster->class_pref == BOT_CLASS_ANGEL
      || bot->roster->class_pref == BOT_CLASS_DROID
      || bot->roster->class_pref == BOT_CLASS_LICH )
        return 150;
    return 60;
}

/*
 * bot_should_train_primal - TRUE if bot needs more primal and can afford one point.
 * Cost for the next primal point is (current_practice + 1) * 500 exp.
 */
bool bot_should_train_primal( CHAR_DATA *ch )
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
        "stone", "sanctuary", "fly", "pass door", "shield", "armor", "bless", "frenzy", NULL
    };
    int i;
    int sn;

    /* Out of combat generic spellcasting/buffing */
    if ( ch->position == POS_FIGHTING )
        return FALSE;

    bool needs_buff = FALSE;
    char cmd[64];

    cmd[0] = '\0';

    if ( IS_AFFECTED(ch, AFF_CURSE) )
    {
        sn = skill_lookup("remove curse");
        if ( sn > 0 && ch->pcdata->learned[sn] > 0 )
        {
            needs_buff = TRUE;
            sprintf(cmd, "cast rem self");
        }
    }

    if ( !needs_buff && bot_needs_repair(ch) )
    {
        sn = skill_lookup("repair");
        if ( sn > 0 && ch->pcdata->learned[sn] > 0 )
        {
            needs_buff = TRUE;
            sprintf(cmd, "cast repair");
        }
    }

    if ( !needs_buff )
    {
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
                if ( !strcmp(buffs[i], "fly") && IS_AFFECTED(ch, AFF_FLYING) )
                    continue;
                if ( !strcmp(buffs[i], "pass door") && IS_AFFECTED(ch, AFF_PASS_DOOR) )
                    continue;
                
                needs_buff = TRUE;
                if ( !strcmp(buffs[i], "pass door") )
                    sprintf(cmd, "cast \"pass door\"");
                else
                    sprintf(cmd, "cast %s", buffs[i]);
                break;
            }
        }
    }

    if ( !needs_buff )
        return FALSE;

    /* Shapeshifters in animal form cannot cast spells.
     * Revert to human form first. */
    if ( IS_CLASS(ch, CLASS_SHAPESHIFTER)
      && ch->pcdata != NULL
      && ch->pcdata->powers[SHAPE_FORM] != 0 )
    {
        bot_cmd( ch, "shift human" );
        return TRUE;
    }

    bot_cmd(ch, cmd);
    return TRUE;

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Upgrade-path helpers
 * ----------------------------------------------------------------------- */

/* TRUE if bot can still pursue an upgrade — either the first upgrade (base
 * class -> advanced class) or upgrade2 (same advanced class, higher
 * upgrade_level, capped at BOT_UPGRADE_LEVEL_MAX). */
static bool bot_can_upgrade( CHAR_DATA *ch )
{
    if ( !BOT_UPGRADES_ENABLED ) return FALSE;
    if ( IS_NPC(ch) || ch->pcdata == NULL ) return FALSE;
    if ( ch->class == 0 ) return FALSE;   /* no class chosen yet */
    if ( is_upgrade(ch) )
        return ( ch->pcdata->upgrade_level < BOT_UPGRADE_LEVEL_MAX );
    return ( IS_CLASS(ch, CLASS_VAMPIRE)
          || IS_CLASS(ch, CLASS_MONK)
          || IS_CLASS(ch, CLASS_NINJA)
          || IS_CLASS(ch, CLASS_DEMON)
          || IS_CLASS(ch, CLASS_DROW)
          || IS_CLASS(ch, CLASS_WEREWOLF)
          || IS_CLASS(ch, CLASS_MAGE) );
}

/* Current effective hp/mana/move/qp/pkscore needs for whichever upgrade the
 * bot is chasing.  First upgrade mirrors do_upgrade's pkscore stat penalty
 * (each 100-point gap below 1000 adds 3000 to each stat).  upgrade2 scales
 * requirements by upgrade_level and has no stat penalty — pkscore is a hard
 * gate. */
static void bot_upgrade_stat_needs( CHAR_DATA *ch,
    int *hp_need, int *mana_need, int *move_need,
    int *qp_need, int *pkscore_need )
{
    int hp, mana, move, qp, pk;

    if ( is_upgrade(ch) )
    {
        int lvl = ch->pcdata->upgrade_level;
        hp   = 80000 + lvl * 10000;
        mana = 80000 + lvl * 10000;
        move = 80000 + lvl * 10000;
        qp   = (lvl + 1) * 40000;
        pk   = 2000 + lvl * 500;
    }
    else
    {
        int ratio = get_ratio(ch);
        hp   = BOT_UPGRADE_HP;
        mana = BOT_UPGRADE_MANA;
        move = BOT_UPGRADE_MOVE;
        qp   = BOT_UPGRADE_QP;
        pk   = BOT_UPGRADE_PKSCORE;
        if ( ratio < BOT_UPGRADE_PKSCORE )
        {
            int gap = (BOT_UPGRADE_PKSCORE - UMAX(ratio, 0)) / 100;
            hp   += 3000 * gap;
            mana += 3000 * gap;
            move += 3000 * gap;
        }
    }

    if ( hp_need      ) *hp_need      = hp;
    if ( mana_need    ) *mana_need    = mana;
    if ( move_need    ) *move_need    = move;
    if ( qp_need      ) *qp_need      = qp;
    if ( pkscore_need ) *pkscore_need = pk;
}

/* TRUE once base stat targets are met — bot is actively seeking remaining
 * upgrade requirements (QP/pkscore, and gen 1 for the first upgrade) through
 * PvP.  Uses the unpenalised baseline so we enter the hunt as soon as the
 * raw targets are hit; pkscore-scaled overflow is handled by bot_upgrade_ready. */
static bool bot_in_upgrade_hunt( CHAR_DATA *ch )
{
    if ( !bot_can_upgrade(ch) ) return FALSE;
    if ( is_upgrade(ch) )
    {
        int lvl = ch->pcdata->upgrade_level;
        int target = 80000 + lvl * 10000;
        return ( ch->max_hit  >= target
              && ch->max_mana >= target
              && ch->max_move >= target );
    }
    return ( ch->max_hit  >= BOT_UPGRADE_HP
          && ch->max_mana >= BOT_UPGRADE_MANA
          && ch->max_move >= BOT_UPGRADE_MOVE );
}

/* TRUE when every upgrade requirement is satisfied, including any pkscore
 * penalty on hp/mana/move (first upgrade) or the superstance and pkscore hard
 * gates (upgrade2). */
static bool bot_upgrade_ready( CHAR_DATA *ch )
{
    int hp_need, mana_need, move_need, qp_need, pk_need;
    if ( !bot_in_upgrade_hunt(ch) )                     return FALSE;
    bot_upgrade_stat_needs( ch, &hp_need, &mana_need, &move_need, &qp_need, &pk_need );
    if ( ch->pcdata->quest < qp_need )                  return FALSE;
    if ( ch->max_hit  < hp_need  )                      return FALSE;
    if ( ch->max_mana < mana_need )                     return FALSE;
    if ( ch->max_move < move_need )                     return FALSE;
    if ( get_ratio(ch) < pk_need )                      return FALSE;
    if ( is_upgrade(ch) )
    {
        int i;
        for ( i = 19; i <= 23; i++ )
            if ( ch->stance[i] == -1 ) return FALSE;
    }
    else
    {
        if ( ch->generation != 1 )                      return FALSE;
    }
    return TRUE;
}

/* Map old base class -> BOT_CLASS_* for the post-upgrade class. */
static int bot_upgrade_class_pref( int old_class )
{
    switch ( old_class )
    {
    case CLASS_VAMPIRE:   return BOT_CLASS_UNDEAD_KNIGHT;
    case CLASS_MONK:      return BOT_CLASS_ANGEL;
    case CLASS_NINJA:     return BOT_CLASS_SAMURAI;
    case CLASS_DEMON:     return BOT_CLASS_TANARRI;
    case CLASS_DROW:      return BOT_CLASS_DROID;
    case CLASS_WEREWOLF:  return BOT_CLASS_SHAPESHIFTER;
    case CLASS_MAGE:      return BOT_CLASS_LICH;
    default:              return -1;
    }
}

/*
 * Returns TRUE if the bot has exp worth spending on stats or class rank.
 */
static bool bot_should_train( CHAR_DATA *ch )
{
    int hp_cap = UMIN( 120000, 20000 + 4000 * ch->pkill );

    if ( ch->level == 2 && ch->max_hit >= 2000 )               return TRUE;
    if ( ch->level == 3 && ch->class == 0 )                     return TRUE;

    /* Upgrade ready: enter training state to navigate to the altar */
    if ( bot_upgrade_ready(ch) ) return TRUE;

    /* Cheap generation upgrade: costs only 10M exp at gen 6 or higher. */
    if ( ch->generation >= 6 && ch->exp >= 10000000 )
        return TRUE;

    /* Primal for class gear takes priority over hp/mana/move spending */
    if ( bot_should_train_primal(ch) )                           return TRUE;

    /* Check if we need to practice generic spells */
    {
        static const char *practice_spells[] = {
            "repair", "rem", "stone", "sanctuary", "fly", "pass door", "shield", "armor", "bless", "frenzy",
            "cure blindness", "faerie fog", NULL
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
            /* SS1: pool immediately once basics are maxed */
            if ( ch->stance[19] == -1 )
            {
                if ( ch->exp >= 80000000 ) return TRUE; else return FALSE;
            }
            /* SS2-SS5: train HP to a threshold first, then pool until ready to buy.
             * If below the HP threshold, fall through to HP training below. */
            else if ( ch->stance[19] != -1 && ch->stance[13] >= 200 && ch->stance[20] == -1 )
            {
                if ( ch->max_hit >= 20000 || ch->max_hit >= hp_cap ) { if ( ch->exp >= 120000000 ) return TRUE; else return FALSE; }
                /* else fall through to HP training */
            }
            else if ( ch->stance[20] != -1 && ch->stance[14] >= 200 && ch->stance[21] == -1 )
            {
                if ( ch->max_hit >= 22000 || ch->max_hit >= hp_cap ) { if ( ch->exp >= 140000000 ) return TRUE; else return FALSE; }
                /* else fall through */
            }
            else if ( ch->stance[21] != -1 && ch->stance[15] >= 200 && ch->stance[22] == -1 )
            {
                if ( ch->max_hit >= 24000 || ch->max_hit >= hp_cap ) { if ( ch->exp >= 200000000 ) return TRUE; else return FALSE; }
                /* else fall through */
            }
            else if ( ch->stance[22] != -1 && ch->stance[16] >= 200 && ch->stance[23] == -1 )
            {
                if ( ch->max_hit >= 26000 || ch->max_hit >= hp_cap ) { if ( ch->exp >= 380000000 ) return TRUE; else return FALSE; }
                /* else fall through */
            }
            else
            {
                /* Basic maxed but waiting for current SS mastery to hit 200 — pool */
                return FALSE;
            }
        }
    }

    /* Bots pursuing an upgrade (first upgrade or upgrade2): train to the current
     * stat targets, then pool for gen steps (first upgrade only).  Falls through
     * to the post-upgrade branch when upgrade_level is maxed. */
    if ( bot_can_upgrade(ch) )
    {
        int hp_need, mana_need, move_need, qp_need, pk_need;
        bot_upgrade_stat_needs( ch, &hp_need, &mana_need, &move_need, &qp_need, &pk_need );

        if ( ch->max_hit  < hp_need   && ch->max_hit  < hp_cap && ch->exp >= ch->max_hit  + 1 ) return TRUE;
        if ( ch->max_mana < mana_need && ch->max_mana < hp_cap && ch->exp >= ch->max_mana + 1 ) return TRUE;
        if ( ch->max_move < move_need && ch->max_move < hp_cap && ch->exp >= ch->max_move + 1 ) return TRUE;

        /* First upgrade only: pool for gen 5->4->3->2; gen 1 via PvP gensteal. */
        if ( !is_upgrade(ch) && ch->generation >= 3 )
        {
            int gencost;
            if      ( ch->generation == 3 ) gencost = 400000000;
            else if ( ch->generation == 4 ) gencost = 200000000;
            else if ( ch->generation == 5 ) gencost =  50000000;
            else                            gencost =  10000000;
            if ( ch->exp >= gencost ) return TRUE;
        }
        /* First upgrade: stats+QP met but pkscore below threshold — dump excess
         * exp into hp/mana/move up to hp_cap while PvPing for pkscore. */
        if ( !is_upgrade(ch)
          && ch->generation == 1
          && ch->pcdata->quest >= qp_need
          && get_ratio(ch) < pk_need )
        {
            if ( ch->max_hit  < hp_cap && ch->exp >= ch->max_hit  + 1 ) return TRUE;
            if ( ch->max_mana < hp_cap && ch->exp >= ch->max_mana + 1 ) return TRUE;
            if ( ch->max_move < hp_cap && ch->exp >= ch->max_move + 1 ) return TRUE;
        }
        return FALSE;  /* pool — grind/PvP for QP/pkscore/gen */
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

        /* Battlemage prereqs: all 5 spell colors >= 100 and max_mana >= 5000.
         * Let the mage do_train grind them; it returns TRUE while still working
         * and FALSE when all prereqs are satisfied so we fall through to selfclass. */
        if ( pref == BOT_CLASS_MAGE )
        {
            const BOT_CLASS_AI *ai = bot_class_ai[BOT_CLASS_MAGE];
            if ( ai && ai->do_train && ai->do_train(ch) )
                return TRUE;
            /* fall through to selfclass — game checks max_mana >= 5000 */
        }

        const char *cname = bot_class_name(pref);
        if ( cname == NULL ) return FALSE;
        char cmd[64];
        sprintf( cmd, "selfclass %s", cname );
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

    /* Step 2.9: execute upgrade when all requirements are met.
     * Altar (3054) is one step north of the recall room (3001). */
    if ( bot_upgrade_ready(ch) )
    {
        if ( ch->in_room == NULL || ch->in_room->vnum != 3054 )
        {
            /* Navigate: recall to 3001, then north to 3054 */
            if ( bot_do_recall(ch) && ch->in_room && ch->in_room->vnum == ch->home )
                bot_cmd( ch, "north" );
        }
        else
        {
            /* At the altar — upgrade! */
            int  old_class         = ch->class;
            bool was_upgrade        = is_upgrade(ch);
            int  old_upgrade_level  = ch->pcdata->upgrade_level;
            bot_cmd( ch, "upgrade" );

            bool did_first_upgrade = ( !was_upgrade && is_upgrade(ch) );
            bool did_upgrade2      = ( was_upgrade
                                    && ch->pcdata->upgrade_level > old_upgrade_level );

            if ( did_first_upgrade || did_upgrade2 )
            {
                if ( did_first_upgrade )
                {
                    int new_pref = bot_upgrade_class_pref( old_class );
                    BOT_DATA *bot = ch->pcdata->botdata;
                    if ( new_pref >= 0 && bot && bot->roster )
                    {
                        bot->roster->class_pref = new_pref;
                        save_bot_roster();
                    }
                }
                {
                    FILE *ufp;
                    time_t now = time(NULL);
                    char tbuf[64];
                    struct tm *tm_info = localtime(&now);
                    const char *new_class_name = player_class_name( ch );
                    const char *old_class_name;
                    int saved_class = ch->class;
                    ch->class = old_class;
                    old_class_name = player_class_name( ch );
                    ch->class = saved_class;
                    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm_info);
                    fclose( fpReserve );
                    if ( ( ufp = fopen( "../txt/bot_upgrades.log", "a" ) ) != NULL )
                    {
                        if ( did_upgrade2 )
                            fprintf( ufp, "[%s] %s upgraded: %s L%d -> %s L%d (gen %d)\n",
                                tbuf, ch->name,
                                old_class_name, old_upgrade_level,
                                new_class_name, ch->pcdata->upgrade_level,
                                ch->generation );
                        else
                            fprintf( ufp, "[%s] %s upgraded: %s -> %s (gen %d)\n",
                                tbuf, ch->name,
                                old_class_name, new_class_name,
                                ch->generation );
                        fclose( ufp );
                    }
                    fpReserve = fopen( NULL_FILE, "r" );
                }
                if ( did_upgrade2 )
                    bot_watch_msg( ch, "[UPGRADE] upgrade2 complete! Level up.\n\r" );
                else
                    bot_watch_msg( ch, "[UPGRADE] Class upgraded! Roster updated.\n\r" );
                bot_do_recall( ch );  /* return home to start next training cycle */
            }
        }
        return TRUE;
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

    /* Cheap generation upgrade: costs only 10M exp at gen 6 or higher. */
    if ( ch->generation >= 6 && ch->exp >= 10000000 )
    {
        bot_cmd( ch, "train generation" );
        return TRUE;
    }

    /* Primal for class gear.
     * The "train primal N" command caps at 200 per invocation, so clamp the
     * request; the bot will loop back on the next tick for the remainder. */
    if ( bot_should_train_primal(ch) )
    {
        int target  = bot_primal_target(ch);
        int needed  = target - ch->practice;   /* always >= 1 here */
        if ( needed > 200 ) needed = 200;
        char cmd[32];
        sprintf( cmd, "train primal %d", needed );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    /* Generic spell practice */
    {
        static const char *practice_spells[] = {
            "repair", "remove curse", "stone skin", "sanctuary", "fly", "pass door", "shield", "armor", "bless", "frenzy",
            "cure blindness", "faerie fog", NULL
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
            /* SS2 - pool once max_hit >= 30k, buy at 120M */
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
                if ( ch->max_hit >= 20000 || ch->max_hit >= hp_cap ) return FALSE; /* pool */
                /* else fall through to HP training */
            }
            /* SS3 - pool once max_hit >= 40k, buy at 140M */
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
                if ( ch->max_hit >= 22000 || ch->max_hit >= hp_cap ) return FALSE; /* pool */
                /* else fall through to HP training */
            }
            /* SS4 - pool once max_hit >= 50k, buy at 200M */
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
                if ( ch->max_hit >= 24000 || ch->max_hit >= hp_cap ) return FALSE; /* pool */
                /* else fall through to HP training */
            }
            /* SS5 - pool once max_hit >= 70k, buy at 380M */
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
                if ( ch->max_hit >= 26000 || ch->max_hit >= hp_cap ) return FALSE; /* pool */
                /* else fall through to HP training */
            }
            else
            {
                /* Waiting for mastery */
                return FALSE;
            }
        }
    }

    /* Samurai: Pool exp for martial techniques once we hit the HP gate */
    if ( ch->class == CLASS_SAMURAI && ch->max_hit >= 20000 )
    {
        if ( !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_SLIDE)
          || !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_SIDESTEP)
          || !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_BLOCK)
          || !IS_SET(ch->pcdata->powers[SAMURAI_MARTIAL], SAM_COUNTERMOVE) )
        {
            return FALSE; /* Pool */
        }
    }

    /* Tanarri: pool exp for rank promotions once HP base is established */
    if ( IS_CLASS(ch, CLASS_TANARRI) && ch->max_hit >= 10000 )
    {
        long pool = bot_tan_pool_exp( ch );
        if ( pool > 0 ) return FALSE;
    }

    /* Angel: pool exp for track training once HP base is established */
    if ( IS_CLASS(ch, CLASS_ANGEL) && ch->max_hit >= 5000 )
    {
        long pool = bot_ang_pool_exp( ch );
        if ( pool > 0 ) return FALSE;
    }

    /* Lich: pool exp for lore training once HP base is established */
    if ( IS_CLASS(ch, CLASS_LICH) && ch->max_hit >= 5000 )
    {
        long pool = bot_lich_pool_exp( ch );
        if ( pool > 0 ) return FALSE;
    }

    /* Ninja: pool exp for belt ranks that cost > 10M */
    if ( IS_CLASS(ch, CLASS_NINJA) && ch->max_hit >= 5000 )
    {
        long pool = bot_ninja_pool_exp( ch );
        if ( pool > 0 ) return FALSE;
    }

    /* Vampire: pool exp for age milestones that cost > 10M */
    if ( IS_CLASS(ch, CLASS_VAMPIRE) && ch->max_hit >= 5000 )
    {
        long pool = bot_vamp_pool_exp( ch );
        if ( pool > 0 ) return FALSE;
    }

    /* Bots pursuing an upgrade (first upgrade or upgrade2): train to current
     * stat targets, then train generation (first upgrade only) or pool for PvP. */
    if ( bot_can_upgrade(ch) )
    {
        int hp_need, mana_need, move_need, qp_need, pk_need;
        bot_upgrade_stat_needs( ch, &hp_need, &mana_need, &move_need, &qp_need, &pk_need );

        if ( ch->max_hit  < hp_need   && ch->max_hit  < hp_cap && ch->exp >= ch->max_hit  + 1 )
            { bot_cmd( ch, "train hp all" );   return TRUE; }
        if ( ch->max_mana < mana_need && ch->max_mana < hp_cap && ch->exp >= ch->max_mana + 1 )
            { bot_cmd( ch, "train mana all" ); return TRUE; }
        if ( ch->max_move < move_need && ch->max_move < hp_cap && ch->exp >= ch->max_move + 1 )
            { bot_cmd( ch, "train move all" ); return TRUE; }

        /* First upgrade only: gen 5->4->3->2 via train; gen 1 via PvP gensteal. */
        if ( !is_upgrade(ch) && ch->generation >= 3 )
        {
            int gencost;
            if      ( ch->generation == 3 ) gencost = 400000000;
            else if ( ch->generation == 4 ) gencost = 200000000;
            else if ( ch->generation == 5 ) gencost =  50000000;
            else                            gencost =  10000000;
            if ( ch->exp >= gencost )
                { bot_cmd( ch, "train generation" ); return TRUE; }
        }
        /* First upgrade: stats+QP met but pkscore below threshold — dump excess
         * exp into hp/mana/move up to hp_cap while PvPing for pkscore. */
        if ( !is_upgrade(ch)
          && ch->generation == 1
          && ch->pcdata->quest >= qp_need
          && get_ratio(ch) < pk_need )
        {
            if ( ch->max_hit  < hp_cap && ch->exp >= ch->max_hit  + 1 )
                { bot_cmd( ch, "train hp all" );   return TRUE; }
            if ( ch->max_mana < hp_cap && ch->exp >= ch->max_mana + 1 )
                { bot_cmd( ch, "train mana all" ); return TRUE; }
            if ( ch->max_move < hp_cap && ch->exp >= ch->max_move + 1 )
                { bot_cmd( ch, "train move all" ); return TRUE; }
        }
        return FALSE;  /* pool — keep grinding/PvPing for more exp, QP, pkscore, or gen */
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

/* Returns TRUE if any other player character (including other bots) is in
 * the room.  NPCs are ignored. */
static bool bot_room_has_others( CHAR_DATA *ch )
{
    CHAR_DATA *p;
    if ( ch->in_room == NULL ) return FALSE;
    for ( p = ch->in_room->people; p != NULL; p = p->next_in_room )
    {
        if ( p == ch )       continue;
        if ( IS_NPC(p) )    continue;
        return TRUE;
    }
    return FALSE;
}

/* Try to move to an adjacent room that has no other player characters.
 * Returns TRUE if the bot issued a move command, FALSE if no suitable
 * exit was found (bot stays put). */
static bool bot_find_empty_room( CHAR_DATA *ch )
{
    int door, tries;
    EXIT_DATA *pexit;

    for ( tries = 0; tries < 6; tries++ )
    {
        door = number_range( 0, 5 );
        if ( ch->in_room == NULL ) return FALSE;

        if ( !bot_dir_allowed( ch, door ) )              continue;
        pexit = ch->in_room->exit[door];
        if ( pexit == NULL || pexit->to_room == NULL )   continue;
        if ( IS_SET(pexit->exit_info, EX_CLOSED) )       continue;
        if ( IS_SET(pexit->to_room->room_flags, ROOM_PRIVATE) ) continue;

        /* Check if the destination room is empty of other PCs */
        {
            CHAR_DATA *p;
            bool has_pc = FALSE;
            for ( p = pexit->to_room->people; p != NULL; p = p->next_in_room )
            {
                if ( !IS_NPC(p) )
                {
                    has_pc = TRUE;
                    break;
                }
            }
            if ( !has_pc )
            {
                bot_watch_msg( ch, "[REST] moving to empty room to rest\n\r" );
                bot_cmd( ch, dir_name[door] );
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* Pick a random direction and try to move */
static void bot_try_move( CHAR_DATA *ch )
{
    int tries, door;
    EXIT_DATA *pexit;

    if ( IS_SET(ch->affected_by, AFF_WEBBED) )
    {
        bot_cmd( ch, "flex" );
        return;
    }

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

    if ( IS_SET(ch->affected_by, AFF_WEBBED) )
    {
        bot_cmd( ch, "flex" );
        return;
    }
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

        /* Stop scattering if destination has killable mobs and no other players */
        {
            CHAR_DATA *p;
            bool has_mob = FALSE, has_player = FALSE;
            for ( p = pexit->to_room->people; p != NULL; p = p->next_in_room )
            {
                if ( IS_NPC(p) && !p->fighting && !IS_AFFECTED(p, AFF_ETHEREAL)
                     && IS_SET(p->act, ACT_IS_NPC) && p->master == NULL )
                    has_mob = TRUE;
                else if ( !IS_NPC(p) && p != ch )
                    has_player = TRUE;
            }
            if ( has_mob && !has_player )
                bot->scatter_steps = 0;
        }
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

        /* Stop scattering if destination has killable mobs and no other players */
        {
            CHAR_DATA *p;
            bool has_mob = FALSE, has_player = FALSE;
            for ( p = pexit->to_room->people; p != NULL; p = p->next_in_room )
            {
                if ( IS_NPC(p) && !p->fighting && !IS_AFFECTED(p, AFF_ETHEREAL)
                     && IS_SET(p->act, ACT_IS_NPC) && p->master == NULL )
                    has_mob = TRUE;
                else if ( !IS_NPC(p) && p != ch )
                    has_player = TRUE;
            }
            if ( has_mob && !has_player )
                bot->scatter_steps = 0;
        }
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
        if ( !IS_NPC(victim) )                      continue;   /* Don't attack players */
        if ( victim->fighting )                     continue;   /* Skip mobs already in combat */
        if ( IS_AFFECTED(victim, AFF_ETHEREAL) )    continue;   /* Can't fight ethereal mobs */
        if ( victim->master != NULL )               continue;   /* Don't attack summoned pets (golems, charmed mobs, etc.) */
        if ( !can_see(ch, victim) )                 continue;   /* Can't attack what we can't see (invis, hidden, etc.) */
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
        if ( bot_in_upgrade_hunt(ch) )
            do_pvp = TRUE;  /* always seek PvP when hunting pkscore/gen/QP for upgrade */
        else if ( global_bot_pvp_mode == BOT_PVP_MODE_WAR )
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
                bot->pvp_bot_initiated = TRUE;
                snprintf(msg, sizeof(msg), "[PVP] Selected %s for hunting\n\r", bot->pvp_target);
                bot_watch_msg( ch, msg );
                if ( bot_pvp_is_grudge(bot, target->name) )
                    bot_flame_grudge_hunt( ch, target->name );
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
    BOT_DATA  *bot = ch->pcdata ? ch->pcdata->botdata : NULL;
    CHAR_DATA *victim;
    CHAR_DATA *best_victim = NULL;
    int        count = 0;
    char       dbg[256];

    /* First pass: prefer grudge targets (players who have attacked us).
     * If at least one valid grudge target exists, pick only among those. */
    if ( bot && bot->grudge_count > 0 )
    {
        for ( victim = char_list; victim != NULL; victim = victim->next )
        {
            if ( IS_NPC(victim) ) continue;
            if ( victim == ch ) continue;
            if ( victim->in_room == NULL ) continue;
            if ( !bot_pvp_is_grudge(bot, victim->name) ) continue;
            if ( bot_pvp_is_blacklisted(bot, victim->name) ) continue;
            if ( !bot_is_valid_pvp(ch, victim, dbg, sizeof(dbg)) )
            {
                bot_watch_msg(ch, dbg);
                continue;
            }
            if ( number_range(0, count) == 0 )
                best_victim = victim;
            count++;
        }
        if ( best_victim != NULL )
            return best_victim;
    }

    /* Second pass: any valid target not on the blacklist */
    count = 0;
    for ( victim = char_list; victim != NULL; victim = victim->next )
    {
        if ( IS_NPC(victim) ) continue;
        if ( victim == ch ) continue;
        if ( victim->in_room == NULL ) continue;
        if ( bot && bot_pvp_is_blacklisted(bot, victim->name) ) continue;

        if ( !bot_is_valid_pvp(ch, victim, dbg, sizeof(dbg)) )
        {
            bot_watch_msg(ch, dbg);
            continue;
        }

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

    /* Decapped bots are mortals (level 2) and can't wear their class gear,
     * which leaves them stuck re-trying the newbiepack forever. Bounce to
     * training so 'train avatar' restores their class before grinding. */
    if ( ch->level == 2 && ch->max_hit >= 2000 )
    {
        bot_watch_msg( ch, "[REASON] mortal in grind state, needs train avatar\n\r" );
        bot_change_state( ch, bot, BOT_TRAINING );
        return;
    }

    if ( bot_upgrade_ready(ch) && ch->position != POS_FIGHTING )
    {
        bot_watch_msg( ch, "[REASON] upgrade ready -- heading to altar\n\r" );
        bot_change_state( ch, bot, BOT_TRAINING );
        return;
    }

    /* Scatter: take random steps into the zone so bots don't all pile up at
     * the entrance.  Stop early if combat starts. */
    if ( bot->scatter_steps > 0 && ch->position != POS_FIGHTING )
    {
        bot_scatter_move( ch, bot );
        bot->scatter_steps--;
        return;
    }

    /* Area escape check: if the bot wandered out of its grind zone (e.g. via
     * a door or exit not covered by bot_area_rules), recall and let the idle
     * state re-navigate.  Only fires when nav queue is empty and scatter is done. */
    if ( ch->in_room != NULL && ch->in_room->area != NULL
      && ch->in_room->area->filename != NULL )
    {
        bool in_valid_zone = FALSE;
        int  tier_i, route_i, rn;

        for ( tier_i = 0; tier_i < GRIND_TIER_COUNT; tier_i++ )
        {
            if ( ch->max_hit >= grind_tiers[tier_i].max_hit ) continue;

            for ( route_i = 0; route_i < grind_tiers[tier_i].num_routes && !in_valid_zone; route_i++ )
            {
                const char **route = grind_tiers[tier_i].routes[route_i];
                if ( route == NULL ) continue;
                for ( rn = 0; route_names[rn].route != NULL; rn++ )
                {
                    if ( route_names[rn].route == route
                      && !str_cmp( ch->in_room->area->filename, route_names[rn].filename ) )
                    {
                        in_valid_zone = TRUE;
                        break;
                    }
                }
            }
            break; /* only check first matching tier */
        }

        if ( !in_valid_zone )
        {
            bot_watch_msg( ch, "[GRIND] outside grind zone -- recalling to restart\n\r" );
            if ( bot_do_recall(ch) )
                bot_change_state( ch, bot, BOT_IDLE );
            return;
        }
    }

    if ( bot_needs_rest(ch) )
    {
        char r[128];
        snprintf( r, sizeof(r), "[REASON] needs rest: hp %d%% mana %d%%\n\r",
            ch->hit  * 100 / UMAX(1, ch->max_hit),
            ch->mana * 100 / UMAX(1, ch->max_mana) );
        bot_watch_msg( ch, r );
        bot_change_state( ch, bot, BOT_RESTING );
        if ( ch->position == POS_FIGHTING )
            do_flee( ch, "" );
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
            if ( bot_in_upgrade_hunt(ch) )
                do_pvp = TRUE;
            else if ( global_bot_pvp_mode == BOT_PVP_MODE_WAR ) do_pvp = TRUE;
            else if ( global_bot_pvp_mode == BOT_PVP_MODE_NORMAL && bot->roster && bot->roster->aggression > 0 && number_percent() < bot->roster->aggression ) do_pvp = TRUE;

            if ( do_pvp )
            {
                CHAR_DATA *target = bot_find_pvp_target(ch);
                if ( target != NULL )
                {
                    char msg[256];
                    strncpy(bot->pvp_target, target->name, sizeof(bot->pvp_target) - 1);
                    bot->pvp_target[sizeof(bot->pvp_target) - 1] = '\0';
                    bot->pvp_bot_initiated = TRUE;
                    snprintf(msg, sizeof(msg), "[PVP] Selected %s for hunting\n\r", bot->pvp_target);
                    bot_watch_msg( ch, msg );
                    if ( bot_pvp_is_grudge(bot, target->name) )
                        bot_flame_grudge_hunt( ch, target->name );
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
    if ( bot->grind_attempts >= 1 )
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
            if ( bot_in_upgrade_hunt(ch) )
                do_pvp = TRUE;
            else if ( global_bot_pvp_mode == BOT_PVP_MODE_WAR ) do_pvp = TRUE;
            else if ( global_bot_pvp_mode == BOT_PVP_MODE_NORMAL && bot->roster && bot->roster->aggression > 0 && number_percent() < bot->roster->aggression ) do_pvp = TRUE;

            if ( do_pvp )
            {
                CHAR_DATA *target = bot_find_pvp_target(ch);
                if ( target != NULL )
                {
                    char msg[256];
                    strncpy(bot->pvp_target, target->name, sizeof(bot->pvp_target) - 1);
                    bot->pvp_target[sizeof(bot->pvp_target) - 1] = '\0';
                    bot->pvp_bot_initiated = TRUE;
                    snprintf(msg, sizeof(msg), "[PVP] Selected %s for hunting\n\r", bot->pvp_target);
                    bot_watch_msg( ch, msg );
                    if ( bot_pvp_is_grudge(bot, target->name) )
                        bot_flame_grudge_hunt( ch, target->name );
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

/* Find shortest path from start to target. Fills path[] with directions (0-5)
 * and returns path length, or -1 if no path. max_steps caps the output buffer. */
static int bot_find_path( ROOM_INDEX_DATA *start, ROOM_INDEX_DATA *target,
                          int *path, int max_steps )
{
    struct bfs_queue {
        ROOM_INDEX_DATA *room;
    } *queue;
    /* parent_vnum[v] = vnum of the room we came from to reach v.
     * parent_dir[v]  = direction taken from parent to reach v.
     * Together these let us walk back from target to start and reverse. */
    int  *parent_vnum;
    char *parent_dir;
    bool *visited;
    int head = 0, tail = 0;
    int i, len = -1;

    if ( start == NULL || target == NULL || path == NULL || max_steps <= 0 )
        return -1;
    if ( start == target )
        return -1;

    visited     = calloc( 250000, sizeof(bool) );
    parent_vnum = calloc( 250000, sizeof(int) );
    parent_dir  = calloc( 250000, sizeof(char) );
    queue       = calloc( 20000,  sizeof(struct bfs_queue) );
    if ( visited == NULL || parent_vnum == NULL || parent_dir == NULL || queue == NULL )
        goto done;

    visited[start->vnum] = TRUE;
    queue[tail++].room = start;

    while ( head < tail && tail < 19990 )
    {
        ROOM_INDEX_DATA *r = queue[head++].room;

        if ( r == target )
        {
            /* Walk back from target to start via parent_dir, collecting directions.
             * Then reverse into the caller's path[] buffer. */
            int   rev[1024];
            int   n = 0;
            ROOM_INDEX_DATA *cur = target;
            while ( cur != start && n < (int)(sizeof(rev)/sizeof(rev[0])) )
            {
                rev[n++] = parent_dir[cur->vnum];
                {
                    int pv = parent_vnum[cur->vnum];
                    ROOM_INDEX_DATA *p = get_room_index( pv );
                    if ( p == NULL ) { n = -1; break; }
                    cur = p;
                }
            }
            if ( n > 0 )
            {
                int copy = n < max_steps ? n : max_steps;
                int k;
                for ( k = 0; k < copy; k++ )
                    path[k] = rev[n - 1 - k];
                len = copy;
            }
            goto done;
        }

        for ( i = 0; i < 6; i++ )
        {
            EXIT_DATA *pexit = r->exit[i];
            if ( pexit != NULL && pexit->to_room != NULL
              && !IS_SET(pexit->exit_info, EX_LOCKED)
              && pexit->to_room->vnum < 250000
              && !visited[pexit->to_room->vnum] )
            {
                visited[pexit->to_room->vnum]     = TRUE;
                parent_vnum[pexit->to_room->vnum] = r->vnum;
                parent_dir[pexit->to_room->vnum]  = (char)i;
                queue[tail++].room = pexit->to_room;
            }
        }
    }

done:
    if ( visited )     free( visited );
    if ( parent_vnum ) free( parent_vnum );
    if ( parent_dir )  free( parent_dir );
    if ( queue )       free( queue );
    return len;
}

static void bot_state_pvp_hunt( CHAR_DATA *ch, BOT_DATA *bot )
{
    CHAR_DATA *victim;

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

    if ( bot_upgrade_ready(ch) )
    {
        bot_watch_msg( ch, "[REASON] upgrade ready -- abandoning hunt for altar\n\r" );
        bot->pvp_target[0] = '\0';
        bot->pvp_chasing = FALSE;
        bot_change_state( ch, bot, BOT_TRAINING );
        return;
    }

    /* Don't pursue at low resources -- heal up first, keeping the target.
     * Exception: if we're chasing a fleeing opponent we were already fighting,
     * skip the health gate and go immediately (they may escape if we wait). */
    if ( !bot->pvp_chasing && !bot_is_healthy(ch) )
    {
        bot_watch_msg( ch, "[PVP] Not healthy enough to hunt -- resting first.\n\r" );
        bot_change_state( ch, bot, BOT_RESTING );
        return;
    }

    victim = get_char_world( ch, bot->pvp_target );
    if ( victim == NULL || !bot_is_valid_pvp(ch, victim, NULL, 0) )
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "[PVP] Target %s lost or no longer valid.\n\r", bot->pvp_target);
        bot_watch_msg( ch, msg );
        /* Hunt failed without a kill -- blacklist if we initiated this hunt */
        if ( bot->pvp_bot_initiated )
        {
            bot_pvp_add_blacklist( bot, bot->pvp_target );
            snprintf( msg, sizeof(msg), "[GRUDGE] Blacklisting %s (hunt failed, no kill).\n\r", bot->pvp_target );
            bot_watch_msg( ch, msg );
        }
        bot->pvp_target[0] = '\0';
        bot->pvp_chasing = FALSE;
        bot_change_state( ch, bot, BOT_GRINDING );
        return;
    }

    if ( ch->in_room == victim->in_room )
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "[PVP] Target %s found! Attacking.\n\r", bot->pvp_target);
        bot_watch_msg( ch, msg );
        bot->pvp_chasing = FALSE;
        bot_change_state( ch, bot, BOT_PVP_FIGHT );
        return;
    }

    /* Move towards target */
    if ( IS_SET(ch->affected_by, AFF_WEBBED) )
    {
        bot_watch_msg( ch, "[PVP] webbed -- flexing before pursuing\n\r" );
        bot_cmd( ch, "flex" );
        return;
    }

    {
        int path[32];
        int path_len;
        int nav_cap = (int)(sizeof(bot->nav_cmds) / sizeof(bot->nav_cmds[0]));
        int max_steps = (int)(sizeof(path) / sizeof(path[0]));
        if ( nav_cap < max_steps ) max_steps = nav_cap;

        path_len = bot_find_path( ch->in_room, victim->in_room, path, max_steps );

        if ( path_len > 0 )
        {
            EXIT_DATA *pexit = ch->in_room->exit[path[0]];
            char echo[256];
            int k;
            if ( pexit != NULL && IS_SET(pexit->exit_info, EX_CLOSED) )
            {
                char cmd[64];
                sprintf(cmd, "open %s", dir_name[path[0]]);
                bot_cmd( ch, cmd );
            }
            snprintf( echo, sizeof(echo), "[PVP] BFS found %d-step path -- heading %s to reach %s\n\r",
                      path_len, dir_name[path[0]], bot->pvp_target );
            bot_watch_msg( ch, echo );
            /* Queue the full path; the nav queue drain handles one step per tick.
             * If the victim moves off this path, bot_tick's pre-drain check will
             * flush the queue so we re-plan on the next tick. */
            bot->nav_n = 0;
            for ( k = 0; k < path_len; k++ )
                bot_nav_queue( bot, dir_name[path[k]] );
            bot->pvp_path_target_vnum = victim->in_room->vnum;
        }
        else
        {
            bot_watch_msg( ch, "[PVP] BFS failed -- target unreachable. Halting hunt.\n\r" );
            bot->pvp_target[0] = '\0';
            bot->pvp_chasing = FALSE;
            bot->pvp_path_target_vnum = 0;
            bot_change_state( ch, bot, BOT_GRINDING );
        }
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

    /* If we got beaten down (stunned/incap) but not finished, cut our losses */
    if ( ch->position <= POS_STUNNED )
    {
        bot_watch_msg( ch, "[PVP] Incapacitated -- clearing PVP target and recovering.\n\r" );
        if ( bot->pvp_bot_initiated && bot->pvp_target[0] != '\0' )
        {
            char msg[256];
            bot_pvp_add_blacklist( bot, bot->pvp_target );
            snprintf( msg, sizeof(msg), "[GRUDGE] Blacklisting %s (incapacitated).\n\r", bot->pvp_target );
            bot_watch_msg( ch, msg );
        }
        bot->pvp_target[0] = '\0';
        bot_change_state( ch, bot, BOT_RESTING );
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
        /* Distinguish a loss (bot fled at low HP) from target running away.
         * If below half HP we lost -- don't re-hunt and go recover instead. */
        if ( ch->hit < ch->max_hit / 2 )
        {
            char msg[256];
            snprintf( msg, sizeof(msg), "[PVP] Lost fight vs %s -- clearing target and resting.\n\r", bot->pvp_target );
            bot_watch_msg( ch, msg );
            if ( bot->pvp_bot_initiated )
            {
                bot_pvp_add_blacklist( bot, bot->pvp_target );
                snprintf( msg, sizeof(msg), "[GRUDGE] Blacklisting %s (fled at low HP).\n\r", bot->pvp_target );
                bot_watch_msg( ch, msg );
            }
            bot->pvp_target[0] = '\0';
            bot_change_state( ch, bot, BOT_RESTING );
            return;
        }
        bot->pvp_chasing = TRUE;
        bot_change_state( ch, bot, BOT_PVP_HUNT );
        return;
    }

    if ( victim->position <= POS_STUNNED )
    {
        /* Time to finish them -- count this as a win regardless of who initiated.
         * Issue kill + decap/gensteal in the SAME tick so the bot doesn't miss
         * the mortal-wound window.  kill drops them from stunned/incap to mortal,
         * then the immediately-following decap/gensteal finishes them before the
         * next tick fires. */
        char cmd[256];
        char killmsg[128];
        bool was_grudge = bot_pvp_is_grudge( bot, victim->name );
        /* Revenge satisfied: remove from grudge list and clear nemesis if applicable */
        bot_pvp_remove_grudge( bot, victim->name );
        if ( bot->nemesis[0] != '\0' && !str_cmp( bot->nemesis, victim->name ) )
            bot->nemesis[0] = '\0';
        snprintf( killmsg, sizeof(killmsg), "[GRUDGE] Finishing %s -- removed from grudge list.\n\r", victim->name );
        bot_watch_msg( ch, killmsg );
        if ( was_grudge )
            bot_flame_grudge_kill( ch, victim->name );

        /* Hit them first to ensure they drop to mortal wound */
        sprintf( cmd, "kill %s", victim->name );
        bot_cmd( ch, cmd );

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
    /* Flee if attacked while resting -- need to get out of combat to heal */
    if ( ch->position == POS_FIGHTING )
    {
        bot_watch_msg( ch, "[REASON] attacked while resting, fleeing\n\r" );
        do_flee( ch, "" );
        return;
    }

    /* Don't rest with other players around -- find an empty room first.
     * The bot keeps searching each tick until it finds a room with no
     * other PCs, or until the rest timer expires (at which point it
     * gives up and rests in place). */
    if ( bot_room_has_others(ch) && bot->state_timer > 0 )
    {
        if ( ch->position != POS_STANDING )
        {
            do_stand( ch, "" );
            return;
        }
        if ( bot_find_empty_room(ch) )
            return;  /* moved -- check again next tick */
        /* No empty adjacent room found this tick -- try again next tick */
        bot_watch_msg( ch, "[REST] searching for empty room...\n\r" );
        return;
    }

    /* Can't rest while being hunted -- go back to panic flee */
    if ( bot->pvp_attacker[0] != '\0' )
    {
        bot_watch_msg( ch, "[REASON] still being hunted -- can't rest, fleeing\n\r" );
        bot_change_state( ch, bot, BOT_PVP_FLEE );
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

    /* Remove curse before anything else -- AFF_CURSE blocks recall so a bot
     * stuck in a no-exit classhq zone can never escape until it is cleared.
     * bot_generic_buff_check handles the skill check. */
    if ( IS_AFFECTED(ch, AFF_CURSE) && bot_generic_buff_check(ch) )
        return;

    /* Liches have no natural HP regen. Use chant heal (color spell, no lore req)
     * to recover while resting. between_fights handles the mana/HP guards. */
    if ( IS_CLASS(ch, CLASS_LICH) && !bot_is_healthy(ch) && bot->roster )
    {
        const BOT_CLASS_AI *ai = bot_class_ai[bot->roster->class_pref];
        if ( ai && ai->between_fights && ai->between_fights(ch) )
            return;
    }

    /* Ninja HP regen (update_ninja in update.c) requires ch->rage > 0, and
     * rage decays by 1/tick when not fighting.  Fire michi via buff_check so
     * ninjas refresh rage while resting instead of meditating (which doesn't
     * help them regen). */
    if ( IS_CLASS(ch, CLASS_NINJA) && bot->roster )
    {
        const BOT_CLASS_AI *ai = bot_class_ai[bot->roster->class_pref];
        if ( ai && ai->buff_check && ai->buff_check(ch) )
            return;
    }

    /* Meditate while recovering if needed and gear is ready */
    if ( bot->needs_meditate && bot->ready_meditate
      && ch->position != POS_MEDITATING )
    {
        bot_cmd( ch, "meditate" );
        return;
    }

    /* Done -- stand up, clear flags, transition out */
    if ( bot_is_healthy(ch) || bot->state_timer <= 0 )
    {
        bot->needs_meditate = FALSE;
        bot->ready_meditate = FALSE;
        if ( ch->position != POS_STANDING )
        {
            do_stand( ch, "" );
            return;
        }
        if ( bot->pvp_target[0] != '\0' )
        {
            bot_watch_msg( ch, "[PVP] Recovered -- resuming hunt.\n\r" );
            bot_change_state( ch, bot, BOT_PVP_HUNT );
        }
        else
            bot_change_state( ch, bot, BOT_IDLE );
    }
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

/* -----------------------------------------------------------------------
 * bot_state_pvp_flee
 *
 * Panic movement state entered when a bot chooses to run from an attacker.
 * The bot stays here until fight_timer reaches 0, then hides in a
 * different grind zone.  Movement is intentionally chaotic -- the bot
 * makes mistakes like a real panicking player would.
 * ----------------------------------------------------------------------- */
static void bot_state_pvp_flee( CHAR_DATA *ch, BOT_DATA *bot )
{
    /* Safe: fight timer gone -- clear attacker and go hide somewhere new */
    if ( ch->fight_timer == 0 )
    {
        if ( bot->pvp_attacker[0] != '\0' )
        {
            char msg[256];
            snprintf( msg, sizeof(msg), "[PVP_FLEE] Fight timer cleared -- hiding from %s.\n\r",
                      bot->pvp_attacker );
            bot_watch_msg( ch, msg );
            bot->pvp_attacker[0] = '\0';
        }
        /* Change state first (sets timers, scatter, calls navigate_to_grind_zone
         * for the tier-based route), then override the nav with any-zone routing
         * so the bot hides far from where it was attacked. */
        bot_change_state( ch, bot, BOT_GRINDING );
        bot->nav_n = 0;
        bot_navigate_to_any_grind_zone( bot, ch );
        bot->pvp_flee_zone[0] = '\0';
        return;
    }

    /* If in combat: try to flee, with occasional chaos freeze */
    if ( ch->position == POS_FIGHTING )
    {
        if ( number_percent() < 25 )
        {
            bot_watch_msg( ch, "[PVP_FLEE] Panic -- forgot to flee this tick!\n\r" );
            return;
        }
        do_flee( ch, "" );
        return;
    }

    /* Must be standing to move */
    if ( ch->position < POS_STANDING )
    {
        bot_cmd( ch, "stand" );
        return;
    }

    /* Attacker walked into our room: bolt immediately */
    if ( bot->pvp_attacker[0] != '\0' )
    {
        CHAR_DATA *hunter = get_char_room( ch, bot->pvp_attacker );
        if ( hunter != NULL )
        {
            bot_watch_msg( ch, "[PVP_FLEE] Attacker in room! Running!\n\r" );
            bot_try_move( ch );
            return;
        }
    }

    /* Running through the world: move each tick with chaos */
    {
        int roll = number_percent();
        if ( roll < 30 )
        {
            /* Hesitate: bot freezes a tick */
            bot_watch_msg( ch, "[PVP_FLEE] Hesitating...\n\r" );
            return;
        }
        /* Move in any available direction -- no pathfinding, pure panic */
        bot_try_move( ch );
    }
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
    BOT_DATA *bot = ch->pcdata ? ch->pcdata->botdata : NULL;
    /* Mage spell-training cycle: ring is intentionally in inventory for identify.
     * Skip all gear management until the cycle completes and the flag clears. */
    if ( bot && bot->spell_training ) return;
    bot_gear_check( ch );
}

/* -----------------------------------------------------------------------
 * bot_check_tied_up - detect and recover from being tied up / gagged
 *
 * Called at the top of bot_update() before bot_check_vision().
 * Returns TRUE if a recovery action was taken (caller should return).
 *
 * TIED_UP: the bot is restrained and interp.c blocks almost every command.
 *   'flex' IS on the tied-up whitelist and do_flex removes TIED_UP (plus
 *   any AFF_WEBBED).  We issue flex first; next tick the bot is free.
 *
 * GAGGED: set on tied-up victims; only blocks speech.  Once TIED_UP is
 *   cleared, 'gag self' removes the gag.
 *
 * BLINDFOLDED: applied alongside tie/gag (gensteal, do_tie).  Handled here
 *   so recovery runs before any state logic that would try to target mobs
 *   the bot can't see.  bot_check_vision has the same branch but is gated
 *   behind darkness/fighting checks that can leave the bit stuck.
 * ----------------------------------------------------------------------- */
static bool bot_check_tied_up( CHAR_DATA *ch, BOT_DATA *bot )
{
    /* Flex removes TIED_UP (and webs); 'flex' is on the tied-up whitelist */
    if ( IS_EXTRA(ch, TIED_UP) )
    {
        bot_watch_msg( ch, "[TIED] tied up -- flexing to break free\n\r" );
        bot_cmd( ch, "flex" );
        return TRUE;
    }

    /* Remove gag once no longer tied (gag self requires not being tied) */
    if ( IS_EXTRA(ch, GAGGED) )
    {
        bot_watch_msg( ch, "[TIED] gagged -- removing gag\n\r" );
        bot_cmd( ch, "gag self" );
        return TRUE;
    }

    /* Remove blindfold once no longer tied.  do_blindfold requires
     * POS_STANDING, so stand up first if resting/sitting. */
    if ( IS_EXTRA(ch, BLINDFOLDED) )
    {
        if ( ch->position == POS_FIGHTING )
            return FALSE;   /* can't blindfold while fighting; try next tick */
        if ( ch->position < POS_STANDING )
        {
            bot_watch_msg( ch, "[TIED] blindfolded -- standing to remove\n\r" );
            bot_cmd( ch, "stand" );
            return TRUE;
        }
        bot_watch_msg( ch, "[TIED] blindfolded -- removing blindfold\n\r" );
        bot_cmd( ch, "blindfold self" );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * bot_check_vision - detect and recover from blindness or darkness
 *
 * Called at the top of bot_update() before the nav queue is drained.
 * Returns TRUE if a recovery action was taken (caller should return).
 *
 * Two distinct causes of blindness, handled differently:
 *
 * Darkness (ROOM_TOTAL_DARKNESS from player-cast darkness):
 *   Phase 1: cast faerie fog to dispel the darkness, preserving nav queue.
 *   Phase 2 (blind_recovery set): if still dark, faerie fog failed -- abort
 *   nav queue and recall.  If fog cleared the darkness, resume normally.
 *
 * Non-dispellable darkness (ROOM_DARK flag, nighttime):
 *   Faerie fog cannot help here.  Recall immediately.
 *
 * AFF_BLIND from gsn_blindness spell:
 *   Cast cure blindness in place.  The bot checks is_affected(gsn_blindness)
 *   first - other sources of AFF_BLIND (drowfire, daemon smoke, item effects)
 *   use a different affect type that cure blindness cannot strip.  Those
 *   expire on their own; the bot returns FALSE so normal AI keeps running.
 *
 * BLINDFOLDED: remove the blindfold in place.
 * ----------------------------------------------------------------------- */
static bool bot_check_vision( CHAR_DATA *ch, BOT_DATA *bot )
{
    bool in_darkness;
    bool in_total_darkness;
    bool is_blind;
    bool is_blindfolded;

    /* Mirror the vision checks in can_see() to determine if the bot is
     * actually blind due to darkness.  Several abilities bypass all darkness:
     *   PLR_HOLYLIGHT  - Angel gsenses, Drow drowsight, Droid unholy sight,
     *                    Ninja kanzuite, Monk divine sight, Vampire power
     *   VAM_SONIC      - vampire bat form echolocation
     *   ITEMA_VISION   - certain artifact items
     * Drow/Droid are also immune to ROOM_TOTAL_DARKNESS specifically. */
    in_darkness       = FALSE;
    in_total_darkness = FALSE;
    if ( ch->in_room != NULL
      && !IS_IMMORTAL(ch)
      && !IS_ITEMAFF(ch, ITEMA_VISION)
      && !IS_SET(ch->act, PLR_HOLYLIGHT)
      && !IS_VAMPAFF(ch, VAM_SONIC) )
    {
        if ( IS_SET(ch->in_room->room_flags, ROOM_TOTAL_DARKNESS)
          && !IS_CLASS(ch, CLASS_DROW)
          && !IS_CLASS(ch, CLASS_DROID) )
        {
            in_darkness       = TRUE;
            in_total_darkness = TRUE;
        }
        else if ( room_is_dark( ch->in_room )
               && !IS_AFFECTED(ch, AFF_INFRARED)
               && !IS_VAMPAFF(ch, VAM_NIGHTSIGHT) )
            in_darkness = TRUE;
    }

    is_blindfolded = IS_EXTRA(ch, BLINDFOLDED);
    is_blind       = ( IS_AFFECTED(ch, AFF_BLIND)
                    && !IS_AFFECTED(ch, AFF_SHADOWSIGHT) );

    /* Phase 2: faerie fog was cast last tick -- check if it worked.
     * If the room is no longer dark, clear the flag and resume normally.
     * If still dark, faerie fog failed -- recall immediately. */
    if ( bot->blind_recovery )
    {
        bot->blind_recovery = FALSE;
        if ( !in_darkness )
        {
            bot_watch_msg( ch, "[VISION] faerie fog cleared darkness -- resuming\n\r" );
            return FALSE;   /* darkness gone, continue normal AI */
        }
        /* Still dark after faerie fog -- recall immediately */
        if ( bot->nav_n > 0 )
        {
            bot_watch_msg( ch, "[VISION] darkness - aborting nav queue\n\r" );
            bot->nav_n = 0;
        }
        bot_watch_msg( ch, "[VISION] darkness - recalling\n\r" );
        bot_do_recall(ch);
        return TRUE;
    }

    /* Darkness: attempt faerie fog first.
     * Faerie fog strips NEW_DARKNESS from Drow/Droid in the room and clears
     * the ROOM_TOTAL_DARKNESS flag. */
    if ( in_darkness )
    {
        if ( ch->position == POS_FIGHTING )
            return FALSE;   /* Can't cast or recall while fighting */

        /* Try faerie fog for any darkness */
        {
            int sn = skill_lookup( "faerie fog" );
            bool can_cast = ( sn > 0 && ch->pcdata->learned[sn] > 0 );

            if ( can_cast )
            {
                if ( IS_CLASS(ch, CLASS_SHAPESHIFTER) && ch->pcdata->powers[SHAPE_FORM] != 0 )
                {
                    bot_watch_msg( ch, "[VISION] darkness - shifting human to cast faerie fog\n\r" );
                    bot_cmd( ch, "shift human" );
                    return TRUE;
                }

                bot_watch_msg( ch, "[VISION] darkness - casting faerie fog\n\r" );
                bot_cmd( ch, "cast 'faerie fog'" );
                bot->blind_recovery = TRUE;  /* check result next tick */
                return TRUE;  /* nav queue preserved */
            }
        }

        /* Faerie fog unavailable -- recall */
        if ( bot->nav_n > 0 )
        {
            bot_watch_msg( ch, "[VISION] darkness - aborting nav queue\n\r" );
            bot->nav_n = 0;
        }
        bot_watch_msg( ch, "[VISION] darkness - recalling\n\r" );
        bot_do_recall(ch);
        return TRUE;
    }

    /* Blindfolded: remove it in place */
    if ( is_blindfolded )
    {
        if ( ch->position == POS_FIGHTING )
            return FALSE;
        bot_watch_msg( ch, "[VISION] removing blindfold\n\r" );
        bot_cmd( ch, "blindfold self" );
        return TRUE;
    }

    /* Blind spell: only attempt cure blindness if the blindness was applied
     * by gsn_blindness - the same check spell_cure_blindness itself uses.
     * Other sources of AFF_BLIND (drowfire, daemon smoke, item effects) use
     * a different affect type; cure blindness silently does nothing against
     * them, so the bot would loop forever.  Those effects are timed and
     * expire on their own - return FALSE so normal AI keeps running. */
    if ( is_blind )
    {
        int sn;
        if ( !is_affected( ch, gsn_blindness ) )
        {
            bot_watch_msg( ch, "[VISION] blind (non-spell) - waiting to expire\n\r" );
            return FALSE;
        }
        if ( ch->position == POS_FIGHTING )
            return FALSE;
        /* Head form (post-decap) cannot cast spells — magic.c rejects
         * polymorphed casters with "You cannot cast spells in this form."
         * Without this guard the bot spams cast every tick while a severed head. */
        if ( IS_HEAD(ch, LOST_HEAD) || IS_AFFECTED(ch, AFF_POLYMORPH) )
            return FALSE;
        /* Shapeshifters in animal form cannot cast spells */
        if ( IS_CLASS(ch, CLASS_SHAPESHIFTER)
          && ch->pcdata->powers[SHAPE_FORM] != 0 )
            return FALSE;
        sn = skill_lookup("cure blindness");
        if ( sn > 0 && ch->pcdata->learned[sn] > 0 )
        {
            bot_watch_msg( ch, "[VISION] casting cure blindness\n\r" );
            bot_cmd( ch, "cast \"cure blindness\" self" );
            return TRUE;
        }
        /* Blindness is curable but bot lacks the spell - wait it out */
        return FALSE;
    }

    return FALSE;
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
     * already in inventory; skip ALL AI until the head respawns with a body
     * and LOST_HEAD is cleared.  This must be the very first check so that
     * no other code path (watchbot, trap eject, PvP, state dispatch, etc.)
     * can issue commands while the bot is a severed head. */
    if ( IS_HEAD( ch, LOST_HEAD ) )
    {
        if ( !bot->decap_recovery )
            //bot_flame_got_headed( ch );
        bot->decap_recovery = TRUE;
        if ( bot->state != BOT_TRAINING )
            bot_change_state( ch, bot, BOT_TRAINING );
        return;
    }

    /* Per-tick status prompt to any watchbot watcher (every 3 ticks) */
    if ( ch->desc != NULL && ch->desc->snoop_by != NULL )
    {
        bot->watch_tick++;
        if ( bot->watch_tick >= 3 )
        {
            char prompt[256];
            bot->watch_tick = 0;
            snprintf( prompt, sizeof(prompt),
                "[TICK] %s (%s) | %-11s | %dhp %dm %dmv %dxp\n\r",
                ch->name, bot_class_str(ch), bot_state_str(bot->state),
                ch->hit, ch->mana, ch->move, ch->exp );
            write_to_buffer( ch->desc->snoop_by, prompt, 0 );
        }
    }

    /* If incapacitated or worse, the bot has no control over its body and
     * must wait to either regenerate enough HP to stand up, or die. */
    if ( ch->position < POS_SLEEPING )
        return;

    /* Safety: eject any bot trapped in a sealed classhq cluster.
     * 93350-93356: spider web area (vampire HQ) -- queen webs/traps.
     * 93420-93426: Hell (demon HQ) -- bots swallowed by spec_eater Satan.
     * All exits in both clusters loop back internally with no path to the world. */
    {
        int vnum = ch->in_room ? ch->in_room->vnum : 0;
        bool in_vamp_trap = ( vnum >= ROOM_VNUM_VAMP_CRYPT && vnum <= ROOM_VNUM_VAMP_CRYPT + 6 );
        bool in_hell_trap = ( vnum >= ROOM_VNUM_HELL && vnum <= ROOM_VNUM_HELL + 6 );
        bool in_trap      = in_vamp_trap || in_hell_trap;
        if ( in_trap )
        {
            ROOM_INDEX_DATA *home;
            char logbuf[256];
            snprintf( logbuf, sizeof(logbuf),
                "[BOT_SAFETY] %s trapped in classhq cluster (room %d) -- ejecting",
                ch->name, vnum );
            log_string( logbuf );
            strncat( logbuf, "\n\r", sizeof(logbuf) - strlen(logbuf) - 1 );
            bot_watch_msg( ch, logbuf );
            if ( ch->position == POS_FIGHTING )
                stop_fighting( ch, TRUE );
            home = get_room_index( ch->home );
            if ( home == NULL ) home = get_room_index( ROOM_VNUM_TEMPLE );
            if ( home != NULL )
            {
                char_from_room( ch );
                char_to_room( ch, home );
                bot_cmd( ch, "look" );
            }
            bot_change_state( ch, bot, BOT_IDLE );
            return;
        }
    }

    /* Skip all gear management during decap recovery — the bot has class gear
     * in inventory from behead() and must not touch newbiepack or class gear
     * until after call all is issued and decap_recovery is cleared. */
    if ( !bot->decap_recovery )
        bot_ensure_geared( ch );

    /* Break free from tied-up / gagged state before anything else */
    if ( bot_check_tied_up( ch, bot ) )
        return;

    /* Recover from blindness or total darkness */
    if ( bot_check_vision( ch, bot ) )
        return;

    /* Decrement timers */
    bot->state_timer--;
    if ( bot->cmd_delay > 0 ) { bot->cmd_delay--; return; }  /* Wait before acting */

    /* Reset cmd delay - human-like pause between commands */
    bot->cmd_delay = number_range( 1, 2 );

    /* PvP hunt: invalidate the queued path if the victim has moved off it.
     * bot->pvp_path_target_vnum is the victim's room at plan time; if the victim
     * is no longer there, the rest of our path is stale -- flush and re-plan on
     * the next state-dispatch tick instead of marching to where they used to be. */
    if ( bot->state == BOT_PVP_HUNT && bot->nav_n > 0 && bot->pvp_path_target_vnum != 0
      && bot->pvp_target[0] != '\0' )
    {
        CHAR_DATA *hv = get_char_world( ch, bot->pvp_target );
        if ( hv == NULL || hv->in_room == NULL
          || hv->in_room->vnum != bot->pvp_path_target_vnum )
        {
            bot_watch_msg( ch, "[PVP] target moved off planned path -- flushing queue to re-plan\n\r" );
            bot->nav_n = 0;
            bot->pvp_path_target_vnum = 0;
        }
    }

    /* Ensure buffs are up before stepping through the nav queue -- otherwise
     * the bot stalls at closed doors (pass door) or lacks needed effects en route. */
    if ( bot->nav_n > 0 && ch->position >= POS_STANDING )
    {
        const BOT_CLASS_AI *ai = NULL;
        if ( bot->roster )
            ai = bot_class_ai[bot->roster->class_pref];

        if ( bot_generic_buff_check(ch) )
            return;
        if ( ai && ai->buff_check && ai->buff_check(ch) )
            return;
    }

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
        if ( IS_SET(ch->affected_by, AFF_WEBBED) )
        {
            bot_watch_msg( ch, "[NAV] blocked -- webbed, flexing\n\r" );
            bot_cmd( ch, "flex" );
            return;   /* keep queue intact, retry once web breaks */
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

    /* -----------------------------------------------------------------------
     * Combat target switch: if we're fighting an NPC and a player starts
     * attacking us, drop the mob and switch to the player.  Never switch
     * off a player we're already fighting.
     * ----------------------------------------------------------------------- */
    if ( ch->fighting != NULL
      && IS_NPC( ch->fighting )
      && ch->in_room != NULL )
    {
        CHAR_DATA *rch;
        for ( rch = ch->in_room->people; rch != NULL; rch = rch->next_in_room )
        {
            if ( rch == ch ) continue;
            if ( IS_NPC( rch ) ) continue;
            if ( rch->fighting != ch ) continue;

            char cmd[MAX_INPUT_LENGTH];
            snprintf( cmd, sizeof(cmd), "combatswitch %s", rch->name );
            bot_watch_msg( ch, "[COMBAT] player attacker detected -- switching target\n\r" );
            bot_cmd( ch, cmd );
            break;
        }
    }

    /* -----------------------------------------------------------------------
     * PvP victim detection (central, runs every tick before state dispatch)
     *
     * If a player or bot attacked us and we haven't recorded them yet,
     * decide immediately: fight back or flee.  This fires from any state
     * so the bot reacts even while grinding, exploring, or resting.
     * ----------------------------------------------------------------------- */
    if ( ch->fighting != NULL
      && !IS_NPC( ch->fighting )
      && bot->pvp_attacker[0] == '\0'   /* not already in flee mode */
      && bot->state != BOT_PVP_FIGHT    /* not us being the aggressor */
      && bot->state != BOT_PVP_FLEE )   /* not already fleeing */
    {
        bot_handle_pvp_attack( ch, bot, ch->fighting );
        return;
    }

    /* If the attacker walked into our room while we're not in combat,
     * snap back to panic-flee regardless of what state we're in. */
    if ( bot->pvp_attacker[0] != '\0'
      && ch->position != POS_FIGHTING
      && bot->state != BOT_PVP_FLEE )
    {
        CHAR_DATA *hunter = get_char_room( ch, bot->pvp_attacker );
        if ( hunter != NULL )
        {
            bot_watch_msg( ch, "[PVP] Attacker found us! Back to fleeing!\n\r" );
            bot_change_state( ch, bot, BOT_PVP_FLEE );
            return;
        }
    }

    /* Nemesis interrupt: if our nemesis walks into our room, attack on sight.
     * Fires from any state except active combat, fleeing, or logging out. */
    if ( bot->nemesis[0] != '\0'
      && bot->state != BOT_PVP_FIGHT
      && bot->state != BOT_PVP_FLEE
      && bot->state != BOT_LOGGING_OUT
      && ch->position != POS_FIGHTING )
    {
        CHAR_DATA *nem = get_char_room( ch, bot->nemesis );
        if ( nem != NULL && bot_is_valid_pvp(ch, nem, NULL, 0) )
        {
            char msg[128];
            snprintf( msg, sizeof(msg), "[GRUDGE] Nemesis %s is here -- attacking!\n\r", bot->nemesis );
            bot_watch_msg( ch, msg );
            bot_flame_grudge_hunt( ch, bot->nemesis );
            strncpy( bot->pvp_target, bot->nemesis, sizeof(bot->pvp_target)-1 );
            bot->pvp_target[sizeof(bot->pvp_target)-1] = '\0';
            bot->pvp_bot_initiated = TRUE;
            bot_change_state( ch, bot, BOT_PVP_FIGHT );
            char cmd[64];
            snprintf( cmd, sizeof(cmd), "kill %s", bot->nemesis );
            bot_cmd( ch, cmd );
            return;
        }
    }

    /* Continuous WAR MODE check (skip if we're already fleeing/being hunted) */
    if ( global_bot_pvp_mode == BOT_PVP_MODE_WAR
      && bot->pvp_attacker[0] == '\0'
      && bot->state != BOT_PVP_HUNT
      && bot->state != BOT_PVP_FIGHT
      && bot->state != BOT_PVP_FLEE
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
                bot->pvp_bot_initiated = TRUE;
                snprintf(msg, sizeof(msg), "[PVP] WAR MODE ongoing override -> hunting %s\n\r", bot->pvp_target);
                bot_watch_msg( ch, msg );
                if ( bot_pvp_is_grudge(bot, target->name) )
                    bot_flame_grudge_hunt( ch, target->name );
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
    case BOT_PVP_FLEE:    bot_state_pvp_flee(   ch, bot ); break;
    default:
        bot_change_state( ch, bot, BOT_IDLE );
        break;
    }
}
