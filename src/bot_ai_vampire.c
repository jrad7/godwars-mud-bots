/*
 * bot_ai_vampire.c - Vampire class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_VAMPIRE.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_VAMPIRE].
 *
 * Functions here are intentionally file-scoped (static) except for the
 * exported vtable object at the bottom.  Do not call them directly from
 * other translation units; go through bot_class_ai[BOT_CLASS_VAMPIRE].
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
 * bot_vamp_pick_research
 *
 * Returns the DISC_VAMP_* index of the next discipline the vampire bot
 * should research, or -1 if nothing is left to advance within rank limits.
 *
 * Two-pass priority:
 *   Pass 1 - core combat passives to level 5, then unlock key active powers.
 *   Pass 2 - max out combat cores to level 10.
 * ----------------------------------------------------------------------- */

static int bot_vamp_pick_research( CHAR_DATA *ch )
{
    /* Discipline level cap based on vampire age rank */
    int maxlevel;
    if      ( ch->pcdata->rank <= AGE_NEONATE )      maxlevel = 5;
    else if ( ch->pcdata->rank == AGE_ANCILLA )      maxlevel = 7;
    else if ( ch->pcdata->rank == AGE_ELDER   )      maxlevel = 9;
    else                                             maxlevel = 10;

    /*
     * Priority table: { disc_index, target_level }
     * Rows with the same disc at a higher target extend the goal once
     * lower-priority entries are satisfied.
     */
    static const struct { int disc; int target; } prio[] = {
        /* Core combat passives - benefit from being as high as possible */
        { DISC_VAMP_POTE, 5  },   /* Potence:        multiplies unarmed damage  */
        { DISC_VAMP_CELE, 5  },   /* Celerity:       dodge + extra hits          */
        { DISC_VAMP_FORT, 5  },   /* Fortitude:      passive damage reduction    */
        /* Protean 2: unlock claws (primary melee weapon) */
        { DISC_VAMP_PROT, 2  },
        /* Obtenebration 5: shroud aura + lamprey drain attack */
        { DISC_VAMP_OBTE, 5  },
        /* Presence 1: awe combat aura */
        { DISC_VAMP_PRES, 1  },
        /* Auspex 1: truesight (see invisible / detect hidden) */
        { DISC_VAMP_AUSP, 1  },
        /* Thaumaturgy 4: theft of vitae (steal blood in combat) */
        { DISC_VAMP_THAU, 4  },
        /* Serpentis 4: tendrils (combat melee attack) */
        { DISC_VAMP_SERP, 4  },
        /* Thanatosis 5: withering stat debuff + drainlife */
        { DISC_VAMP_THAN, 5  },
        /* Obfuscate 1: vanish (emergency escape) */
        { DISC_VAMP_OBFU, 1  },
        /* Necromancy 4: spirit guard (defensive buff) */
        { DISC_VAMP_NECR, 4  },
        /* Pass 2: maximize the combat cores */
        { DISC_VAMP_POTE, 10 },
        { DISC_VAMP_CELE, 10 },
        { DISC_VAMP_FORT, 10 },
        { DISC_VAMP_PROT, 10 },
        { DISC_VAMP_OBTE, 10 },
        { DISC_VAMP_PRES, 10 },
        { -1, 0 }
    };

    int i;
    for ( i = 0; prio[i].disc != -1; i++ )
    {
        int disc   = prio[i].disc;
        int target = UMIN( prio[i].target, maxlevel );

        if ( ch->power[disc] < 0  ) continue;  /* not available to this char */
        if ( ch->power[disc] >= target ) continue;  /* already at or past goal */
        if ( ch->power[disc] >= 10     ) continue;  /* already at absolute max */

        return disc;
    }

    return -1;  /* nothing left within current rank */
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE if the vampire has class-specific exp worth spending:
 *   - an age milestone is reachable, or
 *   - a discipline research slot needs to be started or spent.
 * ----------------------------------------------------------------------- */

static bool bot_vamp_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_VAMPIRE) ) return FALSE;

    /* Age milestones */
    if ( ch->pcdata->rank <= AGE_NEONATE    && ch->exp >= 1500000  ) return TRUE;
    if ( ch->pcdata->rank == AGE_ANCILLA    && ch->exp >= 7500000  ) return TRUE;
    if ( ch->pcdata->rank == AGE_ELDER      && ch->exp >= 15000000 ) return TRUE;
    if ( ch->pcdata->rank == AGE_METHUSELAH && ch->exp >= 30000000 ) return TRUE;
    if ( ch->pcdata->rank == AGE_LA_MAGRA   && ch->exp >= 60000000 ) return TRUE;

    /* Research finished - time to spend the point */
    if ( ch->pcdata->disc_points == 999 ) return TRUE;

    /* Not researching yet - start the next one in priority order */
    if ( ch->pcdata->disc_research == -1
      && bot_vamp_pick_research(ch) != -1 )
        return TRUE;

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Executes one vampire-specific training step.
 * Returns TRUE if a command was issued.
 * Called by bot_do_train() before the generic hp/mana/move spending.
 * ----------------------------------------------------------------------- */

static bool bot_vamp_do_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_VAMPIRE) ) return FALSE;

    /* Age progression */
    if ( ch->pcdata->rank <= AGE_NEONATE    && ch->exp >= 1500000  )
        { bot_cmd( ch, "train ancilla" );    return TRUE; }
    if ( ch->pcdata->rank == AGE_ANCILLA    && ch->exp >= 7500000  )
        { bot_cmd( ch, "train elder" );      return TRUE; }
    if ( ch->pcdata->rank == AGE_ELDER      && ch->exp >= 15000000 )
        { bot_cmd( ch, "train methuselah" ); return TRUE; }
    if ( ch->pcdata->rank == AGE_METHUSELAH && ch->exp >= 30000000 )
        { bot_cmd( ch, "train lamagra" );    return TRUE; }
    if ( ch->pcdata->rank == AGE_LA_MAGRA   && ch->exp >= 60000000 )
        { bot_cmd( ch, "train trueblood" );  return TRUE; }

    /* Research complete - spend the point */
    if ( ch->pcdata->disc_points == 999 && ch->pcdata->disc_research > 0 )
    {
        char cmd[64];
        sprintf( cmd, "train %s", discipline[ch->pcdata->disc_research] );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    /* No active research - start the next one in priority order */
    if ( ch->pcdata->disc_research == -1 )
    {
        int disc = bot_vamp_pick_research(ch);
        if ( disc > 0 )
        {
            char cmd[64];
            sprintf( cmd, "research %s", discipline[disc] );
            bot_cmd( ch, cmd );
            return TRUE;
        }
    }

    /* Still accumulating research points - nothing to do right now */
    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Ensures all passive vampire buffs are active.
 * Issues at most one command per call; returns TRUE when it does.
 * Safe to call between fights.
 * ----------------------------------------------------------------------- */

static bool bot_vamp_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_VAMPIRE) ) return FALSE;

    /* Truesight (Auspex 1) - see invisible / detect hidden */
    if ( ch->power[DISC_VAMP_AUSP] >= 1
      && !IS_SET(ch->act, PLR_HOLYLIGHT) )
    { bot_cmd( ch, "truesight" ); return TRUE; }

    /* Awe (Presence 1) - combat intimidation aura */
    if ( ch->power[DISC_VAMP_PRES] >= 1
      && !IS_EXTRA(ch, EXTRA_AWE) )
    { bot_cmd( ch, "awe" ); return TRUE; }

    /* Claws (Protean 2) - primary melee weapon for vampires */
    if ( ch->power[DISC_VAMP_PROT] >= 2
      && !IS_VAMPAFF(ch, VAM_CLAWS) )
    { bot_cmd( ch, "claws" ); return TRUE; }

    /* Spirit Guard (Necromancy 4) - defensive buff vs. attacks */
    if ( ch->power[DISC_VAMP_NECR] >= 4
      && !IS_SET(ch->flag2, AFF_SPIRITGUARD) )
    { bot_cmd( ch, "spiritguard" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one combat ability per tick using probability-based selection.
 * Called each pulse while the bot is in POS_FIGHTING.
 * ----------------------------------------------------------------------- */

static void bot_vamp_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA  *target = ch->fighting;
    int         blood;
    int         roll;
    char        cmd[MAX_INPUT_LENGTH];
    const char *tname;

    if ( target == NULL || ch->pcdata == NULL ) return;

    blood = ch->pcdata->condition[COND_THIRST];
    roll  = number_range( 1, 100 );
    tname = target->name;

    /* Priority 0: steal blood when running dry (Thaumaturgy 4)
     * Blood fuels most discipline abilities - keep it topped up. */
    if ( ch->power[DISC_VAMP_THAU] >= 4 && blood < 100 )
    {
        sprintf( cmd, "theft %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 1 (25%): drainlife - deals damage and recovers HP */
    if ( ch->power[DISC_VAMP_THAN] >= 5 && roll <= 25 )
    {
        sprintf( cmd, "drainlife %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* (Priority 2 removed: assassinate is out-of-combat only) */

    /* Priority 3 (55%): lamprey - shadow drain (Obtenebration 5) */
    if ( ch->power[DISC_VAMP_OBTE] >= 5 && roll <= 55 )
    {
        sprintf( cmd, "lamprey %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 4 (65%): tendrils - serpentis melee attack */
    if ( ch->power[DISC_VAMP_SERP] >= 4 && roll <= 65 )
    {
        sprintf( cmd, "tendrils %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 5 (75%): withering - reduces target's combat stats */
    if ( ch->power[DISC_VAMP_THAN] >= 4 && roll <= 75 )
    {
        sprintf( cmd, "withering %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 6 (85%): scream - room-wide sonic damage (costs 50 blood) */
    if ( ch->power[DISC_VAMP_MELP] >= 1 && blood >= 50 && roll <= 85 )
    {
        bot_cmd( ch, "scream" );
        return;
    }

    /* (Priority 7 removed: mindblast is out-of-combat only) */

    /* Fallback: basic combat continues via normal multi_hit loop */
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_vamp_ai = {
    bot_vamp_should_train,   /* should_train   */
    bot_vamp_do_train,       /* do_train       */
    bot_vamp_buff_check,     /* buff_check     */
    bot_vamp_combat_action,  /* combat_action  */
    NULL                     /* between_fights - vampires use buff_check instead */
};
