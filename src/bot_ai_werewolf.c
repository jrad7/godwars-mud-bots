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
 * Priority rationale:
 *   1. Boar 3   — incoming dam /= 2; biggest single survivability jump
 *   2. Bear 5   — dam *= 1.2; 20% melee damage boost
 *   3. Lynx 2   — +2 extra attacks per round (multi_hit)
 *   4. Raptor 1 — auto rfangs attack every round
 *   5. Spider 1 — passive poison on every hit
 *   6. Luna 1   — flameclaws toggle (Luna 1 is the cheapest unlock)
 *   6b.Luna 2   — moonarmour unlock (gear system requires DISC_WERE_LUNA >= 2)
 *   7. Bear 7   — skin (−100 armor) and rend toggles
 *   8. Bear 8   — slam auto-proc (1 in 2–5 rounds depending on Bear lvl)
 *   9. Hawk 5   — quills toggle (extra multi_hit per round)
 *  10. Wolf 2   — rage/wolfman form
 *  11. Mantis 3 — dodge/disarm-resist bonus
 *  12. Raptor 3 — perception toggle
 *  13. Boar 7   — extra attacks from move pool
 *  14. Wolf 4   — razorclaws (requires claws active)
 *  15. Raptor 8 — jawlock (prevents target fleeing)
 *  16. Luna 3   — motherstouch self-heal in combat
 *  17. Spider 2 — web command
 *  18. Raptor 10 — talons burst (2k–4k vs NPCs)
 *  19. Luna 8   — moonbeam (500-mana burst)
 *  20. Owl 5    — staredown (force flee)
 *  21. Pain 10  — +750 max_dam flat
 *  22. Mantis 6 — full dodge/parry bonus
 *  23. Owl 8    — cocoon (gnosis-gated dam/2)
 *  24. Max remaining disciplines to 10
 * ----------------------------------------------------------------------- */

static int bot_ww_pick_research( CHAR_DATA *ch )
{
    static const struct { int disc; int target; } prio[] = {
        /* Luna 1: flameclaws toggle */
        { DISC_WERE_LUNA, 1  },
        /* Luna 2: moonarmour unlock (DISC_WERE_LUNA >= 2 required by bot gear) */
        { DISC_WERE_LUNA, 2  },
        /* Boar 3: halves all incoming damage */
        { DISC_WERE_BOAR, 3  },
        /* Bear 5: 20% outgoing damage boost */
        { DISC_WERE_BEAR, 5  },
        /* Lynx 2: +2 extra attacks */
        { DISC_WERE_LYNX, 2  },
        /* Raptor 1: auto rfangs every round */
        { DISC_WERE_RAPT, 1  },
        /* Spider 1: passive poison on hit */
        { DISC_WERE_SPID, 1  },
        /* Bear 7: skin + rend */
        { DISC_WERE_BEAR, 7  },
        /* Bear 8: slam auto-proc */
        { DISC_WERE_BEAR, 8  },
        /* Hawk 5: quills toggle */
        { DISC_WERE_HAWK, 5  },
        /* Wolf 2: rage/wolfman form */
        { DISC_WERE_WOLF, 2  },
        /* Mantis 3: dodge and disarm-resist bonus */
        { DISC_WERE_MANT, 3  },
        /* Raptor 3: perception toggle */
        { DISC_WERE_RAPT, 3  },
        /* Boar 7: extra attacks from move pool */
        { DISC_WERE_BOAR, 7  },
        /* Wolf 4: razorclaws */
        { DISC_WERE_WOLF, 4  },
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
        /* Owl 5: staredown */
        { DISC_WERE_OWL,  5  },
        /* Pain 10: +750 max_dam */
        { DISC_WERE_PAIN, 10 },
        /* Mantis 6: full dodge/parry bonus */
        { DISC_WERE_MANT, 6  },
        /* Owl 8: cocoon */
        { DISC_WERE_OWL,  8  },
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
 *   2. rage        — wolfman form (Wolf 2); only if rage > 99
 *   3. quills      — extra quills attack each round (Hawk 5)
 *   4. skin        — −100 armor (Bear 7)
 *   5. slam        — auto shoulder slam (Bear 8)
 *   6. rend        — rend target equipment (Bear or Boar 7)
 *   7. perception  — detect hidden/stealthy (Raptor 3)
 *   8. jawlock     — prevent flee (Raptor 8)
 * ----------------------------------------------------------------------- */

static bool bot_ww_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_WEREWOLF) ) return FALSE;

    /* Flameclaws (Luna 1): flaming claw attacks */
    if ( ch->power[DISC_WERE_LUNA] >= 1
      && !IS_SET(ch->newbits, NEW_MONKFLAME) )
    { bot_cmd( ch, "flameclaws" ); return TRUE; }

    /* Rage / wolfman form (Wolf 2): enter only after rage > 99
     * so the max_dam bonus is already in effect when we transform */
    if ( ch->power[DISC_WERE_WOLF] >= 2
      && !IS_SET(ch->special, SPC_WOLFMAN)
      && ch->rage > 99 )
    { bot_cmd( ch, "rage" ); return TRUE; }

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
 *   4 (78%): roar       — Bear 6, 1-in-6 force flee
 *   5 (88%): staredown  — Owl 5, high force-flee rate vs NPCs
 *   6 (95%): web        — Spider 2, immobilize target
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

    /* Priority 4 (78%): roar — force flee (1-in-6 chance, 18-beat lag) */
    if ( ch->power[DISC_WERE_BEAR] >= 6
      && roll <= 78 )
    {
        bot_cmd( ch, "roar" );
        return;
    }

    /* Priority 5 (88%): staredown — Owl 5 force flee
     * Owl 5 NPC: ~2/3 success; Owl 6+ NPC: near-certain; 16-beat lag */
    if ( ch->power[DISC_WERE_OWL] >= 5
      && roll <= 88 )
    {
        sprintf( cmd, "staredown %s", tname );
        bot_cmd( ch, cmd );
        return;
    }

    /* Priority 6 (95%): web — immobilize target */
    if ( ch->power[DISC_WERE_SPID] >= 2
      && roll <= 95 )
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
 * Steps:
 *   1. Build rage to > 99 via `rage` command (Wolf 2+, not already wolfman)
 *   2. Devour a corpse in the room if injured (Raptor 5+)
 * ----------------------------------------------------------------------- */

static bool bot_ww_between_fights( CHAR_DATA *ch )
{
    OBJ_DATA *obj;

    if ( !IS_CLASS(ch, CLASS_WEREWOLF) ) return FALSE;

    /* Step 1: build rage above 99 for the max_dam bonus
     * Each call to `rage` (outside wolfman) adds ~40-60 rage.
     * Once rage > 99, enter wolfman form via buff_check instead. */
    if ( ch->power[DISC_WERE_WOLF] >= 2
      && !IS_SET(ch->special, SPC_WOLFMAN)
      && ch->rage < 100 )
    {
        bot_cmd( ch, "rage" );
        return TRUE;
    }

    /* Step 2: devour NPC corpse in room to recover HP */
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
