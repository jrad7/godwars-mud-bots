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

/* Queue a navigation command to be executed before normal AI resumes */
static void bot_nav_queue( BOT_DATA *bot, const char *cmd )
{
    if ( bot->nav_n < 24 )
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

/* -----------------------------------------------------------------------
 * bot_change_state - transition to a new AI state
 * ----------------------------------------------------------------------- */

void bot_change_state( CHAR_DATA *ch, BOT_DATA *bot, bot_state_t new_state )
{
#if BOT_DEBUG
    if ( bot->state != new_state )
    {
        const char *state_names[] = {
            "IDLE", "EXPLORING", "GRINDING", "TRAINING",
            "PVP_HUNT", "PVP_FIGHT", "SHOPPING", "RESTING", "LOGGING_OUT"
        };
        char buf[MAX_STRING_LENGTH];
        sprintf( buf, "BOT DEBUG: %s changing state from %s to %s",
            ch->name,
            bot->state <= BOT_LOGGING_OUT ? state_names[bot->state] : "UNKNOWN",
            new_state <= BOT_LOGGING_OUT ? state_names[new_state] : "UNKNOWN" );
        log_string( buf );
    }
#endif

    bot->state       = new_state;
    bot->cmd_delay   = number_range( 1, 2 );

    switch ( new_state )
    {
    case BOT_IDLE:
        bot->state_timer = number_range( 1, 2 );    /* 1-2 seconds */
        break;
    case BOT_EXPLORING:
        bot->state_timer = number_range( 1, 2 );    /* 1-2 seconds */
        break;
    case BOT_GRINDING:
        bot->state_timer = number_range( 120, 180 );   /* 2-3 minutes */
        bot->grind_attempts = 0;
        /* Navigate to grinding area based on power tier */
        if ( ch->max_hit < 3500 )
        {
            /* Tier 1 - newbie area: recall -> up -> open door -> south */
            bot->nav_n = 0;
            bot_nav_queue( bot, "recall" );
            bot_nav_queue( bot, "up" );
            bot_nav_queue( bot, "open door" );
            bot_nav_queue( bot, "south" );
        }
        else if ( ch->max_hit < 6000 )
        {
            /* Tier 2 - Smurf Village: recall -> 2S -> 3W -> N */
            bot->nav_n = 0;
            bot_nav_queue( bot, "recall" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "west" );
            bot_nav_queue( bot, "west" );
            bot_nav_queue( bot, "west" );
            bot_nav_queue( bot, "north" );
        }
        else
        {
            /* Tier 3 - Elemental Canyon: recall(3001) -> 2S -> 6E -> 4S -> 2E -> S -> 2E -> D -> S */
            bot->nav_n = 0;
            bot_nav_queue( bot, "recall" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "south" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "east" );
            bot_nav_queue( bot, "down" );
            bot_nav_queue( bot, "south" );
        }
        break;
    case BOT_TRAINING:
        bot->state_timer = number_range( 5, 15 );     /* short burst, then back to grind */
        break;
    case BOT_RESTING:
        bot->state_timer = number_range( 60, 120 );    /* 2-3 minutes */
        break;
    case BOT_LOGGING_OUT:
        bot->state_timer = number_range( 3, 8 );      /* 3-8 seconds */
        break;
    default:
        bot->state_timer = number_range( 40, 120 );
        break;
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
 * Returns TRUE if the bot has exp worth spending on stats or class rank.
 */
static bool bot_should_train( CHAR_DATA *ch )
{
    int hp_cap = UMIN( 120000, 20000 + 4000 * ch->pkill );

    if ( ch->level == 2 && ch->max_hit >= 2000 )               return TRUE;
    if ( ch->level == 3 && ch->class == 0 )                     return TRUE;
    if ( ch->max_hit  < hp_cap         && ch->exp >= ch->max_hit  + 1 ) return TRUE;
    if ( ch->max_mana < ch->max_hit    && ch->max_mana < hp_cap
      && ch->exp >= ch->max_mana + 1 )                                   return TRUE;
    if ( ch->max_move < 10000          && ch->exp >= ch->max_move + 1 ) return TRUE;

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

    return FALSE;
}

/*
 * Execute one round of training commands.
 * Returns TRUE if at least one train command was issued.
 */
static bool bot_do_train( CHAR_DATA *ch )
{
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

/* Find a mob in the room that's safe to attack */
static CHAR_DATA *bot_find_mob_target( CHAR_DATA *ch )
{
    CHAR_DATA *victim;

    if ( ch->in_room == NULL ) return NULL;

    for ( victim = ch->in_room->people; victim != NULL; victim = victim->next_in_room )
    {
        if ( !IS_NPC(victim) )   continue;   /* Don't attack players */
        if ( victim->fighting )  continue;   /* Skip mobs already in combat */
        if ( victim->level > ch->level + 15 ) continue; /* Too strong */
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
        if ( bot_needs_rest(ch) )
        {
            bot_change_state( ch, bot, BOT_RESTING );
            return;
        }
        if ( bot_should_train(ch) )
        {
            bot_change_state( ch, bot, BOT_TRAINING );
            return;
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

static void bot_state_exploring( CHAR_DATA *ch, BOT_DATA *bot )
{
    if ( bot_needs_rest(ch) )
    {
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

    if ( bot_needs_rest(ch) )
    {
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
            /* Stance entry is handled by autodrop() in the combat engine */
            bot->grind_attempts = 0;
            return;
        }

        /* Between fights: buffs first, then any between-fight setup */
        if ( ai && ai->buff_check && ai->buff_check(ch) )
            return;   /* issued a buff command this tick */
        if ( ai && ai->between_fights && ai->between_fights(ch) )
            return;   /* issued a setup command this tick */
    }

    /* Find something to kill */
    victim = bot_find_mob_target( ch );
    if ( victim != NULL )
    {
        char cmd[MAX_INPUT_LENGTH];
        sprintf( cmd, "kill %s", victim->name );
        bot_cmd( ch, cmd );
        bot->grind_attempts = 0;
        return;
    }

    /* No targets here - move to find some */
    bot->grind_attempts++;
    if ( bot->grind_attempts > 3 )
    {
        bot_try_move( ch );
        bot->grind_attempts = 0;
    }

    if ( bot->state_timer <= 0 )
    {
        if ( bot_should_train(ch) )
            bot_change_state( ch, bot, BOT_TRAINING );
        else
            bot_change_state( ch, bot, BOT_IDLE );
    }
}

static void bot_state_resting( CHAR_DATA *ch, BOT_DATA *bot )
{
    /* Stand and fight back if attacked */
    if ( ch->position == POS_FIGHTING )
    {
        bot_change_state( ch, bot, BOT_IDLE );
        return;
    }

    /* Sit or sleep to recover */
    if ( ch->position == POS_STANDING )
    {
        if ( ch->hit < ch->max_hit / 2 )
            bot_cmd( ch, "sleep" );
        else
            bot_cmd( ch, "sit" );
    }

    /* Once recovered, stand up */
    if ( bot_is_healthy(ch) && bot->state_timer <= 0 )
    {
        if ( ch->position != POS_STANDING )
            bot_cmd( ch, "stand" );
        bot_change_state( ch, bot, BOT_IDLE );
    }
    else if ( bot->state_timer <= 0 && !bot_needs_rest(ch) )
    {
        if ( ch->position != POS_STANDING )
            bot_cmd( ch, "stand" );
        bot_change_state( ch, bot, BOT_IDLE );
    }
}

static void bot_state_training( CHAR_DATA *ch, BOT_DATA *bot )
{
    /* Abort if attacked */
    if ( ch->position == POS_FIGHTING )
    {
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
 * bot_ensure_geared - ensures the bot isn't naked, spawning gear if needed
 * ----------------------------------------------------------------------- */
static void bot_ensure_geared( CHAR_DATA *ch )
{
    int i;
    bool naked = TRUE;

    /* Don't try to gear up mid-combat */
    if ( ch->position == POS_FIGHTING )
        return;

    /* Check if any gear is worn */
    for ( i = 0; i < MAX_WEAR; i++ )
    {
        if ( get_eq_char( ch, i ) != NULL )
        {
            naked = FALSE;
            break;
        }
    }

    if ( naked )
    {
        /* Must be standing to wear equipment */
        if ( ch->position < POS_STANDING )
        {
            bot_cmd( ch, "wake" );
            bot_cmd( ch, "stand" );
        }

        /* Try to wear whatever is currently in inventory */
        bot_cmd( ch, "wear all" );

        /* Re-check if they managed to put anything on */
        for ( i = 0; i < MAX_WEAR; i++ )
        {
            if ( get_eq_char( ch, i ) != NULL )
            {
                naked = FALSE;
                break;
            }
        }

        if ( naked )
        {
            /* Still naked, must have lost it all! Spawn newbie pack and wear. */
            do_newbiepack( ch, "" );
            bot_cmd( ch, "wear all" );
        }
    }
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

    /* Make sure we are geared (must be done before timers that might return early) */
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
            return;   /* keep queue intact, retry after combat ends */
        }
        if ( ch->position < POS_STANDING )
        {
#if BOT_DEBUG
            log_string( "BOT_NAV: blocked - not standing, issuing stand" );
#endif
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
        return;
    }

    /* State dispatch */
    switch ( bot->state )
    {
    case BOT_IDLE:        bot_state_idle(       ch, bot ); break;
    case BOT_EXPLORING:   bot_state_exploring(  ch, bot ); break;
    case BOT_GRINDING:    bot_state_grinding(   ch, bot ); break;
    case BOT_TRAINING:    bot_state_training(   ch, bot ); break;
    case BOT_RESTING:     bot_state_resting(    ch, bot ); break;
    case BOT_LOGGING_OUT: bot_state_logging_out(ch, bot ); break;
    default:
        bot_change_state( ch, bot, BOT_IDLE );
        break;
    }
}
