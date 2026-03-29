/*
 * bot_chat.c - Bot chat system for Dystopia MUD
 *
 * Handles canned responses to greetings and questions, and generates
 * occasional unprompted chat messages. Future: hook in AI API here.
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
 * Response tables
 * ----------------------------------------------------------------------- */

/* Greetings */
static const char *greetings[] = {
    "hey",
    "hi",
    "hello",
    "sup",
    "heya",
    "yo",
    "howdy",
    NULL
};

static const char *greeting_responses[] = {
    "hey",
    "hi there",
    "sup",
    "yo",
    "heya",
    "what's up",
    "hey hey",
    "hiya",
    NULL
};

/* Farewells */
static const char *farewells[] = {
    "bye",
    "later",
    "cya",
    "goodbye",
    "farewell",
    "seeya",
    "peace",
    NULL
};

static const char *farewell_responses[] = {
    "later",
    "bye",
    "see ya",
    "peace",
    "take care",
    "cya",
    NULL
};

/* How are you */
static const char *howami_triggers[] = {
    "how are you",
    "how r u",
    "how you doing",
    "what's up",
    "whats up",
    NULL
};

static const char *howami_responses[] = {
    "not bad",
    "pretty good",
    "could be worse",
    "decent",
    "alive, which is something",
    "grinding away",
    NULL
};

/* Class questions */
static const char *class_triggers[] = {
    "what class",
    "what are you",
    "ur class",
    "your class",
    NULL
};

/* Generic unknowns */
static const char *generic_responses[] = {
    "hm",
    "yeah",
    "interesting",
    "...",
    "sure",
    "if you say so",
    NULL
};

/* Unprompted chat - things bots say randomly */
static const char *unprompted_says[] = {
    "anyone know good grinding spots?",
    "this place is wild",
    "ugh died again",
    "finally got that exp",
    "anyone wanna fight?",
    "quiet in here",
    "man i need better gear",
    "where is everyone",
    "been playing all day",
    "nice weather we're having",
    NULL
};

static const char *unprompted_chat[] = {
    "is the server laggy or just me",
    "anyone else grinding right now",
    "this game still has the best pvp",
    "looking for a group",
    "just hit a new exp record",
    NULL
};

/* Count entries in a NULL-terminated string array */
static int arr_len( const char **arr )
{
    int n = 0;
    while ( arr[n] != NULL ) n++;
    return n;
}

/* Pick a random entry from a NULL-terminated string array */
static const char *arr_random( const char **arr )
{
    int n = arr_len( arr );
    if ( n == 0 ) return NULL;
    return arr[number_range(0, n-1)];
}

/* -----------------------------------------------------------------------
 * bot_get_response - look up a canned response for a trigger phrase
 * ----------------------------------------------------------------------- */

const char *bot_get_response( const char *trigger, int chattiness )
{
    int i;

    /* Chattiness gate - quieter bots reply less often */
    if ( number_percent() > chattiness )
        return NULL;

    /* Check greetings */
    for ( i = 0; greetings[i] != NULL; i++ )
    {
        if ( !str_infix(greetings[i], trigger) )
            return arr_random( greeting_responses );
    }

    /* Check farewells */
    for ( i = 0; farewells[i] != NULL; i++ )
    {
        if ( !str_infix(farewells[i], trigger) )
            return arr_random( farewell_responses );
    }

    /* Check how-are-you */
    for ( i = 0; howami_triggers[i] != NULL; i++ )
    {
        if ( !str_infix(howami_triggers[i], trigger) )
            return arr_random( howami_responses );
    }

    /* Check class questions */
    for ( i = 0; class_triggers[i] != NULL; i++ )
    {
        if ( !str_infix(class_triggers[i], trigger) )
            return NULL;   /* Will be handled with class-specific reply */
    }

    /* Low-chance generic reply */
    if ( number_percent() < 15 )
        return arr_random( generic_responses );

    return NULL;
}

/* -----------------------------------------------------------------------
 * bot_hear_say - bot hears someone speak in the same room
 * ----------------------------------------------------------------------- */

void bot_hear_say( CHAR_DATA *bot, CHAR_DATA *speaker, char *msg )
{
    BOT_DATA   *bdata;
    const char *response;
    char        cmd[MAX_INPUT_LENGTH];
    int         chattiness;

    if ( bot == NULL || bot->pcdata == NULL ) return;
    bdata = bot->pcdata->botdata;
    if ( bdata == NULL ) return;

    chattiness = bdata->roster ? bdata->roster->chattiness : 50;

    /* Only respond to players, not other bots */
    if ( speaker != NULL && IS_NPC(speaker) ) return;
    if ( speaker != NULL && speaker->pcdata != NULL && speaker->pcdata->is_bot ) return;

    /* Check if the message contains the bot's name */
    bool name_mentioned = ( bot->name != NULL
                          && !str_infix(bot->name, msg) );

    /* Respond if name mentioned or by chattiness chance */
    if ( !name_mentioned && number_percent() > chattiness ) return;

    /* Handle class question separately for realism */
    {
        int i;
        for ( i = 0; class_triggers[i] != NULL; i++ )
        {
            if ( !str_infix(class_triggers[i], msg) )
            {
                if ( bot->class == 0 )
                    sprintf( cmd, "say still deciding on a class" );
                else if ( IS_CLASS(bot, CLASS_VAMPIRE) )
                    sprintf( cmd, "say vampire" );
                else if ( IS_CLASS(bot, CLASS_MONK) )
                    sprintf( cmd, "say monk" );
                else if ( IS_CLASS(bot, CLASS_NINJA) )
                    sprintf( cmd, "say ninja" );
                else if ( IS_CLASS(bot, CLASS_DEMON) )
                    sprintf( cmd, "say demon" );
                else
                    sprintf( cmd, "say nothing special" );
                interpret( bot, cmd );
                return;
            }
        }
    }

    response = bot_get_response( msg, chattiness );
    if ( response != NULL )
    {
        /* Human-like: respond with a short delay (handled by cmd_delay) */
        sprintf( cmd, "say %s", response );
        interpret( bot, cmd );
    }
}

/* -----------------------------------------------------------------------
 * bot_hear_tell - bot receives a private tell
 * ----------------------------------------------------------------------- */

void bot_hear_tell( CHAR_DATA *bot, CHAR_DATA *speaker, char *msg )
{
    BOT_DATA   *bdata;
    const char *response;
    char        cmd[MAX_INPUT_LENGTH];
    int         chattiness;

    if ( bot == NULL || bot->pcdata == NULL ) return;
    bdata = bot->pcdata->botdata;
    if ( bdata == NULL ) return;

    chattiness = bdata->roster ? bdata->roster->chattiness : 70;

    /* Always respond to tells (more personal) */
    response = bot_get_response( msg, chattiness + 20 );
    if ( response != NULL && speaker != NULL )
    {
        sprintf( cmd, "tell %s %s", speaker->name, response );
        interpret( bot, cmd );
    }
    else if ( speaker != NULL && number_percent() < 40 )
    {
        /* Fallback ack */
        const char *acks[] = { "yeah", "ok", "sure", "hm", "ok cool", NULL };
        sprintf( cmd, "tell %s %s", speaker->name, arr_random(acks) );
        interpret( bot, cmd );
    }
}

/* -----------------------------------------------------------------------
 * bot_unprompted_chat - bot says something random to break silence
 * ----------------------------------------------------------------------- */

void bot_unprompted_chat( CHAR_DATA *ch, BOT_DATA *bot )
{
    char        cmd[MAX_INPUT_LENGTH];
    int         chattiness;
    int         roll;

    if ( ch == NULL || bot == NULL ) return;
    chattiness = bot->roster ? bot->roster->chattiness : 50;

    if ( number_percent() > chattiness ) return;

    roll = number_range( 1, 3 );
    switch ( roll )
    {
    case 1:
        /* say something in the room */
        sprintf( cmd, "say %s", arr_random(unprompted_says) );
        interpret( ch, cmd );
        break;
    case 2:
        /* chat channel */
        sprintf( cmd, "chat %s", arr_random(unprompted_chat) );
        interpret( ch, cmd );
        break;
    case 3:
        /* emote */
        {
            const char *emotes[] = { "yawn", "grin", "sigh", "nod", "scratch", NULL };
            interpret( ch, (char *)arr_random(emotes) );
        }
        break;
    }
}
