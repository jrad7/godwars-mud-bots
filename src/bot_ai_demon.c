/*
 * bot_ai_demon.c - Demon class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_DEMON.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_DEMON].
 *
 * Currently a stub - all hooks are NULL (generic grinding only).
 * TODO: should_train, do_train, buff_check, combat_action, between_fights.
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
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_demon_ai = {
    NULL,   /* should_train   - TODO */
    NULL,   /* do_train       - TODO */
    NULL,   /* buff_check     - TODO */
    NULL,   /* combat_action  - TODO */
    NULL    /* between_fights - TODO */
};
