/*
 * bot_ai_werewolf.c - Werewolf class AI for Dystopia MUD bots
 *
 * Implements the BOT_CLASS_AI vtable for CLASS_WEREWOLF.
 * Registered in bot_ai.c as bot_class_ai[BOT_CLASS_WEREWOLF].
 *
 * Functions here are intentionally file-scoped (static) except for the
 * exported vtable object at the bottom.  Do not call them directly from
 * other translation units; go through bot_class_ai[BOT_CLASS_WEREWOLF].
 *
 * Werewolf combat power comes from stacked passives:
 *   - DISC_WERE_BOAR >= 3: incoming dam /= 2 (biggest single gain)
 *   - DISC_WERE_BEAR >= 5: dam *= 1.2 (20% outgoing boost)
 *   - DISC_WERE_LYNX >= 2: +2 extra attacks per round
 *   - DISC_WERE_RAPT >= 1: automatic rfangs extra attack every round
 *   - ch->rage > 99:       max_dam += ch->rage + 400
 *
 * Active abilities (moonbeam, talons, roar, staredown) supplement passives.
 * The research/train loop is identical to the vampire and demon systems.
 *
 * See doc/bot_ai/werewolf.md for full discipline reference.
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
#include "garou.h"

/* -----------------------------------------------------------------------
 * bot_ww_pick_research
 *
 * Returns the DISC_WERE_* index of the next discipline to research,
 * or -1 if all priority targets are already met.
 *
 * Priority rationale — damage first (thresholds match fight.c's `>`
 * checks, so targets are one level above the docstring label —
 * e.g. "Bear 5" activates at power[BEAR] > 5, i.e. 6):
 *   1. Raptor 1 — auto rfangs extra multi_hit every round (1-point unlock)
 *   2. Spider 1 — passive poison on every hit (1-point unlock)
 *   3. Wolf 2   — wolfman form: +50 hit/dam, rage+400 max_dam in combat
 *   4. Bear 6   — dam *= 1.2; 20% melee damage boost (fight.c:1515 uses > 5)
 *   5. Lynx 3   — +2 extra attacks per round (fight.c:1082 uses > 2)
 *   6. Boar 3   — incoming dam /= 2; opportunistic survival pickup
 *   7. Luna 1   — flameclaws toggle (cheapest Luna unlock)
 *   8. Luna 2   — moonarmour unlock (gear system requires DISC_WERE_LUNA >= 2)
 *   9. Bear 8   — slam auto-proc (1 in 2–5 rounds depending on Bear lvl)
 *  10. Hawk 5   — quills toggle (extra multi_hit per round)
 *  11. Pain 10  — +750 max_dam flat (expensive but huge damage cap bump)
 *  12. Boar 7   — extra attacks from move pool
 *  13. Bear 7   — skin (−100 armor) and rend toggles
 *  14. Wolf 4   — razorclaws (requires claws active)
 *  15. Mantis 3 — dodge/disarm-resist bonus
 *  16. Raptor 3 — perception toggle
 *  17. Raptor 8 — jawlock (prevents target fleeing)
 *  18. Luna 3   — motherstouch self-heal in combat
 *  19. Spider 2 — web command
 *  20. Raptor 10 — talons burst (2k–4k vs NPCs)
 *  21. Luna 8   — moonbeam (500-mana burst)
 *  22. Mantis 6 — full dodge/parry bonus
 *  23. Max remaining disciplines to 10 (skip Owl — gnosis-gated)
 *
 * Owl 5/8 and Pain (beyond 10) omitted from the active list:
 * staredown force-flees (wastes kills) and cocoon needs gnosis points
 * that aren't trained; they fall into the final max-to-10 sweep.
 * ----------------------------------------------------------------------- */

static int bot_ww_pick_research( CHAR_DATA *ch )
{
    static const struct { int disc; int target; } prio[] = {
        /* Raptor 1: auto rfangs extra attack every round — 1-point unlock */
        { DISC_WERE_RAPT, 1  },
        /* Spider 1: passive poison on hit — 1-point unlock */
        { DISC_WERE_SPID, 1  },
        /* Wolf 2: wolfman form (rage max_dam bonus, +50 hitroll/damroll) */
        { DISC_WERE_WOLF, 2  },
        /* Bear 6: 20% outgoing damage boost (fight.c:1515 uses > 5) */
        { DISC_WERE_BEAR, 6  },
        /* Lynx 3: +2 extra attacks (fight.c:1082 uses > 2) */
        { DISC_WERE_LYNX, 3  },
        /* Boar 3: halves incoming damage — opportunistic survival pickup
         * (fight.c uses > 2, so 3 is the activation level) */
        { DISC_WERE_BOAR, 3  },
        /* Luna 1: flameclaws toggle */
        { DISC_WERE_LUNA, 1  },
        /* Luna 2: moonarmour unlock (DISC_WERE_LUNA >= 2 required by bot gear) */
        { DISC_WERE_LUNA, 2  },
        /* Bear 8: slam auto-proc */
        { DISC_WERE_BEAR, 8  },
        /* Hawk 5: quills toggle */
        { DISC_WERE_HAWK, 5  },
        /* Pain 10: +750 max_dam flat */
        { DISC_WERE_PAIN, 10 },
        /* Boar 7: extra attacks from move pool */
        { DISC_WERE_BOAR, 7  },
        /* Bear 7: skin + rend */
        { DISC_WERE_BEAR, 7  },
        /* Wolf 4: razorclaws */
        { DISC_WERE_WOLF, 4  },
        /* Mantis 3: dodge and disarm-resist bonus */
        { DISC_WERE_MANT, 3  },
        /* Raptor 3: perception toggle */
        { DISC_WERE_RAPT, 3  },
        /* Raptor 8: jawlock */
        { DISC_WERE_RAPT, 8  },
        /* Luna 3: motherstouch self-heal */
        { DISC_WERE_LUNA, 3  },
        /* Spider 2: web command */
        { DISC_WERE_SPID, 2  },
        /* Raptor 10: talons burst */
        { DISC_WERE_RAPT, 10 },
        /* Luna 8: moonbeam */
        { DISC_WERE_LUNA, 8  },
        /* Mantis 6: full dodge/parry bonus */
        { DISC_WERE_MANT, 6  },
        /* Max remaining */
        { DISC_WERE_BEAR, 10 },
        { DISC_WERE_BOAR, 10 },
        { DISC_WERE_LYNX, 10 },
        { DISC_WERE_RAPT, 10 },
        { DISC_WERE_HAWK, 10 },
        { DISC_WERE_WOLF, 10 },
        { DISC_WERE_SPID, 10 },
        { DISC_WERE_LUNA, 10 },
        { DISC_WERE_CONG, 10 },
        { DISC_WERE_PAIN, 10 },
        { -1, 0 }
    };

    int i;
    for ( i = 0; prio[i].disc != -1; i++ )
    {
        int disc   = prio[i].disc;
        int target = prio[i].target;

        if ( ch->power[disc] < 0  ) continue;   /* not available */
        if ( ch->power[disc] >= target ) continue;
        if ( ch->power[disc] >= 10     ) continue;

        return disc;
    }

    return -1;
}

/* -----------------------------------------------------------------------
 * Vtable: should_train
 *
 * Returns TRUE when there is a research step to action:
 *   - research is complete (disc_points == 999), or
 *   - no research is active and a discipline still needs advancement.
 * ----------------------------------------------------------------------- */

static bool bot_ww_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_WEREWOLF) ) return FALSE;

    /* Research complete — ready to spend the point */
    if ( ch->pcdata->disc_points == 999 ) return TRUE;

    /* No active research — start the next one if anything remains */
    if ( ch->pcdata->disc_research == -1
      && bot_ww_pick_research(ch) != -1 )
        return TRUE;

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: do_train
 *
 * Executes one werewolf-specific training step.
 * Returns TRUE if a command was issued.
 * Called by bot_do_train() before the generic hp/mana/move spending.
 * ----------------------------------------------------------------------- */

static bool bot_ww_do_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_WEREWOLF) ) return FALSE;

    /* Research complete — spend the point */
    if ( ch->pcdata->disc_points == 999 && ch->pcdata->disc_research > 0 )
    {
        char cmd[64];
        sprintf( cmd, "train %s", discipline[ch->pcdata->disc_research] );
        bot_cmd( ch, cmd );
        return TRUE;
    }

    /* No active research — start highest-priority unfinished discipline */
    if ( ch->pcdata->disc_research == -1 )
    {
        int disc = bot_ww_pick_research(ch);
        if ( disc > 0 )
        {
            char cmd[64];
            sprintf( cmd, "research %s", discipline[disc] );
            bot_cmd( ch, cmd );
            return TRUE;
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: buff_check
 *
 * Ensures all werewolf toggle buffs are active.
 * Issues at most one command per call; returns TRUE when it does.
 * Called each grinding tick between fights.
 *
 * Buff priority (highest first):
 *   1. flameclaws  — flaming claws (Luna 1)
 *   2. quills      — extra quills attack each round (Hawk 5)
 *   3. skin        — −100 armor (Bear 7)
 *   4. slam        — auto shoulder slam (Bear 8)
 *   5. perception  — detect hidden/stealthy (Raptor 3)
 *   6. jawlock     — prevent flee (Raptor 8)
 *
 * Wolfman form (rage) is not buffed here: update_werewolf auto-fires
 * do_werewolf when rage hits 100 during combat, and rage decays back
 * below 100 out of combat — manual activation would be immediately
 * undone by the next idle tick.
 * ----------------------------------------------------------------------- */

static bool bot_ww_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_WEREWOLF) ) return FALSE;

    /* Flameclaws (Luna 1): flaming claw attacks */
    if ( ch->power[DISC_WERE_LUNA] >= 1
      && !IS_SET(ch->newbits, NEW_MONKFLAME) )
    { bot_cmd( ch, "flameclaws" ); return TRUE; }

    /* Quills (Hawk 5): extra multi_hit quills attack each round */
    if ( ch->power[DISC_WERE_HAWK] >= 5
      && !IS_SET(ch->newbits, NEW_QUILLS) )
    { bot_cmd( ch, "quills" ); return TRUE; }

    /* Skin (Bear 7): −100 armor */
    if ( ch->power[DISC_WERE_BEAR] >= 7
      && !IS_SET(ch->newbits, NEW_SKIN) )
    { bot_cmd( ch, "skin" ); return TRUE; }

    /* Slam (Bear 8): auto shoulder slam proc in combat */
    if ( ch->power[DISC_WERE_BEAR] >= 8
      && !IS_SET(ch->newbits, NEW_SLAM) )
    { bot_cmd( ch, "slam" ); return TRUE; }

    /* Perception (Raptor 3): detect hidden / stealthy enemies */
    if ( ch->power[DISC_WERE_RAPT] >= 3
      && !IS_SET(ch->newbits, NEW_PERCEPTION) )
    { bot_cmd( ch, "perception" ); return TRUE; }

    /* Jawlock (Raptor 8): prevents target from fleeing */
    if ( ch->power[DISC_WERE_RAPT] >= 8
      && !IS_SET(ch->newbits, NEW_JAWLOCK) )
    { bot_cmd( ch, "jawlock" ); return TRUE; }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Vtable: combat_action
 *
 * Fires one active combat ability per tick.
 * Called each pulse while ch->position == POS_FIGHTING.
 *
 * Most werewolf damage is passive (rfangs, quills, slam, Boar knockdown,
 * Bear dam boost, rage max_dam), so active abilities supplement.
 *
 * Priority:
 *   1 (20%): moonbeam   — Luna 8, 500 mana, 500–1000 burst by alignment
 *   2 (45%): talons     — Raptor 10, 2k–4k vs NPCs, no mana cost
 *   3 (65%): motherstouch self — Luna 3, 50 mana, +100 HP; only if hurt
 *   4 (80%): web        — Spider 2, immobilize target
 *
 * Roar and staredown intentionally omitted: both force the target to flee,
 * which wastes the kill (no xp, no corpse to devour) and breaks the
 * bot's grind loop.
 * ----------------------------------------------------------------------- */

static void bot_ww_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA  *target = ch->fighting;
    int         roll;
    char        cmd[MAX_INPUT_LENGTH];
    const char *tname;

    if ( target == NULL ) return;

    roll  = number_range( 1, 100 );
    tname = target->name;

    /* Priority 1 (20%): moonbeam — large burst, alignment-scaled
     * 500 mana at Luna 8; deals 500 (good), 750 (neutral), or 1000 (evil) */
    if ( ch->power[DISC_WERE_LUNA] >= 8
      && ch->mana >= 500
      && roll <= 20 )
    {
        sprintf( cmd, "moonbeam %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 2 (45%): talons — no mana cost, high NPC damage */
    if ( ch->power[DISC_WERE_RAPT] >= 10
      && roll <= 45 )
    {
        bot_cmd( ch, "talons" );
        return;
    }

    /* Priority 3 (65%): motherstouch self — self-heal when below 75% HP
     * In-combat version costs 50 mana and heals +100 HP. */
    if ( ch->power[DISC_WERE_LUNA] >= 3
      && ch->mana >= 50
      && ch->hit < (ch->max_hit * 3 / 4)
      && roll <= 65 )
    {
        bot_cmd( ch, "motherstouch self" );
        return;
    }

    /* Priority 4 (80%): web — immobilize target */
    if ( ch->power[DISC_WERE_SPID] >= 2
      && roll <= 80 )
    {
        sprintf( cmd, "web %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Fallback: passive multi_hit loop (rfangs, quills, slam, bear boost) */
}

/* -----------------------------------------------------------------------
 * Vtable: between_fights
 *
 * Setup performed between kills during the grinding loop.
 * Returns TRUE if a command was issued (stops hunt processing that tick).
 *
 * Rage is intentionally not built here: update_werewolf (update.c:1854)
 * auto-builds rage +5-10/tick while fighting and auto-fires do_werewolf
 * at 100, so the max_dam bonus and wolfman form come online naturally
 * during combat.  Out of combat rage decays -1/tick (update.c:1860)
 * and drops wolfman below 100, so manual pre-raging just wastes a
 * 12-beat lag on rage that will decay before the next fight starts.
 * ----------------------------------------------------------------------- */

static bool bot_ww_between_fights( CHAR_DATA *ch )
{
    OBJ_DATA *obj;

    if ( !IS_CLASS(ch, CLASS_WEREWOLF) ) return FALSE;

    /* Devour NPC corpse in room to recover HP (Raptor 5+) */
    if ( ch->power[DISC_WERE_RAPT] >= 5
      && ch->hit < ch->max_hit )
    {
        for ( obj = ch->in_room->contents; obj != NULL; obj = obj->next_content )
        {
            if ( obj->item_type == ITEM_CORPSE_NPC )
            {
                bot_cmd( ch, "devour corpse" );
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * Exported vtable
 * ----------------------------------------------------------------------- */

const BOT_CLASS_AI bot_werewolf_ai = {
    bot_ww_should_train,    /* should_train   */
    bot_ww_do_train,        /* do_train       */
    bot_ww_buff_check,      /* buff_check     */
    bot_ww_combat_action,   /* combat_action  */
    bot_ww_between_fights   /* between_fights */
};
