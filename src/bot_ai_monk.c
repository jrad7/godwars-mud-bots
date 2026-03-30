/*
 * bot_ai_monk.c - Monk class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_MONK.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_MONK].
 *
 * Currently implements: between_fights (autostance advancement).
 * TODO: buff_check, combat_action, should_train, do_train.
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

/* Index into ch->stance[] for the autostance setting */
#define MONK_AUTODROP  12

/* Forward declaration - defined in kav_fight.c */
void do_stance( CHAR_DATA *ch, char *argument );
void do_autostance( CHAR_DATA *ch, char *argument );

/* -----------------------------------------------------------------------
 * bot_pick_training_stance
 *
 * Returns the 1-based slot (1-5, matching STANCE_* constants) of the
 * next basic stance to train, in order: viper -> crane -> crab ->
 * mongoose -> bull.  Returns 0 if all five are mastered (>= 200 XP).
 * ----------------------------------------------------------------------- */

static int bot_pick_training_stance( CHAR_DATA *ch )
{
    static const int basic[] = { 1, 2, 3, 4, 5 };
    int i;

    for ( i = 0; i < 5; i++ )
    {
        int xp = ch->stance[ basic[i] ];
        if ( xp < 0 ) xp = 0;   /* -1 means locked, treat as 0 */
        if ( xp < 200 )
            return i + 1;        /* 1-based slot */
    }
    return 0;   /* all mastered */
}

/* -----------------------------------------------------------------------
 * bot_set_autostance
 *
 * Sets the bot's autostance (MONK_AUTODROP slot) to the current training
 * stance so the combat engine's autodrop() handles entry on the next fight.
 * Stays on the current stance until it reaches 200 XP, then advances.
 * ----------------------------------------------------------------------- */

static void bot_set_autostance( CHAR_DATA *ch )
{
    static const char *names[] = { "viper", "crane", "crab", "mongoose", "bull" };
    int current = ch->stance[MONK_AUTODROP];
    int pick;

    /* Stick with current stance until fully mastered */
    if ( current >= STANCE_VIPER && current <= STANCE_BULL
      && ch->stance[current] < 200 )
        return;

    /* Current mastered (or unset) - advance to next unmastered */
    pick = bot_pick_training_stance( ch );
    if ( pick == 0 )
        return;   /* all mastered - keep last autostance */

    if ( ch->stance[MONK_AUTODROP] == pick )
        return;   /* already set correctly */

    do_autostance( ch, (char *)names[ pick - 1 ] );
}

/* -----------------------------------------------------------------------
 * Vtable: between_fights
 *
 * Called each tick the bot is grinding but not yet in combat.
 * Advances the training stance when the current one is mastered and
 * relaxes from the old stance so autodrop() re-enters the new one on
 * the next fight.
 *
 * Returns TRUE when a command was issued (skip mob-hunting this tick).
 * ----------------------------------------------------------------------- */

static bool bot_monk_between_fights( CHAR_DATA *ch )
{
    int prev = ch->stance[MONK_AUTODROP];
    bot_set_autostance( ch );

    /* Stance advanced - relax once so autodrop enters the new stance */
    if ( ch->stance[MONK_AUTODROP] != prev && ch->stance[0] != -1 )
    {
        do_stance( ch, "" );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_monk_ai = {
    NULL,                      /* should_train   - TODO */
    NULL,                      /* do_train       - TODO */
    NULL,                      /* buff_check     - TODO */
    NULL,                      /* combat_action  - TODO */
    bot_monk_between_fights    /* between_fights */
};
