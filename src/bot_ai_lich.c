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

/* -----------------------------------------------------------------------
 * bot_buff_check_lich
 * ----------------------------------------------------------------------- */
static bool bot_buff_check_lich( CHAR_DATA *ch )
{
    return FALSE;
}

/* -----------------------------------------------------------------------
 * bot_should_train_lich
 * ----------------------------------------------------------------------- */
static bool bot_should_train_lich( CHAR_DATA *ch )
{
    /* Lores max out at 5. Cost is 10,000,000 * (current_lore + 1) */
    if ( ch->pcdata->powers[CON_LORE] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[CON_LORE] + 1) ) return TRUE;
    if ( ch->pcdata->powers[DEATH_LORE] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[DEATH_LORE] + 1) ) return TRUE;
    if ( ch->pcdata->powers[LIFE_LORE] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[LIFE_LORE] + 1) ) return TRUE;
    if ( ch->pcdata->powers[NECROMANTIC] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[NECROMANTIC] + 1) ) return TRUE;
    if ( ch->pcdata->powers[CHAOS_MAGIC] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[CHAOS_MAGIC] + 1) ) return TRUE;

    if ( bot_should_train_primal(ch) ) return TRUE;

    return FALSE;
}

/* -----------------------------------------------------------------------
 * bot_do_train_lich
 * ----------------------------------------------------------------------- */
static bool bot_do_train_lich( CHAR_DATA *ch )
{
    if ( ch->pcdata->powers[CON_LORE] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[CON_LORE] + 1) )
    {
        bot_cmd( ch, "studylore Conjuring" );
        return TRUE;
    }
    if ( ch->pcdata->powers[DEATH_LORE] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[DEATH_LORE] + 1) )
    {
        bot_cmd( ch, "studylore Death" );
        return TRUE;
    }
    if ( ch->pcdata->powers[LIFE_LORE] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[LIFE_LORE] + 1) )
    {
        bot_cmd( ch, "studylore Life" );
        return TRUE;
    }
    if ( ch->pcdata->powers[NECROMANTIC] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[NECROMANTIC] + 1) )
    {
        bot_cmd( ch, "studylore Necromantic" );
        return TRUE;
    }
    if ( ch->pcdata->powers[CHAOS_MAGIC] < 5 && ch->exp >= 10000000 * (ch->pcdata->powers[CHAOS_MAGIC] + 1) )
    {
        bot_cmd( ch, "studylore Chaos" );
        return TRUE;
    }
    return FALSE;
}

/* -----------------------------------------------------------------------
 * bot_combat_action_lich
 * ----------------------------------------------------------------------- */
static void bot_combat_action_lich( CHAR_DATA *ch )
{
    CHAR_DATA *victim = ch->fighting;
    char       buf[128];
    int        pick;

    if ( victim == NULL )
        return;

    /* Focus on single target abilities with specific constraints */
    if ( ch->hit < ch->max_hit * 0.5 && ch->pcdata->powers[LIFE_LORE] >= 4 && ch->mana >= 5000 )
    {
        bot_cmd( ch, "powertransfer" );
        return;
    }
    
    if ( number_percent() < 60 )
    {
        pick = number_range( 1, 4 );
        if ( pick == 1 && ch->pcdata->powers[DEATH_LORE] >= 3 ) {
            snprintf(buf, sizeof(buf), "chillhand %s", victim->name);
            bot_cmd(ch, buf);
            return;
        }
        if ( pick == 2 && ch->pcdata->powers[DEATH_LORE] >= 4 ) {
            snprintf(buf, sizeof(buf), "painwreck %s", victim->name);
            bot_cmd(ch, buf);
            return;
        }
        if ( pick == 3 && ch->pcdata->powers[NECROMANTIC] >= 3 ) {
            snprintf(buf, sizeof(buf), "soulsuck %s", victim->name);
            bot_cmd(ch, buf);
            return;
        }
        if ( pick == 4 && ch->pcdata->powers[LIFE_LORE] >= 5 ) {
            snprintf(buf, sizeof(buf), "polarity %s", victim->name);
            bot_cmd(ch, buf);
            return;
        }
    }
}

/* -----------------------------------------------------------------------
 * bot_between_fights_lich
 * ----------------------------------------------------------------------- */
/* Toggle this to TRUE if you want to restrict the bot to 1 golem at a time */
static bool bot_lich_single_golem = FALSE;

static bool bot_between_fights_lich( CHAR_DATA *ch )
{
    /* Only summon if we have enough Conjuring lore */
    if ( ch->pcdata->powers[CON_LORE] < 4 )
        return FALSE;

    if ( bot_lich_single_golem )
    {
        bool has_golem = FALSE;
        CHAR_DATA *wch;
        for ( wch = char_list; wch != NULL; wch = wch->next )
        {
            if ( IS_NPC(wch) && wch->master == ch )
            {
                has_golem = TRUE;
                break;
            }
        }
        if ( has_golem )
            return FALSE;
    }

    /* Golems are heavily restricted by flags. We try them sequentially based on availability. */
    if ( !IS_SET(ch->pcdata->powers[GOLEMS_SUMMON], HAS_SUMMONED_IRON) )
    {
        bot_cmd( ch, "summongolem iron" );
        return TRUE;
    }
    if ( !IS_SET(ch->pcdata->powers[GOLEMS_SUMMON], HAS_SUMMONED_STONE) )
    {
        bot_cmd( ch, "summongolem stone" );
        return TRUE;
    }
    if ( !IS_SET(ch->pcdata->powers[GOLEMS_SUMMON], HAS_SUMMONED_CLAY) )
    {
        bot_cmd( ch, "summongolem clay" );
        return TRUE;
    }
    if ( !IS_SET(ch->pcdata->powers[GOLEMS_SUMMON], HAS_SUMMONED_FIRE) )
    {
        bot_cmd( ch, "summongolem fire" );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable linkage
 * ----------------------------------------------------------------------- */
const BOT_CLASS_AI bot_lich_ai = {
    bot_should_train_lich,
    bot_do_train_lich,
    bot_buff_check_lich,
    bot_combat_action_lich,
    bot_between_fights_lich
};
