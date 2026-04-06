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
extern const BOT_CLASS_AI bot_mage_ai;

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
    &bot_werewolf_ai,  /* BOT_CLASS_WEREWOLF  */
    &bot_mage_ai       /* BOT_CLASS_MAGE      */
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

        if ( bot->cmd_history_count >= 10 && !bot->spell_training )
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
            if ( stuck )
            {
                char logbuf[512];
                const char *area_name = ( ch->in_room && ch->in_room->area )
                                        ? ch->in_room->area->name : "unknown area";
                const char *room_name = ch->in_room ? ch->in_room->name : "unknown room";
                int         vnum      = ch->in_room ? ch->in_room->vnum : -1;
                FILE       *fp;

                snprintf( logbuf, sizeof(logbuf),
                    "[STUCK] bot %s stuck on '%s' -- room: %s (%d) in %s -- recalling",
                    ch->name, buf, room_name, vnum, area_name );
                log_string( logbuf );

                /* Append to bugs.txt using the fpReserve pattern */
                fclose( fpReserve );
                if ( ( fp = fopen( BUG_FILE, "a" ) ) != NULL )
                {
                    fprintf( fp, "%s\n", logbuf );
                    fclose( fp );
                }
                fpReserve = fopen( NULL_FILE, "r" );

                /* Notify any watcher */
                strncat( logbuf, "\n\r", sizeof(logbuf) - strlen(logbuf) - 1 );
                bot_watch_msg( ch, logbuf );

                /* Clear history so we don't spam recalls */
                bot->cmd_history_count = 0;
                bot->cmd_history_head  = 0;
                interpret( ch, "recall" );
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
        bot->state_timer      = number_range( BOT_TIMER_RESTING_MIN, BOT_TIMER_RESTING_MAX );
        bot->meditate_pending = TRUE;   /* gear must settle before meditating */
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
    case BOT_CLASS_MAGE:      return "mage";
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
        "stone", "sanctuary", "fly", "pass door", "shield", "armor", "bless", "frenzy", NULL
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
            if ( !strcmp(buffs[i], "fly") && IS_AFFECTED(ch, AFF_FLYING) )
                continue;
            if ( !strcmp(buffs[i], "pass door") && IS_AFFECTED(ch, AFF_PASS_DOOR) )
                continue;
            char cmd[64];
            if ( !strcmp(buffs[i], "pass door") )
                sprintf(cmd, "cast \"pass door\"");
            else
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

    /* Cheap generation upgrade: costs only 10M exp at gen 6 or higher */
    if ( ch->generation >= 6 && ch->exp >= 10000000 )           return TRUE;

    /* Primal for class gear takes priority over hp/mana/move spending */
    if ( bot_should_train_primal(ch) )                           return TRUE;

    /* Check if we need to practice generic spells */
    {
        static const char *practice_spells[] = {
            "repair", "rem", "stone", "sanctuary", "fly", "pass door", "shield", "armor", "bless", "frenzy",
            "cure blindness", NULL
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
                if ( ch->max_hit >= 30000 ) return TRUE;  /* pool (bot_do_train gates at 120M) */
                /* else fall through to HP training */
            }
            else if ( ch->stance[20] != -1 && ch->stance[14] >= 200 && ch->stance[21] == -1 )
            {
                if ( ch->max_hit >= 40000 ) return TRUE;  /* pool until 140M */
                /* else fall through */
            }
            else if ( ch->stance[21] != -1 && ch->stance[15] >= 200 && ch->stance[22] == -1 )
            {
                if ( ch->max_hit >= 50000 ) return TRUE;  /* pool until 200M */
                /* else fall through */
            }
            else if ( ch->stance[22] != -1 && ch->stance[16] >= 200 && ch->stance[23] == -1 )
            {
                if ( ch->max_hit >= 60000 ) return TRUE;  /* pool until 380M */
                /* else fall through */
            }
            else
            {
                /* Basic maxed but waiting for current SS mastery to hit 200 — pool */
                return FALSE;
            }
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

    /* Cheap generation upgrade: costs only 10M exp at gen 6 or higher */
    if ( ch->generation >= 6 && ch->exp >= 10000000 )
    {
        bot_cmd( ch, "train generation" );
        return TRUE;
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
            "repair", "remove curse", "stone skin", "sanctuary", "fly", "pass door", "shield", "armor", "bless", "frenzy",
            "cure blindness", NULL
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
                if ( ch->max_hit >= 30000 ) return FALSE; /* pool */
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
                if ( ch->max_hit >= 40000 ) return FALSE; /* pool */
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
                if ( ch->max_hit >= 50000 ) return FALSE; /* pool */
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
                if ( ch->max_hit >= 70000 ) return FALSE; /* pool */
                /* else fall through to HP training */
            }
            else
            {
                /* Waiting for mastery */
                return FALSE;
            }
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
                     && IS_SET(p->act, ACT_IS_NPC) )
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
                     && IS_SET(p->act, ACT_IS_NPC) )
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
            bot_cmd( ch, "recall" );
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
        bot->pvp_chasing = FALSE;
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

    /* If we got beaten down (stunned/incap) but not finished, cut our losses */
    if ( ch->position <= POS_STUNNED )
    {
        bot_watch_msg( ch, "[PVP] Incapacitated -- clearing PVP target and recovering.\n\r" );
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
    /* Flee if attacked while resting -- need to get out of combat to heal */
    if ( ch->position == POS_FIGHTING )
    {
        bot_watch_msg( ch, "[REASON] attacked while resting, fleeing\n\r" );
        do_flee( ch, "" );
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

    /* Demons must heal in Hell (93420) */
    if ( IS_CLASS(ch, CLASS_DEMON) )
    {
        if ( ch->in_room != NULL && !(ch->in_room->vnum >= ROOM_VNUM_HELL && ch->in_room->vnum <= ROOM_VNUM_HELL + 6) )
        {
            bot_watch_msg( ch, "[REASON] Demon retreating to Hell to heal\n\r" );
            char_from_room(ch);
            char_to_room(ch, get_room_index(ROOM_VNUM_HELL));
            bot_cmd(ch, "look");
        }
    }

    /* Remove curse before anything else -- AFF_CURSE blocks recall so a bot
     * stuck in a no-exit classhq zone (e.g. Hell) can never escape until it
     * is cleared.  bot_generic_buff_check handles the skill check. */
    if ( IS_AFFECTED(ch, AFF_CURSE) )
    {
        if ( bot_generic_buff_check(ch) )
            return;
    }

    /* Mage-class bots must meditate to regain mana efficiently.
     * Wait while bot_ensure_geared still has work to do (meditate_pending).
     * Gear can take many ticks; the flag clears only when gear_check falls
     * through with nothing left, preventing a meditate->stand->meditate loop. */
    if ( ch->position != POS_MEDITATING
      && ( IS_CLASS(ch, CLASS_MAGE) || IS_CLASS(ch, CLASS_MONK)
        || IS_CLASS(ch, CLASS_NINJA) || IS_CLASS(ch, CLASS_DROW)
        || IS_CLASS(ch, CLASS_LICH) ) )
    {
        if ( bot->meditate_pending )
            return;   /* gear not settled yet — wait */
        bot_cmd( ch, "meditate" );
        return;
    }

    /* Just wait for HP to recover -- bot_ensure_geared handles standing/gearing */
    if ( bot_is_healthy(ch) || bot->state_timer <= 0 )
    {
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
 * bot_check_vision - detect and recover from blindness or total darkness
 *
 * Called at the top of bot_update() before the nav queue is drained.
 * Returns TRUE if a recovery action was taken (caller should return).
 *
 * ROOM_TOTAL_DARKNESS: recall to safety (two-phase via blind_recovery flag).
 *   After recall the darkness check won't fire again in the safe room, so
 *   blind_recovery is only used to wait one tick before rechecking.
 *
 * AFF_BLIND / BLINDFOLDED: cure in place each tick - no recall needed.
 *   If the bot doesn't know cure blindness yet it returns FALSE so normal
 *   AI keeps running; the blind will wear off on its own.
 * ----------------------------------------------------------------------- */
static bool bot_check_vision( CHAR_DATA *ch, BOT_DATA *bot )
{
    bool in_total_dark;
    bool is_blind;
    bool is_blindfolded;

    /* Drow and Droid see through total darkness natively - never trigger */
    in_total_dark = ( ch->in_room != NULL
                   && IS_SET(ch->in_room->room_flags, ROOM_TOTAL_DARKNESS)
                   && !IS_CLASS(ch, CLASS_DROW)
                   && !IS_CLASS(ch, CLASS_DROID) );

    is_blindfolded = IS_EXTRA(ch, BLINDFOLDED);
    is_blind       = ( IS_AFFECTED(ch, AFF_BLIND)
                    && !IS_AFFECTED(ch, AFF_SHADOWSIGHT) );

    /* Phase 2: recalled from a dark room last tick - just clear the flag.
     * If the bot is still somehow in darkness, in_total_dark will catch it
     * below; if not, we're done. */
    if ( bot->blind_recovery )
    {
        bot->blind_recovery = FALSE;
        /* Fall through to handle any lingering conditions */
    }

    /* Total darkness: must escape the room - recall to safety */
    if ( in_total_dark )
    {
        if ( ch->position == POS_FIGHTING )
            return FALSE;   /* Can't recall while fighting */
        if ( bot->nav_n > 0 )
        {
            bot_watch_msg( ch, "[VISION] total darkness - aborting nav queue\n\r" );
            bot->nav_n = 0;
        }
        bot_watch_msg( ch, "[VISION] total darkness - recalling\n\r" );
        bot_cmd( ch, "recall" );
        bot->blind_recovery = TRUE;
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

    /* Blind spell: cure in place each tick until it clears.
     * If the spell isn't learned yet, return FALSE so the bot keeps
     * operating normally while the blind wears off on its own. */
    if ( is_blind )
    {
        int sn;
        if ( ch->position == POS_FIGHTING )
            return FALSE;
        sn = skill_lookup("cure blindness");
        if ( sn > 0 && ch->pcdata->learned[sn] > 0 )
        {
            bot_watch_msg( ch, "[VISION] casting cure blindness\n\r" );
            bot_cmd( ch, "cast \"cure blindness\" self" );
            return TRUE;
        }
        /* Don't know the spell yet - wait it out, don't block AI */
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

    /* Safety: eject any bot trapped in a sealed classhq cluster.
     * 93350-93356: spider web area (vampire HQ) -- queen webs/traps.
     * 93420-93426: Hell (demon HQ) -- non-demon bots swallowed by spec_eater Satan.
     * Demon bots belong in Hell to heal, but non-demons have no exit and get stuck.
     * All exits in both clusters loop back internally with no path to the world. */
    {
        int vnum = ch->in_room ? ch->in_room->vnum : 0;
        bool in_vamp_trap = ( vnum >= ROOM_VNUM_VAMP_CRYPT && vnum <= ROOM_VNUM_VAMP_CRYPT + 6 );
        bool in_hell_trap = ( vnum >= ROOM_VNUM_HELL && vnum <= ROOM_VNUM_HELL + 6 )
                         && !IS_CLASS(ch, CLASS_DEMON);
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

    /* Recover from blindness or total darkness before anything else */
    if ( bot_check_vision( ch, bot ) )
        return;

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
