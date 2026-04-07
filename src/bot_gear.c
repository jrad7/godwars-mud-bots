/*
 * bot_gear.c - Equipment management for Dystopia MUD bots
 *
 * Handles gear acquisition and slot maintenance once per bot update tick.
 * The single exported function bot_gear_check() does exactly ONE action per
 * call so gear upgrades happen gradually and don't flood the command log.
 *
 * Priority each tick:
 *   1. Ensure the bot is standing (class armor commands require POS_STANDING).
 *   2a. Extract any unworn wield/hold item (looted weapons bots can't use).
 *   2b. Extract one surplus managed item from inventory (newbiepack or class
 *       gear that isn't worn).  Keeps the carry list clean.
 *   3. Unclassed bot: fill every empty newbiepack-eligible slot.
 *   4. Classed bot: walk the class gear table in slot order.
 *      - Slot has class gear            → skip.
 *      - Slot has newbiepack + primal   → unequip newbiepack, create class piece, wear_obj it.
 *      - Slot has newbiepack, no primal → leave it, try next slot.
 *      - Slot empty + primal            → create class piece, wear_obj it.
 *      - Slot empty, no primal          → fill with newbiepack (temporary).
 *   5. Newbiepack sweep for any slot covered by the newbiepack but absent
 *      from the class gear table (e.g. drow WEAR_NECK_2).
 *
 * Notes on implementation choices:
 *   - Surplus cleanup uses extract_obj() directly rather than drop+sacrifice
 *     to avoid keyword collisions when worn and unequipped items share names.
 *   - obj_to_char() prepends to ch->carrying, so a newly created class piece
 *     is always at ch->carrying after the creation command.  wear_obj() is
 *     called directly on that pointer rather than "wear all" to avoid hitting
 *     unrelated wieldable items in inventory (e.g. looted weapons, wolfman
 *     hand-slot blocks) that would spam error messages each tick.
 *     The displaced newbiepack stays in inventory and is extracted next tick.
 *   - Vampire and demon wield class weapons (vamparmor longsword/dagger,
 *     demonarmour longsword/shortsword) — far higher damage dice than claws.
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

/* All player-craftable class armor lives in the 33000-33299 range. */
static bool bot_is_class_gear_vnum( int vnum )
{
    return ( vnum >= 33000 && vnum <= 33299 );
}

/* -----------------------------------------------------------------------
 * Per-class gear tables
 *
 * Each row: { wear_slot, "command to issue", primal_cost }
 * Terminated by { WEAR_NONE, NULL, 0 }.
 *
 * Vampire: longsword (33040, 25d50 avg 637) + dagger (33041) — do NOT use claws.
 * Demon:   longsword (33120, 50d75 avg 1900) + shortsword (33121) — do NOT use claws.
 * ----------------------------------------------------------------------- */

static const BOT_GEAR_PIECE gear_vampire[] = {
    { WEAR_WIELD,    "vamparmor longsword", 60 },
    { WEAR_HOLD,     "vamparmor dagger",    60 },
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
    { WEAR_WIELD,    "ninjaarmor sword",     60 },
    { WEAR_HOLD,     "ninjaarmor dagger",    60 },
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
    { WEAR_NONE, NULL, 0 }
};

static const BOT_GEAR_PIECE gear_demon[] = {
    { WEAR_WIELD,    "demonarmour longsword",   60 },
    { WEAR_HOLD,     "demonarmour shortsword",  60 },
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
    { WEAR_WIELD,    "drowcreate whip",      60 },
    { WEAR_HOLD,     "drowcreate dagger",    60 },
    { WEAR_FINGER_L, "drowcreate ring",      60 },
    { WEAR_FINGER_R, "drowcreate ring",      60 },
    { WEAR_NECK_1,   "drowcreate amulet",    60 },
    { WEAR_NECK_2,   "drowcreate amulet",    60 },
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

static const BOT_GEAR_PIECE gear_mage[] = {
    { WEAR_WIELD,    "magearmor staff",    60 },
    { WEAR_HOLD,     "magearmor dagger",   60 },
    { WEAR_FINGER_L, "magearmor ring",     60 },
    { WEAR_FINGER_R, "magearmor ring",     60 },
    { WEAR_NECK_1,   "magearmor collar",   60 },
    { WEAR_NECK_2,   "magearmor collar",   60 },
    { WEAR_BODY,     "magearmor robe",     60 },
    { WEAR_HEAD,     "magearmor cap",      60 },
    { WEAR_LEGS,     "magearmor leggings", 60 },
    { WEAR_FEET,     "magearmor boots",    60 },
    { WEAR_HANDS,    "magearmor gloves",   60 },
    { WEAR_ARMS,     "magearmor sleeves",  60 },
    { WEAR_ABOUT,    "magearmor cape",     60 },
    { WEAR_WAIST,    "magearmor belt",     60 },
    { WEAR_WRIST_L,  "magearmor bracer",   60 },
    { WEAR_WRIST_R,  "magearmor bracer",   60 },
    { WEAR_FACE,     "magearmor mask",     60 },
    { WEAR_NONE, NULL, 0 }
};

/* Tanar'ri: claymore wield, 13 armor slots. Command: taneq. Cost: 150 primal.
 * No hold slot — only one weapon piece exists (claymore, vnum 33200). */
static const BOT_GEAR_PIECE gear_tanarri[] = {
    { WEAR_WIELD,    "taneq claymore",  150 },
    { WEAR_HOLD,     "taneq claymore",  150 },
    { WEAR_FINGER_L, "taneq ring",      150 },
    { WEAR_FINGER_R, "taneq ring",      150 },
    { WEAR_NECK_1,   "taneq collar",    150 },
    { WEAR_NECK_2,   "taneq collar",    150 },
    { WEAR_BODY,     "taneq plate",     150 },
    { WEAR_HEAD,     "taneq helmet",    150 },
    { WEAR_LEGS,     "taneq leggings",  150 },
    { WEAR_FEET,     "taneq boots",     150 },
    { WEAR_HANDS,    "taneq gauntlets", 150 },
    { WEAR_ARMS,     "taneq sleeves",   150 },
    { WEAR_ABOUT,    "taneq cloak",     150 },
    { WEAR_WAIST,    "taneq belt",      150 },
    { WEAR_WRIST_L,  "taneq bracer",    150 },
    { WEAR_WRIST_R,  "taneq bracer",    150 },
    { WEAR_FACE,     "taneq visor",     150 },
    { WEAR_NONE, NULL, 0 }
};

/* Angel: sword wield, 13 armor slots. Command: angelicarmor. Cost: 150 primal.
 * No hold slot — only one weapon piece exists (sword, vnum 33192). */
static const BOT_GEAR_PIECE gear_angel[] = {
    { WEAR_WIELD,    "angelicarmor sword",     150 },
    { WEAR_HOLD,     "angelicarmor sword",     150 },
    { WEAR_FINGER_L, "angelicarmor ring",      150 },
    { WEAR_FINGER_R, "angelicarmor ring",      150 },
    { WEAR_NECK_1,   "angelicarmor necklace",  150 },
    { WEAR_NECK_2,   "angelicarmor necklace",  150 },
    { WEAR_BODY,     "angelicarmor plate",     150 },
    { WEAR_HEAD,     "angelicarmor helmet",    150 },
    { WEAR_LEGS,     "angelicarmor leggings",  150 },
    { WEAR_FEET,     "angelicarmor boots",     150 },
    { WEAR_HANDS,    "angelicarmor gauntlets", 150 },
    { WEAR_ARMS,     "angelicarmor sleeves",   150 },
    { WEAR_ABOUT,    "angelicarmor cloak",     150 },
    { WEAR_WAIST,    "angelicarmor belt",      150 },
    { WEAR_WRIST_L,  "angelicarmor bracer",    150 },
    { WEAR_WRIST_R,  "angelicarmor bracer",    150 },
    { WEAR_FACE,     "angelicarmor visor",     150 },
    { WEAR_NONE, NULL, 0 }
};

/* Indexed by BOT_CLASS_* */
const BOT_GEAR_PIECE *bot_class_gear[BOT_CLASS_COUNT] = {
    gear_vampire,   /* BOT_CLASS_VAMPIRE  */
    gear_monk,      /* BOT_CLASS_MONK     */
    gear_ninja,     /* BOT_CLASS_NINJA    */
    gear_demon,     /* BOT_CLASS_DEMON    */
    gear_drow,      /* BOT_CLASS_DROW     */
    gear_werewolf,  /* BOT_CLASS_WEREWOLF */
    gear_mage,      /* BOT_CLASS_MAGE     */
    gear_tanarri,   /* BOT_CLASS_TANARRI  */
    gear_angel      /* BOT_CLASS_ANGEL    */
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
 * bot_watch_wear - send a [GEAR] wear notice to any watchbot watcher.
 * ----------------------------------------------------------------------- */
static void bot_watch_wear( CHAR_DATA *ch, OBJ_DATA *obj, const char *type )
{
    char echo[256];
    if ( ch->desc == NULL || ch->desc->snoop_by == NULL ) return;
    snprintf( echo, sizeof(echo), "[GEAR] %s: wear '%s' (%s)\n\r",
        ch->name,
        obj->short_descr ? obj->short_descr : "?",
        type );
    write_to_buffer( ch->desc->snoop_by, echo, 0 );
}

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
 * Uses wear_obj() directly rather than "wear all" to avoid triggering error
 * messages from unrelated wieldable items in inventory.
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
            bot_watch_wear( ch, obj, "newbiepack" );
            wear_obj( ch, obj, TRUE );
            return TRUE;
        }
    }

    /* Spawn a new piece and wear it */
    obj = bot_spawn_obj( ch, vnum );
    if ( obj != NULL )
    {
        bot_watch_wear( ch, obj, "newbiepack" );
        wear_obj( ch, obj, TRUE );
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
    bool                  force_newbiepack;   /* TRUE: ignore in_table in sweep */

    if ( ch == NULL || ch->pcdata == NULL ) return;
    bot = ch->pcdata->botdata;
    if ( bot == NULL || bot->roster == NULL ) return;

    /* Never manage gear mid-combat (POS_FIGHTING=8 < POS_STANDING=9) */
    if ( ch->position == POS_FIGHTING )
        return;


    /* Step 1: must be standing — class armor commands require POS_STANDING.
     * If bot is intentionally meditating (needs_meditate+ready_meditate both
     * set) leave it alone — gear is already complete. */
    if ( ch->position < POS_STANDING )
    {
        if ( bot->needs_meditate && bot->ready_meditate )
            return;   /* meditating intentionally — don't disturb */
        do_stand( ch, "" );
        return;
    }

    class_pref        = bot->roster->class_pref;
    table             = bot_class_gear[class_pref];

    /* Step 1.5: recover called-back class gear from inventory.
     * After a decap the bot calls gear back and it lands in inventory.
     * Skip while decap_recovery is still set — call all hasn't been issued yet.
     *
     * Surplus duplicate guard: after avatar-retrain + call all a bot may end up
     * with an extra copy of a class piece (e.g. two pairs of boots).  Wearing a
     * duplicate would displace the equipped copy into inventory, creating an
     * infinite swap loop.  Before wearing, verify all gear-table slots that need
     * this vnum aren't already fully satisfied. */
    if ( ch->class != 0 && !bot->decap_recovery )
    {
        for ( obj = ch->carrying; obj != NULL; obj = obj->next_content )
        {
            const BOT_GEAR_PIECE *ge;
            const char           *matched_cmd;
            int                   matched_count;
            int                   needed_count;

            if ( obj->wear_loc != WEAR_NONE ) continue;
            if ( !bot_is_class_gear_vnum( obj->pIndexData->vnum ) ) continue;

            /* Find how many gear-table slots are already filled with this vnum,
             * and capture the cmd shared by those entries. */
            matched_cmd   = NULL;
            matched_count = 0;
            for ( ge = table; ge->wear_slot != WEAR_NONE; ge++ )
            {
                OBJ_DATA *worn = get_eq_char( ch, ge->wear_slot );
                if ( worn != NULL
                  && worn->pIndexData->vnum == obj->pIndexData->vnum )
                {
                    matched_cmd = ge->cmd;
                    matched_count++;
                }
            }

            /* If no worn item shares this vnum yet, the slot is free — wear it. */
            if ( matched_cmd == NULL )
            {
                bot_watch_wear( ch, obj, "called gear" );
                wear_obj( ch, obj, TRUE );
                bot->limb_gear_call = FALSE;  /* gear confirmed back; episode over */
                return;
            }

            /* Count how many table entries need this same vnum (e.g. 2 for rings,
             * 1 for boots).  If all are already filled, this piece is surplus;
             * step 2b will extract it next tick. */
            needed_count = 0;
            for ( ge = table; ge->wear_slot != WEAR_NONE; ge++ )
            {
                if ( strcmp( ge->cmd, matched_cmd ) == 0 )
                    needed_count++;
            }

            if ( matched_count >= needed_count )
            {
                bot_watch_wear( ch, obj, "surplus-skip" );
                continue;   /* surplus duplicate — step 2b will extract it */
            }

            bot_watch_wear( ch, obj, "called gear" );
            wear_obj( ch, obj, TRUE );
            bot->limb_gear_call = FALSE;  /* gear confirmed back; episode over */
            return;
        }
    }

    /* Step 2a: extract any unworn wield/hold item from inventory.
     * Bots loot weapons from mobs; werewolves and claw classes never wield,
     * and wolfman form blocks hand slots — these items just cause noise. */
    for ( obj = ch->carrying; obj != NULL; obj = obj_next )
    {
        obj_next = obj->next_content;
        if ( obj->wear_loc != WEAR_NONE ) continue;
        if ( CAN_WEAR( obj, ITEM_WIELD ) || CAN_WEAR( obj, ITEM_HOLD ) )
        {
            extract_obj( obj );
            return;
        }
    }

    /* Step 2b: extract one surplus managed inventory item per tick.
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
        /* Preserve class gear during decap recovery — it was called back by
         * behead() and must survive until call all is issued and step 1.5
         * wears it. */
        if ( bot->decap_recovery && bot_is_class_gear_vnum( obj->pIndexData->vnum ) )
            continue;
        extract_obj( obj );
        return;
    }

    force_newbiepack  = FALSE;

    /* Step 3: unclassed bot or decap recovery — fill newbiepack slots only.
     * During decap_recovery the bot is re-training; class gear must not be
     * created or worn until call all has been issued and the flag cleared. */
    if ( ch->class == 0 || bot->decap_recovery )
    {
        for ( i = 0; newbie_slots[i].wear_slot >= 0; i++ )
        {
            if ( bot_fill_newbie_slot( ch, newbie_slots[i].wear_slot,
                                           newbie_slots[i].vnum ) )
                return;
        }
        bot->ready_meditate = TRUE;
        return;
    }

    /* Werewolf moonarmour requires DISC_WERE_LUNA >= 2.
     * Skip class gear entirely until the discipline is learned;
     * newbiepack fills slots in the meantime via step 5. */
    if ( class_pref == BOT_CLASS_WEREWOLF
      && ch->power[DISC_WERE_LUNA] < 2 )
    {
        force_newbiepack = TRUE;
        goto newbiepack_sweep;
    }

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
                OBJ_DATA *before = ch->carrying;
                OBJ_DATA *created;
                unequip_char( ch, current );        /* → inventory */
                bot_cmd( ch, entry->cmd );          /* class piece → HEAD of ch->carrying */
                created = ( ch->carrying != before ) ? ch->carrying : NULL;
                if ( created != NULL && created->wear_loc == WEAR_NONE )
                {
                    bot_watch_wear( ch, created, "class gear" );
                    wear_obj( ch, created, TRUE );
                }
                return;
            }
            {
                time_t now = time(NULL);
                if ( ch->desc != NULL && ch->desc->snoop_by != NULL
                  && now - bot->last_gear_warn >= 60 )
                {
                    char echo[256];
                    snprintf( echo, sizeof(echo),
                        "[GEAR] %s: skip '%s' -- need %d primal, have %d\n\r",
                        ch->name, entry->cmd, entry->primal_cost, ch->practice );
                    write_to_buffer( ch->desc->snoop_by, echo, 0 );
                    bot->last_gear_warn = now;
                }
            }
            continue;   /* can't afford yet; leave newbiepack, try next slot */
        }

        /* Slot is empty */
        if ( ch->practice >= entry->primal_cost )
        {
            OBJ_DATA *before  = ch->carrying;
            OBJ_DATA *created;

            /* Before crafting, try "call all" once — the piece may have been
             * severed off during PvP and is still claimed by the bot.  If call
             * all retrieves it, step 1.5 will wear it next tick and clear
             * limb_gear_call.  If nothing comes back (gear was never made or
             * call all already fired), limb_gear_call is TRUE on the next pass
             * and we fall through to craft normally. */
            if ( !bot->limb_gear_call )
            {
                bot->limb_gear_call = TRUE;
                bot_cmd( ch, "call all" );
                return;
            }

            bot_cmd( ch, entry->cmd );              /* class piece → HEAD of ch->carrying */
            created = ( ch->carrying != before ) ? ch->carrying : NULL;
            if ( created != NULL && created->wear_loc == WEAR_NONE )
            {
                bot_watch_wear( ch, created, "class gear" );
                wear_obj( ch, created, TRUE );
            }
            bot->limb_gear_call = FALSE;  /* crafted fresh; reset for next episode */
            return;
        }

        /* Can't afford class gear — fill with newbiepack as temporary cover */
        {
            time_t now = time(NULL);
            if ( ch->desc != NULL && ch->desc->snoop_by != NULL
              && now - bot->last_gear_warn >= 60 )
            {
                char echo[256];
                snprintf( echo, sizeof(echo),
                    "[GEAR] %s: slot empty, skip '%s' -- need %d primal, have %d\n\r",
                    ch->name, entry->cmd, entry->primal_cost, ch->practice );
                write_to_buffer( ch->desc->snoop_by, echo, 0 );
                bot->last_gear_warn = now;
            }
        }
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
     * from the class gear table.  These slots
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
        if ( in_table && !force_newbiepack )
            continue;   /* handled by step 4 */

        /* Skip wield/hold slots for classes that don't have them in their
         * gear table (monk, vampire, demon, werewolf).  wear_obj would fail
         * and spam an error message every tick. */
        if ( newbie_slots[i].wear_slot == WEAR_WIELD
          || newbie_slots[i].wear_slot == WEAR_HOLD )
            continue;

        if ( bot_fill_newbie_slot( ch, newbie_slots[i].wear_slot,
                                       newbie_slots[i].vnum ) )
            return;
    }

    /* Fell through with nothing to do — gear is complete. */
    bot->ready_meditate = TRUE;
}

/* -----------------------------------------------------------------------
 * bot_is_gearing - TRUE while the bot still has empty newbiepack slots.
 *
 * Used by the grinding state to delay combat until the bot is fully
 * equipped.  Only checks slots that the newbiepack covers (wield/hold
 * excluded for classes that fight bare-handed).
 * ----------------------------------------------------------------------- */
bool bot_is_gearing( CHAR_DATA *ch )
{
    int i;
    int class_pref;
    const BOT_GEAR_PIECE *table;
    const BOT_GEAR_PIECE *entry;
    bool in_table;

    if ( ch == NULL || ch->pcdata == NULL ) return FALSE;
    if ( ch->pcdata->botdata == NULL || ch->pcdata->botdata->roster == NULL )
        return FALSE;

    class_pref = ch->pcdata->botdata->roster->class_pref;
    table      = bot_class_gear[class_pref];

    for ( i = 0; newbie_slots[i].wear_slot >= 0; i++ )
    {
        int slot = newbie_slots[i].wear_slot;

        /* Skip wield/hold for classes that fight bare-handed */
        if ( slot == WEAR_WIELD || slot == WEAR_HOLD )
        {
            in_table = FALSE;
            for ( entry = table; entry->wear_slot != WEAR_NONE; entry++ )
            {
                if ( entry->wear_slot == slot ) { in_table = TRUE; break; }
            }
            if ( !in_table ) continue;
        }

        if ( get_eq_char( ch, slot ) == NULL )
            return TRUE;
    }

    return FALSE;
}
