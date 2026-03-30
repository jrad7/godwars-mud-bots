/*
 * bot_mgr.c - Bot population manager for Dystopia MUD
 *
 * Manages a roster of bot characters that log in/out like real players.
 * Bots use fake descriptors (descriptor = BOT_DESCRIPTOR_SENTINEL) so
 * no real socket is needed. Commands are injected via interpret().
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

/* Global bot roster */
BOT_ROSTER_ENTRY bot_roster[MAX_BOT_ROSTER];
int              bot_roster_count  = 0;
static bool      bot_roster_dirty  = FALSE;

/*
 * Default bot roster - used if bots.txt doesn't exist yet.
 * Mix of classes, lifespans, and personalities.
 */
static const struct {
    const char *name;
    int         class_pref;
    int         lifespan;
    int         chattiness;
    int         aggression;
    int         explorer;
} bot_defaults[] = {
    { "Krael",   BOT_CLASS_VAMPIRE, BOT_LIFE_PERMANENT, 60, 80, 40 },
    { "Sylas",   BOT_CLASS_MONK,    BOT_LIFE_PERMANENT, 80, 40, 60 },
    { "Vex",     BOT_CLASS_NINJA,   BOT_LIFE_LONG,      30, 90, 50 },
    { "Zara",    BOT_CLASS_VAMPIRE, BOT_LIFE_PERMANENT, 70, 50, 80 },
    { "Mordain", BOT_CLASS_DEMON,   BOT_LIFE_LONG,      40, 90, 30 },
    { "Thresh",  BOT_CLASS_MONK,    BOT_LIFE_SHORT,     80, 30, 50 },
    { "Kira",    BOT_CLASS_NINJA,   BOT_LIFE_PERMANENT, 50, 60, 90 },
    { "Nyx",     BOT_CLASS_DEMON,   BOT_LIFE_LONG,      30, 85, 40 },
    { "Draven",  BOT_CLASS_VAMPIRE, BOT_LIFE_SHORT,     70, 50, 40 },
    { "Rael",    BOT_CLASS_MONK,    BOT_LIFE_PERMANENT, 60, 40, 70 },
    { "Soren",   BOT_CLASS_NINJA,   BOT_LIFE_LONG,      70, 60, 50 },
    { "Mira",    BOT_CLASS_DEMON,   BOT_LIFE_SHORT,     80, 30, 70 },
    { NULL }
};

/* -----------------------------------------------------------------------
 * Roster persistence
 * ----------------------------------------------------------------------- */

void load_bot_roster( void )
{
    FILE *fp;
    char  buf[64];
    int   i;

    if ( ( fp = fopen( "../txt/bots.txt", "r" ) ) != NULL )
    {
        bot_roster_count = 0;
        while ( fscanf( fp, "%63s", buf ) == 1
             && bot_roster_count < MAX_BOT_ROSTER )
        {
            if ( !str_cmp( buf, "#BOT" ) )
            {
                BOT_ROSTER_ENTRY *r = &bot_roster[bot_roster_count];
                char key[64];
                memset( r, 0, sizeof(*r) );

                while ( fscanf( fp, "%63s", key ) == 1
                     && str_cmp( key, "End" ) )
                {
                    if      ( !str_cmp(key,"Name")         ) fscanf(fp,"%19s", r->name);
                    else if ( !str_cmp(key,"ClassPref")    ) fscanf(fp,"%d",  &r->class_pref);
                    else if ( !str_cmp(key,"Lifespan")     ) fscanf(fp,"%d",  &r->lifespan);
                    else if ( !str_cmp(key,"Chattiness")   ) fscanf(fp,"%d",  &r->chattiness);
                    else if ( !str_cmp(key,"Aggression")   ) fscanf(fp,"%d",  &r->aggression);
                    else if ( !str_cmp(key,"Explorer")     ) fscanf(fp,"%d",  &r->explorer);
                    else if ( !str_cmp(key,"TotalPlaytime")) fscanf(fp,"%d",  &r->total_playtime);
                    else if ( !str_cmp(key,"OfflineUntil") ) fscanf(fp,"%ld", (long*)&r->offline_until);
                    else if ( !str_cmp(key,"Retired")      ) { int v=0; fscanf(fp,"%d",&v); r->retired=(bool)v; }
                }
                r->online = FALSE;
                bot_roster_count++;
            }
        }
        fclose( fp );

        if ( bot_roster_count > 0 )
        {
            sprintf( log_buf, "Bot: loaded %d roster entries.", bot_roster_count );
            log_string( log_buf );
            return;
        }
    }

    /* No file or empty - seed from defaults */
    bot_roster_count = 0;
    for ( i = 0; bot_defaults[i].name != NULL && i < MAX_BOT_ROSTER; i++ )
    {
        BOT_ROSTER_ENTRY *r = &bot_roster[bot_roster_count++];
        memset( r, 0, sizeof(*r) );
        strncpy( r->name,       bot_defaults[i].name,      sizeof(r->name)-1 );
        r->class_pref   = bot_defaults[i].class_pref;
        r->lifespan     = bot_defaults[i].lifespan;
        r->chattiness   = bot_defaults[i].chattiness;
        r->aggression   = bot_defaults[i].aggression;
        r->explorer     = bot_defaults[i].explorer;
        r->retired      = FALSE;
        r->online       = FALSE;
        r->offline_until = 0;
    }

    save_bot_roster();
    sprintf( log_buf, "Bot: created default roster (%d bots).", bot_roster_count );
    log_string( log_buf );
}

void save_bot_roster( void )
{
    FILE *fp;
    int   i;

    if ( ( fp = fopen( "../txt/bots.txt", "w" ) ) == NULL )
    {
        bug( "save_bot_roster: cannot open ../txt/bots.txt", 0 );
        return;
    }

    for ( i = 0; i < bot_roster_count; i++ )
    {
        BOT_ROSTER_ENTRY *r = &bot_roster[i];
        fprintf( fp, "#BOT\n" );
        fprintf( fp, "Name          %s\n",   r->name );
        fprintf( fp, "ClassPref     %d\n",   r->class_pref );
        fprintf( fp, "Lifespan      %d\n",   r->lifespan );
        fprintf( fp, "Chattiness    %d\n",   r->chattiness );
        fprintf( fp, "Aggression    %d\n",   r->aggression );
        fprintf( fp, "Explorer      %d\n",   r->explorer );
        fprintf( fp, "TotalPlaytime %d\n",   r->total_playtime );
        fprintf( fp, "OfflineUntil  %ld\n",  (long)r->offline_until );
        fprintf( fp, "Retired       %d\n",   (int)r->retired );
        fprintf( fp, "End\n\n" );
    }

    fclose( fp );
    bot_roster_dirty = FALSE;
}

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static int count_online_bots( void )
{
    int count = 0, i;
    for ( i = 0; i < bot_roster_count; i++ )
        if ( bot_roster[i].online ) count++;
    return count;
}

/* -----------------------------------------------------------------------
 * bot_login - bring a bot into the game
 * ----------------------------------------------------------------------- */

bool bot_login( BOT_ROSTER_ENTRY *roster )
{
    static PC_DATA   pcdata_zero;
    DESCRIPTOR_DATA *d;
    CHAR_DATA       *ch;
    BOT_DATA        *bot;
    bool            is_new_bot = FALSE;

    if ( descriptor_free == NULL )
        d = alloc_perm( sizeof(*d) );
    else
    {
        d               = descriptor_free;
        descriptor_free = descriptor_free->next;
    }

    init_descriptor( d, BOT_DESCRIPTOR_SENTINEL );
    d->host          = str_dup( "bot" );
    d->lookup_status = STATUS_DONE;   /* no DNS lookup */

    if ( load_char_obj( d, roster->name ) )
    {
        ch = d->character;
    }
    else
    {
        is_new_bot = TRUE;

        if ( char_free == NULL )
            ch = alloc_perm( sizeof(*ch) );
        else
        {
            ch        = char_free;
            char_free = char_free->next;
        }
        clear_char( ch );

        if ( pcdata_free == NULL )
            ch->pcdata = alloc_perm( sizeof(*ch->pcdata) );
        else
        {
            ch->pcdata  = pcdata_free;
            pcdata_free = pcdata_free->next;
        }
        *ch->pcdata = pcdata_zero;

        d->character             = ch;
        ch->desc                 = d;
        ch->name                 = str_dup( roster->name );
        ch->pcdata->switchname   = str_dup( roster->name );
        ch->act                  = PLR_BLANK | PLR_COMBINE | PLR_PROMPT | PLR_HOLYLIGHT;
        ch->pcdata->board        = &boards[DEFAULT_BOARD];

        /* Basic stats */
        ch->sex                  = number_range( 1, 2 );
        ch->level                = 1;
        ch->trust                = 0;
        ch->pcdata->perm_str     = number_range( 12, 16 );
        ch->pcdata->perm_int     = number_range( 12, 16 );
        ch->pcdata->perm_wis     = number_range( 12, 16 );
        ch->pcdata->perm_dex     = number_range( 12, 16 );
        ch->pcdata->perm_con     = number_range( 12, 16 );
        ch->max_hit              = 5000;
        ch->hit                  = ch->max_hit;
        ch->max_mana             = 5000;
        ch->mana                 = ch->max_mana;
        ch->max_move             = 5000;
        ch->move                 = ch->max_move;
        ch->gold                 = number_range( 100, 500 );
        ch->pcdata->condition[0] = 48;
        ch->pcdata->condition[1] = 48;

        /* Required strings */
        ch->short_descr  = str_dup( "" );
        ch->long_descr   = str_dup( "" );
        ch->description  = str_dup( "" );
        ch->lord         = str_dup( "" );
        ch->morph        = str_dup( "" );
        ch->poweraction  = str_dup( "" );
        ch->powertype    = str_dup( "" );
        ch->prompt       = str_dup( "" );
        ch->cprompt      = str_dup( "" );
        ch->pload        = str_dup( "" );
        ch->pcdata->pwd           = str_dup( "" );
        ch->pcdata->title         = str_dup( "" );
        ch->pcdata->bamfin        = str_dup( "" );
        ch->pcdata->bamfout       = str_dup( "" );
        ch->pcdata->conception    = str_dup( "" );
        ch->pcdata->parents       = str_dup( "" );
        ch->pcdata->cparents      = str_dup( "" );
        ch->pcdata->marriage      = str_dup( "" );
        ch->pcdata->loginmessage  = str_dup( "" );
        ch->pcdata->logoutmessage = str_dup( "" );
        ch->pcdata->avatarmessage = str_dup( "" );
        ch->pcdata->decapmessage  = str_dup( "" );
        ch->pcdata->tiemessage    = str_dup( "" );

        /* Create time strings */
        {
            char *ct = ctime( &current_time );
            ct[strlen(ct)-1] = '\0';      /* strip newline */
            ch->createtime = str_dup( ct );
            ch->lasttime   = str_dup( ct );
        }
        ch->lasthost             = str_dup( "bot" );
        ch->hunting              = str_dup( "" );
        ch->pcdata->last_decap[0] = str_dup( "" );
        ch->pcdata->last_decap[1] = str_dup( "" );

        {
            ROOM_INDEX_DATA *start_room = get_room_index( ROOM_VNUM_SCHOOL );
            if ( start_room == NULL ) start_room = get_room_index( ROOM_VNUM_LIMBO );
            char_to_room( ch, start_room );
        }

        set_learnable_disciplines( ch );
        ch->form = 1048575;
        do_newbiepack( ch, "" );
        do_wear( ch, "all" );

#if BOT_DEBUG
        {
            OBJ_DATA *obj;
            char buf[MAX_STRING_LENGTH];
            int count_eq = 0, count_inv = 0;
            sprintf( buf, "BOT DEBUG: %s EQ=[", ch->name );
            for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
            {
                if ( obj->wear_loc != WEAR_NONE ) { strcat( buf, obj->short_descr ); strcat( buf, ", " ); count_eq++; }
            }
            if (count_eq == 0) strcat( buf, "none" );
            strcat( buf, "] INV=[" );
            for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
            {
                if ( obj->wear_loc == WEAR_NONE ) { strcat( buf, obj->short_descr ); strcat( buf, ", " ); count_inv++; }
            }
            if (count_inv == 0) strcat( buf, "none" );
            strcat( buf, "]" );
            log_string( buf );
        }
#endif

        save_char_obj( ch );
    }

    /* Add to char_list for both NEW and RETURNING bots */
    ch->next  = char_list;
    char_list = ch;

    ch->pcdata->is_bot = TRUE;
    if ( !IS_SET(ch->extra, EXTRA_TRUSTED) )
        SET_BIT( ch->extra, EXTRA_TRUSTED );

    bot = (BOT_DATA *)alloc_mem( sizeof(BOT_DATA) );
    memset( bot, 0, sizeof(BOT_DATA) );
    bot->roster          = roster;
    bot->cmd_delay       = number_range(  1, 4 );
    bot->session_start   = current_time;
    bot->session_max     = number_range( BOT_SESSION_MIN, BOT_SESSION_MAX );
    bot->idle_chat_timer = number_range( 30, 120 );
    ch->pcdata->botdata  = bot;

    /* Start grinding immediately so the nav queue fires right away */
    bot_change_state( ch, bot, BOT_GRINDING );

    d->connected  = CON_PLAYING;
    d->next       = descriptor_list;
    descriptor_list = d;

    if ( ch->in_room == NULL )
    {
        ROOM_INDEX_DATA *dest = NULL;
        if ( ch->home > 0 ) dest = get_room_index( ch->home );
        if ( dest == NULL ) dest = get_room_index( ROOM_VNUM_SCHOOL );
        if ( dest == NULL ) dest = get_room_index( ROOM_VNUM_LIMBO );
        char_to_room( ch, dest );
    }
    else if ( !is_new_bot )
    {
        char_to_room( ch, ch->in_room );
    }

    /* Ensure the bot has a valid physical form (hands, etc) to wear gear */
    if ( ch->form == 0 )
        ch->form = 1048575;

    /* Sanity check: if bot loaded from file but has no gear, give them a newbie pack */
    if ( ch->carrying == NULL && ch->level == 1 )
    {
        do_newbiepack( ch, "" );
    }
    
    /* Always attempt to wear any unequipped gear upon login */
    do_wear( ch, "all" );

#if BOT_DEBUG
    {
        OBJ_DATA *obj;
        char buf[MAX_STRING_LENGTH];
        int count_eq = 0, count_inv = 0;
        sprintf( buf, "BOT DEBUG: %s (Loaded) EQ=[", ch->name );
        for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
        {
            if ( obj->wear_loc != WEAR_NONE ) { strcat( buf, obj->short_descr ); strcat( buf, ", " ); count_eq++; }
        }
        if (count_eq == 0) strcat( buf, "none" );
        strcat( buf, "] INV=[" );
        for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
        {
            if ( obj->wear_loc == WEAR_NONE ) { strcat( buf, obj->short_descr ); strcat( buf, ", " ); count_inv++; }
        }
        if (count_inv == 0) strcat( buf, "none" );
        strcat( buf, "]" );
        log_string( buf );
    }
#endif

    /* Ensure recall destination is set - new chars have home=0 */
    if ( ch->home == 0 )
        ch->home = 3001;

    ch->logon = current_time;
    roster->online = TRUE;

    /* World entrance notification, same as nanny() does for real players */
    {
        char buf[MAX_STRING_LENGTH];
        if ( ch->level <= 6 )
        {
            if ( !ragnarok )
                sprintf( buf, "#2%s #7enters #R%s.#n", ch->name, MUDNAME );
            else
                sprintf( buf, "#2%s #7enters #R%s #y(#0Ragnarok#y).#n", ch->name, MUDNAME );
            enter_info( buf );
        }
        act( "$n has entered the game.", ch, NULL, NULL, TO_ROOM );
    }

    sprintf( log_buf, "Bot login: %s", ch->name );
    log_string( log_buf );

    return TRUE;
}

/* -----------------------------------------------------------------------
 * bot_logout - gracefully remove a bot from the game
 * ----------------------------------------------------------------------- */

void bot_logout( CHAR_DATA *ch )
{
    BOT_DATA *bot = ch->pcdata->botdata;
    int       playtime;

    if ( bot == NULL ) return;

    /* Update roster stats before character is freed */
    if ( bot->roster != NULL )
    {
        playtime = (int)( current_time - bot->session_start );
        bot->roster->total_playtime += playtime;
        bot->roster->online          = FALSE;
        bot->roster->offline_until   = current_time
            + (time_t)number_range( BOT_OFFLINE_MIN, BOT_OFFLINE_MAX );

        /* Retirement check */
        switch ( bot->roster->lifespan )
        {
        case BOT_LIFE_SHORT:
            if ( bot->roster->total_playtime >= BOT_RETIRE_SHORT )
                bot->roster->retired = TRUE;
            break;
        case BOT_LIFE_LONG:
            if ( bot->roster->total_playtime >= BOT_RETIRE_LONG )
                bot->roster->retired = TRUE;
            break;
        default:
            break;
        }
    }

    bot_roster_dirty = TRUE;

    /* Stop fighting if we are */
    if ( ch->position == POS_FIGHTING )
    {
        do_flee( ch, "" );
        ch->fighting = NULL;
    }

    ch->fight_timer = 0;   /* prevent do_quit silent block */
    do_quit( ch, "" );
    /* ch is freed after this - do not touch */
}

/* -----------------------------------------------------------------------
 * bot_manager_update - called every PULSE_BOT_MANAGER from update_handler
 * ----------------------------------------------------------------------- */

void bot_ai_tick( void )
{
    CHAR_DATA *ch, *ch_next;
    for ( ch = char_list; ch != NULL; ch = ch_next )
    {
        ch_next = ch->next;
        if ( IS_NPC(ch) || !ch->pcdata->is_bot ) continue;
        bot_update( ch );
    }
}

void bot_manager_update( void )
{
    CHAR_DATA *ch, *ch_next;
    int i, online, target;

    /* Reset all online flags, then re-confirm from char_list in one pass */
    for ( i = 0; i < bot_roster_count; i++ )
        bot_roster[i].online = FALSE;

    for ( ch = char_list; ch != NULL; ch = ch_next )
    {
        ch_next = ch->next;
        if ( IS_NPC(ch) || !ch->pcdata->is_bot ) continue;

        BOT_DATA *bot = ch->pcdata->botdata;
        if ( bot == NULL ) continue;

        if ( bot->roster != NULL )
            bot->roster->online = TRUE;

        /* Session timeout */
        if ( (int)(current_time - bot->session_start) >= bot->session_max
          && ch->position != POS_FIGHTING )
        {
            bot_change_state( ch, bot, BOT_LOGGING_OUT );
        }
    }

    /* Flush roster changes from any logouts this cycle */
    if ( bot_roster_dirty )
        save_bot_roster();

    /* Adjust population toward target */
    online = count_online_bots();
    target = number_range( BOT_MIN_ONLINE, BOT_MAX_ONLINE );

    if ( online < target )
    {
        for ( i = 0; i < bot_roster_count; i++ )
        {
            BOT_ROSTER_ENTRY *r = &bot_roster[i];
            if ( r->retired || r->online ) continue;
            if ( r->offline_until != 0 && current_time < r->offline_until ) continue;

            if ( bot_login(r) )
                break;   /* one at a time to stagger logins */
        }
    }
}
