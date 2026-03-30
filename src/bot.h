#ifndef BOT_H
#define BOT_H

/*
 * Bot system for Dystopia MUD
 * Server-side player bots that use fake descriptors (no real socket).
 * Bots execute commands via interpret() just like real players.
 */

#include <time.h>

/* Forward declarations to avoid circular includes with merc.h */
struct char_data;
struct descriptor_data;

/* Set to 1 to enable verbose bot debug logging, 0 to disable */
#define BOT_DEBUG  0

/* Sentinel value for bot descriptors - means no real socket */
#define BOT_DESCRIPTOR_SENTINEL  (-2)

/* Population settings */
#define MAX_BOT_ROSTER      20
#define BOT_MIN_ONLINE       1
#define BOT_MAX_ONLINE       8

/* Session lengths in seconds */
#define BOT_SESSION_MIN  (30  * 60)   /* 30 minutes */
#define BOT_SESSION_MAX  (240 * 60)   /* 4 hours    */
#define BOT_OFFLINE_MIN  (10  * 60)   /* 10 minutes */
#define BOT_OFFLINE_MAX  (120 * 60)   /* 2 hours    */

/* Pulse timer for bot manager (~30 seconds at 4 pulses/sec) */
#define PULSE_BOT_MANAGER  (30 * 4)

/* Pulse timer for bot AI (every second) */
#define PULSE_BOT_AI       ( 1 * PULSE_PER_SECOND)

/* Retirement thresholds in seconds of total playtime */
#define BOT_RETIRE_SHORT   ( 4 * 3600)   /* 4 hours */
#define BOT_RETIRE_LONG    (72 * 3600)   /* 3 days  */

/*
 * Bot AI states
 */
typedef enum {
    BOT_IDLE        = 0,   /* Standing around, looking, social chat    */
    BOT_EXPLORING   = 1,   /* Walking through rooms, examining things  */
    BOT_GRINDING    = 2,   /* Fighting mobs for experience             */
    BOT_TRAINING    = 3,   /* Practicing skills, training stats        */
    BOT_PVP_HUNT    = 4,   /* Seeking a player fight                   */
    BOT_PVP_FIGHT   = 5,   /* Actively in PvP combat                   */
    BOT_SHOPPING    = 6,   /* Buying/selling at shops                  */
    BOT_RESTING     = 7,   /* Sitting/sleeping to recover HP/mana      */
    BOT_LOGGING_OUT = 8    /* Saying goodbye, about to quit            */
} bot_state_t;

/*
 * Bot lifespan types
 */
typedef enum {
    BOT_LIFE_PERMANENT = 0,  /* Never retires, always comes back */
    BOT_LIFE_LONG      = 1,  /* Retires after ~3 days of playtime */
    BOT_LIFE_SHORT     = 2   /* Retires after ~4 hours of playtime */
} bot_life_t;

/*
 * Class preference indices (maps to CLASS_* in class.h)
 */
#define BOT_CLASS_VAMPIRE   0
#define BOT_CLASS_MONK      1
#define BOT_CLASS_NINJA     2
#define BOT_CLASS_DEMON     3
#define BOT_CLASS_COUNT     4

/*
 * Bot roster entry - one per named bot character.
 * Persisted in ../txt/bots.txt between server restarts.
 */
typedef struct bot_roster_entry {
    char    name[20];           /* Character name                    */
    int     class_pref;         /* BOT_CLASS_* preference            */
    int     lifespan;           /* BOT_LIFE_* type                   */
    int     chattiness;         /* 0-100: how often they chat        */
    int     aggression;         /* 0-100: how likely to seek PvP     */
    int     explorer;           /* 0-100: how much they roam         */
    int     total_playtime;     /* Total seconds played ever         */
    time_t  offline_until;      /* Earliest re-login time (0 = now)  */
    bool    retired;            /* TRUE = permanently gone           */
    bool    online;             /* TRUE = currently logged in        */
} BOT_ROSTER_ENTRY;

/*
 * Per-character bot data.
 * Allocated when a bot logs in, freed when they log out.
 * Stored as botdata pointer in PC_DATA.
 */
struct bot_data {
    BOT_ROSTER_ENTRY   *roster;             /* Back-pointer to roster      */
    bot_state_t         state;              /* Current AI state            */
    int                 state_timer;        /* Pulses left in this state   */
    int                 cmd_delay;          /* Pulses until next command   */
    time_t              session_start;      /* When this session began     */
    int                 session_max;        /* Max seconds for session     */
    int                 idle_chat_timer;    /* Pulses until unprompted msg */
    int                 grind_attempts;     /* Combat attempts this state  */
    char                nav_cmds[8][32];   /* Queued navigation commands  */
    int                 nav_n;             /* How many are pending        */
};
typedef struct bot_data BOT_DATA;

/* Global roster (defined in bot_mgr.c) */
extern BOT_ROSTER_ENTRY bot_roster[MAX_BOT_ROSTER];
extern int              bot_roster_count;

/* Function prototypes */
void    bot_manager_update  ( void );
void    bot_ai_tick         ( void );
void    bot_update          ( struct char_data *ch );
bool    bot_login           ( BOT_ROSTER_ENTRY *roster );
void    bot_logout          ( struct char_data *ch );
void    load_bot_roster     ( void );
void    save_bot_roster     ( void );

void    bot_ai_update       ( struct char_data *ch, BOT_DATA *bot );
void    bot_change_state    ( struct char_data *ch, BOT_DATA *bot, bot_state_t new_state );
void    bot_cmd             ( struct char_data *ch, const char *cmd );

void    bot_chat_init       ( void );
const char *bot_get_response( const char *trigger, int chattiness );
void    bot_hear_say        ( struct char_data *bot, struct char_data *speaker, char *msg );
void    bot_hear_tell       ( struct char_data *bot, struct char_data *speaker, char *msg );
void    bot_unprompted_chat ( struct char_data *ch, BOT_DATA *bot );

#endif /* BOT_H */
