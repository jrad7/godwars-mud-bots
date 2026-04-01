/*
 * bot_gear.c - Equipment management for Dystopia MUD bots
 *
 * Handles gear acquisition and slot maintenance once per bot update tick.
 * The single exported function bot_gear_check() does exactly ONE action per
 * call so gear upgrades happen gradually and don't flood the command log.
 *
 * Priority each tick:
 *   1. Ensure the bot is standing (class armor commands require POS_STANDING).
 *   2. Extract one surplus managed item from inventory (newbiepack or class
 *      gear that isn't worn).  Keeps the carry list clean.
 *   3. Unclassed bot: fill every empty newbiepack-eligible slot.
 *   4. Classed bot: walk the class gear table in slot order.
 *      - Slot has class gear            → skip.
 *      - Slot has newbiepack + primal   → unequip newbiepack, create class piece, wear all.
 *      - Slot has newbiepack, no primal → leave it, try next slot.
 *      - Slot empty + primal            → create class piece, wear all.
 *      - Slot empty, no primal          → fill with newbiepack (temporary).
 *   5. Newbiepack sweep for any slot covered by the newbiepack but absent
 *      from the class gear table (e.g. drow WEAR_NECK_2).
 *
 * Notes on implementation choices:
 *   - Surplus cleanup uses extract_obj() directly rather than drop+sacrifice
 *     to avoid keyword collisions when worn and unequipped items share names.
 *   - obj_to_char() prepends to ch->carrying, so a newly created class piece
 *     is at the head of the list and gets tried first by "wear all".  The
 *     displaced newbiepack stays in inventory and is extracted next tick.
 *   - Vampire and demon skip weapon slots: activating claws auto-drops any
 *     wielded item, making weapon creation pointless for those classes.
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
 * Vnum recognition helpers
 * ----------------------------------------------------------------------- */

static bool bot_is_newbiepack_vnum( int vnum )
{
    static const int nv[] = {
        30333, 30334, 30335, 30336, 30337, 30338,
        30339, 30340, 30342, 30343, 2622, 2204, 0
    };
    int i;
    for ( i = 0; nv[i] != 0; i++ )
        if ( nv[i] == vnum ) return TRUE;
    return FALSE;
}

/* All player-craftable class armor lives in the 33000-33199 range. */
static bool bot_is_class_gear_vnum( int vnum )
{
    return ( vnum >= 33000 && vnum <= 33199 );
}

/* -----------------------------------------------------------------------
 * Per-class gear tables
 *
 * Each row: { wear_slot, "command to issue", primal_cost }
 * Terminated by { WEAR_NONE, NULL, 0 }.
 *
 * Vampire: weapons omitted — claws (Protean 2) auto-drops wielded items.
 * Demon:   weapons omitted — claws (Attack 1) auto-drops wielded items.
 * ----------------------------------------------------------------------- */

static const BOT_GEAR_PIECE gear_vampire[] = {
    { WEAR_FINGER_L, "vamparmor ring",      60 },
    { WEAR_FINGER_R, "vamparmor ring",      60 },
    { WEAR_NECK_1,   "vamparmor collar",    60 },
    { WEAR_NECK_2,   "vamparmor collar",    60 },
    { WEAR_BODY,     "vamparmor plate",     60 },
    { WEAR_HEAD,     "vamparmor helmet",    60 },
    { WEAR_LEGS,     "vamparmor leggings",  60 },
    { WEAR_FEET,     "vamparmor boots",     60 },
    { WEAR_HANDS,    "vamparmor gloves",    60 },
    { WEAR_ARMS,     "vamparmor sleeves",   60 },
    { WEAR_ABOUT,    "vamparmor cape",      60 },
    { WEAR_WAIST,    "vamparmor belt",      60 },
    { WEAR_WRIST_L,  "vamparmor bracer",    60 },
    { WEAR_WRIST_R,  "vamparmor bracer",    60 },
    { WEAR_FACE,     "vamparmor visor",     60 },
    { WEAR_NONE, NULL, 0 }
};

static const BOT_GEAR_PIECE gear_monk[] = {
    { WEAR_FINGER_L, "monkarmor ring",      60 },
    { WEAR_FINGER_R, "monkarmor ring",      60 },
    { WEAR_NECK_1,   "monkarmor collar",    60 },
    { WEAR_NECK_2,   "monkarmor collar",    60 },
    { WEAR_BODY,     "monkarmor robe",      60 },
    { WEAR_HEAD,     "monkarmor helmet",    60 },
    { WEAR_LEGS,     "monkarmor shorts",    60 },
    { WEAR_FEET,     "monkarmor boots",     60 },
    { WEAR_HANDS,    "monkarmor gloves",    60 },
    { WEAR_ARMS,     "monkarmor sleeves",   60 },
    { WEAR_ABOUT,    "monkarmor cloak",     60 },
    { WEAR_WAIST,    "monkarmor belt",      60 },
    { WEAR_WRIST_L,  "monkarmor bracer",    60 },
    { WEAR_WRIST_R,  "monkarmor bracer",    60 },
    { WEAR_FACE,     "monkarmor mask",      60 },
    { WEAR_NONE, NULL, 0 }
};

static const BOT_GEAR_PIECE gear_ninja[] = {
    { WEAR_FINGER_L, "ninjaarmor ring",      60 },
    { WEAR_FINGER_R, "ninjaarmor ring",      60 },
    { WEAR_NECK_1,   "ninjaarmor collar",    60 },
    { WEAR_NECK_2,   "ninjaarmor collar",    60 },
    { WEAR_BODY,     "ninjaarmor robe",      60 },
    { WEAR_HEAD,     "ninjaarmor cap",       60 },
    { WEAR_LEGS,     "ninjaarmor leggings",  60 },
    { WEAR_FEET,     "ninjaarmor boots",     60 },
    { WEAR_HANDS,    "ninjaarmor gloves",    60 },
    { WEAR_ARMS,     "ninjaarmor sleeves",   60 },
    { WEAR_ABOUT,    "ninjaarmor cloak",     60 },
    { WEAR_WAIST,    "ninjaarmor belt",      60 },
    { WEAR_WRIST_L,  "ninjaarmor bracer",    60 },
    { WEAR_WRIST_R,  "ninjaarmor bracer",    60 },
    { WEAR_FACE,     "ninjaarmor mask",      60 },
    { WEAR_WIELD,    "ninjaarmor sword",     60 },
    { WEAR_HOLD,     "ninjaarmor dagger",    60 },
    { WEAR_NONE, NULL, 0 }
};

static const BOT_GEAR_PIECE gear_demon[] = {
    { WEAR_FINGER_L, "demonarmour ring",      60 },
    { WEAR_FINGER_R, "demonarmour ring",      60 },
    { WEAR_NECK_1,   "demonarmour collar",    60 },
    { WEAR_NECK_2,   "demonarmour collar",    60 },
    { WEAR_BODY,     "demonarmour plate",     60 },
    { WEAR_HEAD,     "demonarmour helmet",    60 },
    { WEAR_LEGS,     "demonarmour leggings",  60 },
    { WEAR_FEET,     "demonarmour boots",     60 },
    { WEAR_HANDS,    "demonarmour gauntlets", 60 },
    { WEAR_ARMS,     "demonarmour sleeves",   60 },
    { WEAR_ABOUT,    "demonarmour cape",      60 },
    { WEAR_WAIST,    "demonarmour belt",      60 },
    { WEAR_WRIST_L,  "demonarmour bracer",    60 },
    { WEAR_WRIST_R,  "demonarmour bracer",    60 },
    { WEAR_FACE,     "demonarmour visor",     60 },
    { WEAR_NONE, NULL, 0 }
};

static const BOT_GEAR_PIECE gear_drow[] = {
    { WEAR_FINGER_L, "drowcreate ring",      60 },
    { WEAR_FINGER_R, "drowcreate ring",      60 },
    { WEAR_NECK_1,   "drowcreate amulet",    60 },
    /* WEAR_NECK_2 intentionally absent: drow has only one neck piece.
     * Step 5 (newbiepack sweep) will fill that slot with a newbie collar. */
    { WEAR_BODY,     "drowcreate armor",     60 },
    { WEAR_HEAD,     "drowcreate helmet",    60 },
    { WEAR_LEGS,     "drowcreate leggings",  60 },
    { WEAR_FEET,     "drowcreate boots",     60 },
    { WEAR_HANDS,    "drowcreate gauntlets", 60 },
    { WEAR_ARMS,     "drowcreate sleeves",   60 },
    { WEAR_ABOUT,    "drowcreate cloak",     60 },
    { WEAR_WAIST,    "drowcreate belt",      60 },
    { WEAR_WRIST_L,  "drowcreate bracer",    60 },
    { WEAR_WRIST_R,  "drowcreate bracer",    60 },
    { WEAR_FACE,     "drowcreate mask",      60 },
    { WEAR_WIELD,    "drowcreate whip",      60 },
    { WEAR_HOLD,     "drowcreate dagger",    60 },
    { WEAR_NONE, NULL, 0 }
};

/* Werewolf class gear uses moonarmour (CLASS_WEREWOLF command, costs 60 primal/piece).
 * Requires DISC_WERE_LUNA >= 2 — enforced by the discipline guard in bot_gear_check.
 * No wield/hold: werewolves fight with claws. */
static const BOT_GEAR_PIECE gear_werewolf[] = {
    { WEAR_FINGER_L, "moonarmour ring",     60 },
    { WEAR_FINGER_R, "moonarmour ring",     60 },
    { WEAR_NECK_1,   "moonarmour collar",   60 },
    { WEAR_NECK_2,   "moonarmour collar",   60 },
    { WEAR_BODY,     "moonarmour plate",    60 },
    { WEAR_HEAD,     "moonarmour helmet",   60 },
    { WEAR_LEGS,     "moonarmour leggings", 60 },
    { WEAR_FEET,     "moonarmour boots",    60 },
    { WEAR_HANDS,    "moonarmour gloves",   60 },
    { WEAR_ARMS,     "moonarmour sleeves",  60 },
    { WEAR_ABOUT,    "moonarmour cape",     60 },
    { WEAR_WAIST,    "moonarmour belt",     60 },
    { WEAR_WRIST_L,  "moonarmour bracer",   60 },
    { WEAR_WRIST_R,  "moonarmour bracer",   60 },
    { WEAR_FACE,     "moonarmour mask",     60 },
    { WEAR_NONE, NULL, 0 }
};

/* Indexed by BOT_CLASS_* */
const BOT_GEAR_PIECE *bot_class_gear[BOT_CLASS_COUNT] = {
    gear_vampire,   /* BOT_CLASS_VAMPIRE  */
    gear_monk,      /* BOT_CLASS_MONK     */
    gear_ninja,     /* BOT_CLASS_NINJA    */
    gear_demon,     /* BOT_CLASS_DEMON    */
    gear_drow,      /* BOT_CLASS_DROW     */
    gear_werewolf   /* BOT_CLASS_WEREWOLF */
};

/* -----------------------------------------------------------------------
 * Newbiepack slot table
 *
 * Maps each newbiepack vnum to the specific WEAR_* slot it occupies.
 * Only slots actually covered by a newbiepack piece are listed:
 *   - WEAR_ABOUT absent: no newbiepack cape/cloak exists.
 * Terminated by { -1, 0 }.
 * ----------------------------------------------------------------------- */
typedef struct { int wear_slot; int vnum; } NEWBIE_SLOT;

static const NEWBIE_SLOT newbie_slots[] = {
    { WEAR_FINGER_L, 30342 },   /* newbie ring   */
    { WEAR_FINGER_R, 30342 },   /* newbie ring   */
    { WEAR_NECK_1,   30343 },   /* newbie collar */
    { WEAR_NECK_2,   30343 },   /* newbie collar */
    { WEAR_BODY,     30333 },   /* newbie breastplate */
    { WEAR_HEAD,     30334 },   /* newbie helmet */
    { WEAR_LEGS,     30336 },   /* newbie leggings */
    { WEAR_FEET,     30338 },   /* newbie boots  */
    { WEAR_HANDS,    30337 },   /* newbie gauntlets */
    { WEAR_ARMS,     30335 },   /* newbie sleeves */
    { WEAR_WAIST,    2204  },   /* newbie belt   */
    { WEAR_WRIST_L,  30339 },   /* newbie bracer */
    { WEAR_WRIST_R,  30339 },   /* newbie bracer */
    { WEAR_WIELD,    30340 },   /* newbie sword  */
    { WEAR_HOLD,     30340 },   /* newbie sword (offhand) */
    { WEAR_FACE,     2622  },   /* newbie mask   */
    { -1, 0 }
};

/* -----------------------------------------------------------------------
 * bot_spawn_obj - create a single object by vnum into ch's inventory.
 * Returns the new object pointer, NULL if the vnum isn't loaded.
 * ----------------------------------------------------------------------- */
static OBJ_DATA *bot_spawn_obj( CHAR_DATA *ch, int vnum )
{
    OBJ_INDEX_DATA *pIdx = get_obj_index( vnum );
    OBJ_DATA       *obj;

    if ( pIdx == NULL ) return NULL;
    obj = create_object( pIdx, 50 );
    obj_to_char( obj, ch );
    return obj;
}

/* -----------------------------------------------------------------------
 * bot_fill_newbie_slot - ensure one wear slot has a newbiepack piece.
 *
 * Reuses an existing unworn newbiepack item if one is already in inventory;
 * only spawns a fresh one when needed.  Issues "wear all" after placing
 * the item so the engine assigns it to the correct slot.
 * Returns TRUE if an action was taken (caller should return immediately).
 * ----------------------------------------------------------------------- */
static bool bot_fill_newbie_slot( CHAR_DATA *ch, int wear_slot, int vnum )
{
    OBJ_DATA *obj;

    if ( get_eq_char( ch, wear_slot ) != NULL )
        return FALSE;   /* slot already occupied */

    /* Re-use an unworn newbiepack piece already in inventory */
    for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
    {
        if ( obj->wear_loc == WEAR_NONE && obj->pIndexData->vnum == vnum )
        {
            bot_cmd( ch, "wear all" );
            return TRUE;
        }
    }

    /* Spawn a new piece and wear it */
    if ( bot_spawn_obj( ch, vnum ) != NULL )
    {
        bot_cmd( ch, "wear all" );
        return TRUE;
    }

    return FALSE;
}

/* -----------------------------------------------------------------------
 * bot_gear_check - main equipment management tick.
 * Called from bot_ensure_geared() once per bot update (1 Hz).
 * ----------------------------------------------------------------------- */
void bot_gear_check( CHAR_DATA *ch )
{
    BOT_DATA             *bot;
    const BOT_GEAR_PIECE *table;
    const BOT_GEAR_PIECE *entry;
    OBJ_DATA             *obj;
    OBJ_DATA             *obj_next;
    OBJ_DATA             *current;
    int                   class_pref;
    int                   i;
    bool                  in_table;

    if ( ch == NULL || ch->pcdata == NULL ) return;
    bot = ch->pcdata->botdata;
    if ( bot == NULL || bot->roster == NULL ) return;

    /* Never manage gear mid-combat (POS_FIGHTING=8 < POS_STANDING=9) */
    if ( ch->position == POS_FIGHTING )
        return;

    /* Step 1: must be standing — class armor commands require POS_STANDING */
    if ( ch->position < POS_STANDING )
    {
        do_stand( ch, "" );
        return;
    }

    /* Step 2: extract one surplus managed inventory item per tick.
     * "Managed" = newbiepack or class-gear vnum, currently not worn.
     * Direct extract avoids keyword collisions when worn and unequipped
     * items share the same name (e.g. two "newbie ring" items). */
    for ( obj = ch->carrying; obj != NULL; obj = obj_next )
    {
        obj_next = obj->next_content;
        if ( obj->wear_loc != WEAR_NONE ) continue;
        if ( !bot_is_newbiepack_vnum( obj->pIndexData->vnum )
          && !bot_is_class_gear_vnum(  obj->pIndexData->vnum ) )
            continue;
        extract_obj( obj );
        return;
    }

    class_pref = bot->roster->class_pref;
    table      = bot_class_gear[class_pref];

    /* Step 3: unclassed bot — fill every empty newbiepack-eligible slot */
    if ( ch->class == 0 )
    {
        for ( i = 0; newbie_slots[i].wear_slot >= 0; i++ )
        {
            if ( bot_fill_newbie_slot( ch, newbie_slots[i].wear_slot,
                                           newbie_slots[i].vnum ) )
                return;
        }
        return;
    }

    /* Werewolf moonarmour requires DISC_WERE_LUNA >= 2.
     * Skip class gear entirely until the discipline is learned;
     * newbiepack fills slots in the meantime via step 5. */
    if ( class_pref == BOT_CLASS_WEREWOLF
      && ch->power[DISC_WERE_LUNA] < 2 )
        goto newbiepack_sweep;

    /* Step 4: classed bot — class-gear pass (one slot upgraded per tick).
     *
     * obj_to_char() prepends to ch->carrying, so a class piece just created
     * via bot_cmd is at the head of the list and gets tried first by
     * "wear all".  The displaced newbiepack stays in inventory and is
     * extracted by step 2 on the following tick. */
    for ( entry = table; entry->wear_slot != WEAR_NONE; entry++ )
    {
        current = get_eq_char( ch, entry->wear_slot );

        /* Already has class gear here — nothing to do */
        if ( current != NULL
          && bot_is_class_gear_vnum( current->pIndexData->vnum ) )
            continue;

        /* Slot has newbiepack gear — try to upgrade if we can afford it */
        if ( current != NULL
          && bot_is_newbiepack_vnum( current->pIndexData->vnum ) )
        {
            if ( ch->practice >= entry->primal_cost )
            {
                unequip_char( ch, current );        /* → inventory */
                bot_cmd( ch, entry->cmd );          /* class piece → HEAD of inventory */
                bot_cmd( ch, "wear all" );          /* class piece worn, newbiepack stays */
                return;
            }
            continue;   /* can't afford yet; leave newbiepack, try next slot */
        }

        /* Slot is empty */
        if ( ch->practice >= entry->primal_cost )
        {
            bot_cmd( ch, entry->cmd );
            bot_cmd( ch, "wear all" );
            return;
        }

        /* Can't afford class gear — fill with newbiepack as temporary cover */
        for ( i = 0; newbie_slots[i].wear_slot >= 0; i++ )
        {
            if ( newbie_slots[i].wear_slot != entry->wear_slot ) continue;
            if ( bot_fill_newbie_slot( ch, entry->wear_slot,
                                           newbie_slots[i].vnum ) )
                return;
            break;   /* found entry; slot has no newbiepack equivalent — leave empty */
        }
        /* No newbiepack covers this slot (e.g. WEAR_ABOUT) — leave empty for now */
    }

    /* Step 5: newbiepack sweep for slots covered by the newbiepack but absent
     * from the class gear table (example: drow WEAR_NECK_2 — drow has only
     * one neck piece so WEAR_NECK_2 is not in gear_drow[]).  These slots
     * stay filled with newbiepack permanently.
     * Also the landing point for the werewolf Luna-2 discipline guard above. */
    newbiepack_sweep:
    for ( i = 0; newbie_slots[i].wear_slot >= 0; i++ )
    {
        in_table = FALSE;
        for ( entry = table; entry->wear_slot != WEAR_NONE; entry++ )
        {
            if ( entry->wear_slot == newbie_slots[i].wear_slot )
            {
                in_table = TRUE;
                break;
            }
        }
        if ( in_table ) continue;   /* handled by step 4 */

        if ( bot_fill_newbie_slot( ch, newbie_slots[i].wear_slot,
                                       newbie_slots[i].vnum ) )
            return;
    }

}
