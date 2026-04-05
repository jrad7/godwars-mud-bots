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

int              global_bot_pvp_mode = BOT_PVP_MODE_NORMAL;

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
    /* --- Permanent roster: world fixtures, always come back --- */
    { "Krael",   BOT_CLASS_VAMPIRE, BOT_LIFE_PERMANENT, 60, 80, 40 },
    { "Sylas",   BOT_CLASS_MONK,    BOT_LIFE_PERMANENT, 80, 40, 60 },
    { "Zara",    BOT_CLASS_VAMPIRE, BOT_LIFE_PERMANENT, 70, 50, 80 },
    { "Kira",    BOT_CLASS_NINJA,   BOT_LIFE_PERMANENT, 50, 60, 90 },
    { "Rael",    BOT_CLASS_MONK,    BOT_LIFE_PERMANENT, 60, 40, 70 },
    { "Arix",    BOT_CLASS_DEMON,   BOT_LIFE_PERMANENT, 40, 75, 35 },
    { "Caius",   BOT_CLASS_VAMPIRE, BOT_LIFE_PERMANENT, 55, 65, 55 },
    { "Eris",    BOT_CLASS_NINJA,   BOT_LIFE_PERMANENT, 75, 55, 65 },
    { "Grix",    BOT_CLASS_MONK,    BOT_LIFE_PERMANENT, 45, 45, 80 },
    { "Jade",    BOT_CLASS_DEMON,   BOT_LIFE_PERMANENT, 85, 35, 50 },
    { "Lorn",    BOT_CLASS_VAMPIRE, BOT_LIFE_PERMANENT, 30, 85, 30 },
    { "Mael",    BOT_CLASS_MONK,    BOT_LIFE_PERMANENT, 70, 50, 60 },
    { "Oryn",    BOT_CLASS_NINJA,   BOT_LIFE_PERMANENT, 60, 70, 45 },
    { "Reth",    BOT_CLASS_DEMON,   BOT_LIFE_PERMANENT, 50, 80, 40 },
    { "Skai",    BOT_CLASS_VAMPIRE, BOT_LIFE_PERMANENT, 65, 60, 70 },
    { "Thorn",   BOT_CLASS_MONK,    BOT_LIFE_PERMANENT, 35, 90, 25 },
    { "Vael",    BOT_CLASS_NINJA,   BOT_LIFE_PERMANENT, 80, 40, 55 },
    { "Wrex",    BOT_CLASS_DEMON,   BOT_LIFE_PERMANENT, 25, 95, 20 },
    { "Ashe",    BOT_CLASS_VAMPIRE, BOT_LIFE_PERMANENT, 90, 30, 75 },
    { "Cael",    BOT_CLASS_MONK,    BOT_LIFE_PERMANENT, 55, 55, 55 },
    { "Zilvra",  BOT_CLASS_DROW,      BOT_LIFE_PERMANENT, 40, 70, 50 },
    { "Drisnil", BOT_CLASS_DROW,      BOT_LIFE_PERMANENT, 55, 60, 45 },
    { "Garak",   BOT_CLASS_WEREWOLF,  BOT_LIFE_PERMANENT, 35, 75, 55 },
    { "Fenris",  BOT_CLASS_WEREWOLF,  BOT_LIFE_PERMANENT, 50, 65, 40 },
    { "Xolvrae", BOT_CLASS_DROW,      BOT_LIFE_PERMANENT, 30, 80, 40 },
    { "Nathrae", BOT_CLASS_DROW,      BOT_LIFE_PERMANENT, 65, 55, 65 },
    { "Valdris", BOT_CLASS_DROW,      BOT_LIFE_PERMANENT, 50, 65, 35 },
    { "Torval",  BOT_CLASS_WEREWOLF,  BOT_LIFE_PERMANENT, 45, 70, 65 },
    { "Hakon",   BOT_CLASS_WEREWOLF,  BOT_LIFE_PERMANENT, 25, 85, 30 },
    { "Bjarga",  BOT_CLASS_WEREWOLF,  BOT_LIFE_PERMANENT, 70, 60, 50 },

    /* --- Long-lived roster: stay weeks before retiring --- */
    { "Vex",     BOT_CLASS_NINJA,   BOT_LIFE_LONG,      30, 90, 50 },
    { "Mordain", BOT_CLASS_DEMON,   BOT_LIFE_LONG,      40, 90, 30 },
    { "Nyx",     BOT_CLASS_DEMON,   BOT_LIFE_LONG,      30, 85, 40 },
    { "Soren",   BOT_CLASS_NINJA,   BOT_LIFE_LONG,      70, 60, 50 },
    { "Bael",    BOT_CLASS_VAMPIRE, BOT_LIFE_LONG,      50, 70, 60 },
    { "Dex",     BOT_CLASS_NINJA,   BOT_LIFE_LONG,      40, 80, 55 },
    { "Fane",    BOT_CLASS_DEMON,   BOT_LIFE_LONG,      60, 65, 45 },
    { "Haze",    BOT_CLASS_VAMPIRE, BOT_LIFE_LONG,      75, 45, 70 },
    { "Ivar",    BOT_CLASS_MONK,    BOT_LIFE_LONG,      45, 60, 65 },
    { "Jael",    BOT_CLASS_NINJA,   BOT_LIFE_LONG,      65, 55, 50 },
    { "Keth",    BOT_CLASS_DEMON,   BOT_LIFE_LONG,      35, 85, 35 },
    { "Nexx",    BOT_CLASS_VAMPIRE, BOT_LIFE_LONG,      55, 75, 45 },
    { "Pax",     BOT_CLASS_MONK,    BOT_LIFE_LONG,      80, 25, 80 },
    { "Seraph",  BOT_CLASS_VAMPIRE, BOT_LIFE_LONG,      70, 40, 75 },
    { "Tyral",   BOT_CLASS_NINJA,   BOT_LIFE_LONG,      60, 65, 60 },
    { "Ulric",   BOT_CLASS_DEMON,   BOT_LIFE_LONG,      45, 80, 30 },
    { "Vynn",    BOT_CLASS_MONK,    BOT_LIFE_LONG,      75, 35, 70 },
    { "Xan",     BOT_CLASS_VAMPIRE, BOT_LIFE_LONG,      50, 60, 85 },
    { "Yrel",    BOT_CLASS_NINJA,   BOT_LIFE_LONG,      85, 45, 55 },
    { "Zeth",    BOT_CLASS_DEMON,   BOT_LIFE_LONG,      40, 75, 50 },
    { "Vrae",    BOT_CLASS_DROW,      BOT_LIFE_LONG,      60, 65, 55 },
    { "Ilvaen",  BOT_CLASS_DROW,      BOT_LIFE_LONG,      50, 75, 50 },
    { "Szoryn",  BOT_CLASS_DROW,      BOT_LIFE_LONG,      35, 80, 45 },
    { "Quelzar", BOT_CLASS_DROW,      BOT_LIFE_LONG,      75, 60, 60 },
    { "Delvrae", BOT_CLASS_DROW,      BOT_LIFE_LONG,      45, 70, 70 },
    { "Lykan",   BOT_CLASS_WEREWOLF,  BOT_LIFE_LONG,      45, 80, 60 },
    { "Ulfgar",  BOT_CLASS_WEREWOLF,  BOT_LIFE_LONG,      30, 90, 35 },
    { "Sigrun",  BOT_CLASS_WEREWOLF,  BOT_LIFE_LONG,      65, 60, 65 },
    { "Mordak",  BOT_CLASS_WEREWOLF,  BOT_LIFE_LONG,      40, 85, 40 },
    { "Skaldi",  BOT_CLASS_WEREWOLF,  BOT_LIFE_LONG,      55, 70, 55 },

    /* --- Short-lived roster: cycle through, retire fast --- */
    { "Thresh",  BOT_CLASS_MONK,    BOT_LIFE_SHORT,     80, 30, 50 },
    { "Draven",  BOT_CLASS_VAMPIRE, BOT_LIFE_SHORT,     70, 50, 40 },
    { "Mira",    BOT_CLASS_DEMON,   BOT_LIFE_SHORT,     80, 30, 70 },
    { "Bran",    BOT_CLASS_NINJA,   BOT_LIFE_SHORT,     50, 75, 60 },
    { "Dusk",    BOT_CLASS_VAMPIRE, BOT_LIFE_SHORT,     65, 55, 50 },
    { "Emyr",    BOT_CLASS_MONK,    BOT_LIFE_SHORT,     90, 20, 85 },
    { "Fex",     BOT_CLASS_DEMON,   BOT_LIFE_SHORT,     30, 90, 40 },
    { "Gael",    BOT_CLASS_NINJA,   BOT_LIFE_SHORT,     55, 65, 70 },
    { "Hex",     BOT_CLASS_VAMPIRE, BOT_LIFE_SHORT,     40, 80, 45 },
    { "Krix",    BOT_CLASS_MONK,    BOT_LIFE_SHORT,     70, 40, 65 },
    { "Luxe",    BOT_CLASS_DEMON,   BOT_LIFE_SHORT,     85, 35, 55 },
    { "Naxi",    BOT_CLASS_NINJA,   BOT_LIFE_SHORT,     60, 60, 75 },
    { "Omix",    BOT_CLASS_VAMPIRE, BOT_LIFE_SHORT,     45, 70, 50 },
    { "Prex",    BOT_CLASS_MONK,    BOT_LIFE_SHORT,     75, 45, 60 },
    { "Quen",    BOT_CLASS_DEMON,   BOT_LIFE_SHORT,     55, 65, 45 },
    { "Rex",     BOT_CLASS_NINJA,   BOT_LIFE_SHORT,     35, 85, 35 },
    { "Urax",    BOT_CLASS_VAMPIRE, BOT_LIFE_SHORT,     80, 40, 65 },
    { "Wex",     BOT_CLASS_MONK,    BOT_LIFE_SHORT,     50, 55, 80 },
    { "Sylvrix", BOT_CLASS_DROW,      BOT_LIFE_SHORT,     70, 55, 60 },
    { "Chal",    BOT_CLASS_DROW,      BOT_LIFE_SHORT,     60, 65, 55 },
    { "Belryn",  BOT_CLASS_DROW,      BOT_LIFE_SHORT,     80, 50, 70 },
    { "Nizzre",  BOT_CLASS_DROW,      BOT_LIFE_SHORT,     45, 75, 45 },
    { "Ragna",   BOT_CLASS_WEREWOLF,  BOT_LIFE_SHORT,     60, 70, 50 },
    { "Skoll",   BOT_CLASS_WEREWOLF,  BOT_LIFE_SHORT,     35, 85, 45 },
    { "Hati",    BOT_CLASS_WEREWOLF,  BOT_LIFE_SHORT,     50, 80, 35 },
    { "Wulfric", BOT_CLASS_WEREWOLF,  BOT_LIFE_SHORT,     70, 65, 60 },
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
        ch->pcdata->disc_research = -1;   /* 0 from zero-init is not the sentinel; -1 = none */

        d->character             = ch;
        ch->desc                 = d;
        ch->name                 = str_dup( roster->name );
        ch->pcdata->switchname   = str_dup( roster->name );
        ch->act                  = PLR_BLANK | PLR_COMBINE | PLR_PROMPT | PLR_HOLYLIGHT | PLR_BRIEF2 | PLR_BRIEF3;
        ch->pcdata->board        = &boards[DEFAULT_BOARD];

        /* Basic stats */
        ch->sex                  = number_range( 1, 2 );
        ch->level                = 1;
        ch->trust                = 0;
        ch->pcdata->perm_str     = number_range( 10, 16 );
        ch->pcdata->perm_int     = number_range( 10, 16 );
        ch->pcdata->perm_wis     = number_range( 10, 16 );
        ch->pcdata->perm_dex     = number_range( 10, 16 );
        ch->pcdata->perm_con     = number_range( 10, 16 );
        /* max_hit/mana/move left at clear_char defaults (1000/1500/1500) */
        ch->gold                 = number_range( 100, 500 );
        ch->generation           = 6;
        /* Mastery stances must be -1 (unlearned), not 0 — getMight checks != -1 */
        ch->stance[19]           = -1;
        ch->stance[20]           = -1;
        ch->stance[21]           = -1;
        ch->stance[22]           = -1;
        ch->stance[23]           = -1;
        ch->pcdata->condition[COND_DRUNK]  = 0;
        ch->pcdata->condition[COND_FULL]   = 48;
        ch->pcdata->condition[COND_THIRST] = 48;

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

    }

    /* Add to char_list for both NEW and RETURNING bots */
    ch->next  = char_list;
    char_list = ch;

    ch->pcdata->is_bot = TRUE;
    if ( !IS_SET(ch->extra, EXTRA_TRUSTED) )
        SET_BIT( ch->extra, EXTRA_TRUSTED );
    if ( !IS_SET(ch->act, PLR_AUTOSAC) )
        SET_BIT( ch->act, PLR_AUTOSAC );

    bot = (BOT_DATA *)alloc_mem( sizeof(BOT_DATA) );
    memset( bot, 0, sizeof(BOT_DATA) );
    bot->roster          = roster;
    bot->cmd_delay       = number_range(  1, 2 );
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

    save_char_obj( ch );

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

    /* If a player is waiting in auto-watch mode with no bot to watch, assign them now */
    if ( ch->desc != NULL )
    {
        CHAR_DATA *wch;
        for ( wch = char_list; wch != NULL; wch = wch->next )
        {
            bool already_watching = FALSE;
            DESCRIPTOR_DATA *d_scan;

            if ( IS_NPC(wch) || wch->pcdata == NULL ) continue;
            if ( !wch->pcdata->bot_watch_any ) continue;
            if ( wch->desc == NULL ) continue;

            for ( d_scan = descriptor_list; d_scan != NULL; d_scan = d_scan->next )
            {
                if ( d_scan->snoop_by == wch->desc )
                {
                    already_watching = TRUE;
                    break;
                }
            }
            if ( already_watching ) continue;

            /* Only assign if the bot is not already being watched */
            if ( ch->desc->snoop_by != NULL ) break;
            ch->desc->snoop_by = wch->desc;
            act( "Now watching $N.", wch, NULL, ch, TO_CHAR );
            break;
        }
    }

    return TRUE;
}

/* -----------------------------------------------------------------------
 * bot_watch_assign_random - point a watcher's snoop to a random available bot
 *
 * Scans char_list for bots with a sentinel descriptor and no current watcher.
 * Picks one at random (reservoir sample) and sets its snoop_by.
 * Returns TRUE and sends a notice to the watcher if a bot was found.
 * ----------------------------------------------------------------------- */
bool bot_watch_assign_random( CHAR_DATA *watcher, CHAR_DATA *skip )
{
    CHAR_DATA *ch;
    CHAR_DATA *pick = NULL;
    int        count = 0;

    if ( watcher->desc == NULL )
        return FALSE;

    for ( ch = char_list; ch != NULL; ch = ch->next )
    {
        if ( IS_NPC(ch) || !ch->pcdata->is_bot ) continue;
        if ( ch == skip ) continue;
        if ( ch->desc == NULL ) continue;
        if ( ch->desc->descriptor != BOT_DESCRIPTOR_SENTINEL ) continue;
        if ( ch->desc->snoop_by != NULL ) continue;

        count++;
        if ( number_range( 1, count ) == 1 )
            pick = ch;
    }

    if ( pick == NULL )
        return FALSE;

    pick->desc->snoop_by = watcher->desc;
    act( "Now watching $N.", watcher, NULL, pick, TO_CHAR );
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

    /* If a player is watching this bot in auto-any mode, reassign them now,
       before do_quit/close_socket clears snoop_by and frees the descriptor. */
    if ( ch->desc != NULL && ch->desc->snoop_by != NULL )
    {
        DESCRIPTOR_DATA *watcher_desc = ch->desc->snoop_by;
        CHAR_DATA       *watcher      = watcher_desc->original
                                        ? watcher_desc->original
                                        : watcher_desc->character;

        /* Send a summary before detaching */
        {
            char echo[256];
            int session_secs = (int)( current_time - bot->session_start );
            snprintf( echo, sizeof(echo),
                "[BOT] %s logging out (session: %dm%ds, pkills: %d, hp: %d/%d)\n\r",
                ch->name,
                session_secs / 60, session_secs % 60,
                ch->pkill,
                ch->hit, ch->max_hit );
            write_to_buffer( watcher_desc, echo, 0 );
        }

        ch->desc->snoop_by = NULL;   /* detach before do_quit tears things down */

        if ( watcher != NULL && watcher->pcdata != NULL
          && watcher->pcdata->bot_watch_any )
        {
            if ( !bot_watch_assign_random( watcher, ch ) )
                write_to_buffer( watcher_desc,
                    "Bot logged out. No other bots online to watch.\n\r", 0 );
        }
        else
        {
            write_to_buffer( watcher_desc,
                "Bot logged out.\n\r", 0 );
        }
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
        /* Skip if a real player has taken over this bot via do_possess */
        if ( ch->desc != NULL && ch->desc->descriptor != BOT_DESCRIPTOR_SENTINEL )
            continue;
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
        /* Build a shuffled index list so selection is random each tick */
        int order[MAX_BOT_ROSTER];
        int n = bot_roster_count;
        for ( i = 0; i < n; i++ ) order[i] = i;
        for ( i = n - 1; i > 0; i-- )
        {
            int j = number_range( 0, i );
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }

        for ( i = 0; i < n; i++ )
        {
            BOT_ROSTER_ENTRY *r = &bot_roster[ order[i] ];
            if ( r->retired || r->online ) continue;
            if ( r->offline_until != 0 && current_time < r->offline_until ) continue;

            if ( bot_login(r) )
                break;   /* one at a time to stagger logins */
        }
    }
}

void do_botwar( CHAR_DATA *ch, char *argument )
{
    CHAR_DATA *wch;
    if ( !IS_IMMORTAL(ch) && !IS_BOT_OVERSEER(ch) )
    {
        send_to_char("Huh?\n\r", ch);
        return;
    }
    global_bot_pvp_mode = BOT_PVP_MODE_WAR;
    send_to_char("Bot PVP is now globally FORCED (War Mode). All bots will relentlessly hunt.\n\r", ch);

    /* Instantly wake up all grinding/idle bots to evaluate PVP right now */
    for ( wch = char_list; wch != NULL; wch = wch->next )
    {
        if ( IS_NPC(wch) ) continue;
        if ( wch->pcdata && wch->pcdata->botdata )
        {
            BOT_DATA *bot = wch->pcdata->botdata;
            if ( bot->state == BOT_IDLE || bot->state == BOT_GRINDING )
            {
                bot->state_timer = 0;
            }
        }
    }
}

void do_botnormal( CHAR_DATA *ch, char *argument )
{
    if ( !IS_IMMORTAL(ch) && !IS_BOT_OVERSEER(ch) )
    {
        send_to_char("Huh?\n\r", ch);
        return;
    }
    global_bot_pvp_mode = BOT_PVP_MODE_NORMAL;
    send_to_char("Bot PVP is now globally reverted to DEFAULT (Normal Mode). Bots will use individual aggression logic.\n\r", ch);
}

void do_botpeace( CHAR_DATA *ch, char *argument )
{
    CHAR_DATA *wch;

    if ( !IS_IMMORTAL(ch) && !IS_BOT_OVERSEER(ch) )
    {
        send_to_char("Huh?\n\r", ch);
        return;
    }

    global_bot_pvp_mode = BOT_PVP_MODE_PEACE;
    send_to_char("Bot PVP is now globally DISABLED (Peace Mode). All bot combat halted.\n\r", ch);

    for ( wch = char_list; wch != NULL; wch = wch->next )
    {
        if ( IS_NPC(wch) ) continue;
        if ( wch->pcdata && wch->pcdata->botdata )
        {
            BOT_DATA *bot = wch->pcdata->botdata;
            if ( bot->state == BOT_PVP_HUNT || bot->state == BOT_PVP_FIGHT )
            {
                if ( wch->hunting && wch->hunting[0] != '\0' )
                {
                    free_string(wch->hunting);
                    wch->hunting = str_dup("");
                }
                if ( wch->position == POS_FIGHTING )
                {
                    stop_fighting( wch, TRUE );
                }
                bot_change_state( wch, bot, BOT_GRINDING );
            }
        }
    }
}
