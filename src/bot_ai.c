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

/* Forward declaration - defined in kav_fight.c */
void do_stance( CHAR_DATA *ch, char *argument );

/* -----------------------------------------------------------------------
 * bot_cmd - inject a command into a bot as if it typed it
 * ----------------------------------------------------------------------- */

void bot_cmd( CHAR_DATA *ch, const char *cmd )
{
    char buf[MAX_INPUT_LENGTH];
    strncpy( buf, cmd, sizeof(buf)-1 );
    buf[sizeof(buf)-1] = '\0';
    interpret( ch, buf );
}

/* Queue a navigation command to be executed before normal AI resumes */
static void bot_nav_queue( BOT_DATA *bot, const char *cmd )
{
    if ( bot->nav_n < 8 )
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
    bot->cmd_delay   = number_range( 1, 3 );

    switch ( new_state )
    {
    case BOT_IDLE:
        bot->state_timer = number_range( 10, 30 );    /* 10-30 seconds */
        break;
    case BOT_EXPLORING:
        bot->state_timer = number_range( 20, 60 );    /* 20-60 seconds */
        break;
    case BOT_GRINDING:
        bot->state_timer = number_range( 60, 180 );   /* 1-3 minutes */
        bot->grind_attempts = 0;
        /* Navigate to newbie area: recall -> up -> open door -> south */
        if ( ch->level <= 20 )
        {
            bot->nav_n = 0;
            bot_nav_queue( bot, "recall" );
            bot_nav_queue( bot, "up" );
            bot_nav_queue( bot, "open door" );
            bot_nav_queue( bot, "south" );
        }
        break;
    case BOT_RESTING:
        bot->state_timer = number_range( 20, 60 );    /* 20-60 seconds */
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

/*
 * Pick the basic stance (1-5: viper, crane, crab, mongoose, bull) that
 * the bot should train next - whichever has the lowest XP below 200.
 * Returns the stance index (1-5) or 0 if all are mastered.
 */
static int bot_pick_training_stance( CHAR_DATA *ch )
{
    /* Basic stance names match stance[] indices 1-5 */
    static const int  basic[]  = { 1, 2, 3, 4, 5 };
    static const char *names[] = { "viper", "crane", "crab", "mongoose", "bull", NULL };
    int best_idx  = 0;
    int best_xp   = 201;   /* sentinel: any unmastered beats this */
    int i;

    (void)names;   /* used via bot_cmd below */

    for ( i = 0; i < 5; i++ )
    {
        int xp = ch->stance[ basic[i] ];
        if ( xp < 0 ) xp = 0;   /* -1 means locked, treat as 0 */
        if ( xp < 200 && xp < best_xp )
        {
            best_xp  = xp;
            best_idx = i;
        }
    }
    if ( best_xp == 201 ) return 0;   /* all mastered */
    return best_idx + 1;              /* 1-based slot in basic[] */
}

/* Activate the best training stance via do_stance, or plain stance if all done */
static void bot_set_training_stance( CHAR_DATA *ch )
{
    static const char *names[] = { "viper", "crane", "crab", "mongoose", "bull" };
    int pick;

    /* Only change stance when relaxed (stance[0] == -1) */
    if ( ch->stance[0] != -1 ) return;

    pick = bot_pick_training_stance( ch );
    if ( pick == 0 )
    {
        /* All basics mastered - just use generic fighting stance */
        do_stance( ch, "" );
    }
    else
    {
        char buf[32];
        strncpy( buf, names[ pick - 1 ], sizeof(buf) - 1 );
        buf[ sizeof(buf) - 1 ] = '\0';
        do_stance( ch, buf );
    }
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

    /* Already fighting - make sure we have a stance active */
    if ( ch->position == POS_FIGHTING )
    {
        if ( ch->stance[0] == -1 )
            bot_set_training_stance( ch );   /* enter stance mid-fight */
        bot->grind_attempts = 0;
        return;
    }

    /* Between fights: relax stance so we can switch to the next
     * training stance before the next kill */
    if ( ch->stance[0] != -1 )
    {
        do_stance( ch, "" );   /* toggles back to relaxed (-1) */
        return;
    }

    /* Find something to kill, then adopt stance and engage */
    victim = bot_find_mob_target( ch );
    if ( victim != NULL )
    {
        char cmd[MAX_INPUT_LENGTH];
        bot_set_training_stance( ch );          /* choose stance first */
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
        bot_change_state( ch, bot, BOT_IDLE );
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
 * bot_update - main per-bot update, called each PULSE_BOT_MANAGER
 * ----------------------------------------------------------------------- */

void bot_update( CHAR_DATA *ch )
{
    BOT_DATA *bot;

    if ( ch == NULL || ch->pcdata == NULL ) return;
    bot = ch->pcdata->botdata;
    if ( bot == NULL ) return;

    /* Decrement timers */
    bot->state_timer--;
    if ( bot->cmd_delay > 0 ) { bot->cmd_delay--; return; }  /* Wait before acting */

    /* Reset cmd delay - human-like pause between commands */
    bot->cmd_delay = number_range( 1, 4 );

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
    case BOT_RESTING:     bot_state_resting(    ch, bot ); break;
    case BOT_LOGGING_OUT: bot_state_logging_out(ch, bot ); break;
    default:
        bot_change_state( ch, bot, BOT_IDLE );
        break;
    }
}
