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

/* Player-craftable class armor ranges (classeq.are + knightarmor command). */
static bool bot_is_class_gear_vnum( int vnum )
{
    if ( vnum >= 33000 && vnum <= 33014 ) return TRUE;  /* mage */
    if ( vnum >= 33020 && vnum <= 33032 ) return TRUE;  /* monk */
    if ( vnum >= 33040 && vnum <= 33055 ) return TRUE;  /* vampire */
    if ( vnum >= 33060 && vnum <= 33074 ) return TRUE;  /* drow */
    if ( vnum >= 33080 && vnum <= 33094 ) return TRUE;  /* ninja */
    if ( vnum >= 33100 && vnum <= 33114 ) return TRUE;  /* werewolf */
    if ( vnum >= 33120 && vnum <= 33134 ) return TRUE;  /* demon */
    if ( vnum >= 33140 && vnum <= 33153 ) return TRUE;  /* drider */
    if ( vnum >= 33160 && vnum <= 33177 ) return TRUE;  /* shapeshifter */
    if ( vnum >= 33180 && vnum <= 33193 ) return TRUE;  /* angel */
    if ( vnum >= 33200 && vnum <= 33213 ) return TRUE;  /* tanarri */
    if ( vnum >= 33220 && vnum <= 33233 ) return TRUE;  /* lich */
    if ( vnum >= 33240 && vnum <= 33255 ) return TRUE;  /* hobbit */
    if ( vnum >= 33260 && vnum <= 33261 ) return TRUE;  /* fae */
    if ( vnum >= 33280 && vnum <= 33293 ) return TRUE;  /* giant */
    if ( vnum >= 33300 && vnum <= 33313 ) return TRUE;  /* drone (spiderdroid) */
    if ( vnum >= 29975 && vnum <= 29991 ) return TRUE;  /* undead knight */
    return FALSE;
}

/* Returns TRUE if vnum belongs to the player-craftable gear set for the
 * given BOT_CLASS_*.  Used after class upgrades (e.g. werewolf->shapeshifter)
 * to distinguish the bot's own class gear from leftover gear of its prior
 * class — which still passes bot_is_class_gear_vnum but should be replaced. */
static bool bot_is_own_class_gear_vnum( int class_pref, int vnum )
{
    switch ( class_pref )
    {
    case BOT_CLASS_MAGE:           return vnum >= 33000 && vnum <= 33014;
    case BOT_CLASS_MONK:           return vnum >= 33020 && vnum <= 33032;
    case BOT_CLASS_VAMPIRE:        return vnum >= 33040 && vnum <= 33055;
    case BOT_CLASS_DROW:           return vnum >= 33060 && vnum <= 33074;
    case BOT_CLASS_NINJA:          return vnum >= 33080 && vnum <= 33094;
    case BOT_CLASS_WEREWOLF:       return vnum >= 33100 && vnum <= 33114;
    case BOT_CLASS_DEMON:          return vnum >= 33120 && vnum <= 33134;
    case BOT_CLASS_DROID:          return vnum >= 33140 && vnum <= 33153;
    case BOT_CLASS_SHAPESHIFTER:   return vnum >= 33160 && vnum <= 33177;
    case BOT_CLASS_ANGEL:          return vnum >= 33180 && vnum <= 33193;
    case BOT_CLASS_TANARRI:        return vnum >= 33200 && vnum <= 33213;
    case BOT_CLASS_LICH:           return vnum >= 33220 && vnum <= 33233;
    case BOT_CLASS_SAMURAI:        return vnum >= 33240 && vnum <= 33255;
    case BOT_CLASS_UNDEAD_KNIGHT:  return vnum >= 29975 && vnum <= 29991;
    default:                       return FALSE;
    }
}

/* Map ch->class to the BOT_CLASS_* that matches an upgraded character.
 * Returns -1 if the class has no upgraded variant or doesn't match an
 * upgrade target (e.g. base werewolf is BOT_CLASS_WEREWOLF, not handled here). */
static int bot_pref_for_upgraded_class( int ch_class )
{
    switch ( ch_class )
    {
    case CLASS_SHAPESHIFTER:   return BOT_CLASS_SHAPESHIFTER;
    case CLASS_TANARRI:        return BOT_CLASS_TANARRI;
    case CLASS_ANGEL:          return BOT_CLASS_ANGEL;
    case CLASS_UNDEAD_KNIGHT:  return BOT_CLASS_UNDEAD_KNIGHT;
    case CLASS_DROID:          return BOT_CLASS_DROID;
    case CLASS_SAMURAI:        return BOT_CLASS_SAMURAI;
    case CLASS_LICH:           return BOT_CLASS_LICH;
    default:                   return -1;
    }
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

/* Werewolf class gear: klaive wield (33114, 30d60 avg 915) + moonarmour armor.
 * Klaive has SITEM_WOLFWEAPON so it survives the wolfman transform.
 * Moonarmour requires DISC_WERE_LUNA >= 2 — enforced by the discipline
 * guard in bot_gear_check.  Klaive has no discipline requirement. */
static const BOT_GEAR_PIECE gear_werewolf[] = {
    { WEAR_WIELD,    "klaive",              60 },
    { WEAR_HOLD,     "klaive",              60 },
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

/* Undead Knight: longsword wield + shortsword hold, 13 armor slots.
 * Command: knightarmor. Cost: 150 primal per piece.
 * Vnums 29975-29991 — outside normal 33000-33299 range, handled separately
 * in bot_is_class_gear_vnum(). */
static const BOT_GEAR_PIECE gear_undead_knight[] = {
    { WEAR_WIELD,    "knightarmor longsword", 150 },
    { WEAR_HOLD,     "knightarmor shortsword",150 },
    { WEAR_FINGER_L, "knightarmor ring",      150 },
    { WEAR_FINGER_R, "knightarmor ring",      150 },
    { WEAR_NECK_1,   "knightarmor collar",    150 },
    { WEAR_NECK_2,   "knightarmor collar",    150 },
    { WEAR_BODY,     "knightarmor plate",     150 },
    { WEAR_HEAD,     "knightarmor helmet",    150 },
    { WEAR_LEGS,     "knightarmor leggings",  150 },
    { WEAR_FEET,     "knightarmor boots",     150 },
    { WEAR_HANDS,    "knightarmor gauntlets", 150 },
    { WEAR_ARMS,     "knightarmor chains",    150 },
    { WEAR_ABOUT,    "knightarmor cloak",     150 },
    { WEAR_WAIST,    "knightarmor belt",      150 },
    { WEAR_WRIST_L,  "knightarmor bracer",    150 },
    { WEAR_WRIST_R,  "knightarmor bracer",    150 },
    { WEAR_FACE,     "knightarmor visor",     150 },
    { WEAR_NONE, NULL, 0 }
};

/* Shapeshifter: Uses shapearmor. Cost 150 primal per piece. */
static const BOT_GEAR_PIECE gear_shapeshifter[] = {
    { WEAR_WIELD,    "shapearmor knife",    150 },
    { WEAR_HOLD,     "shapearmor kane",     150 },
    { WEAR_FINGER_L, "shapearmor ring",     150 },
    { WEAR_FINGER_R, "shapearmor ring",     150 },
    { WEAR_NECK_1,   "shapearmor necklace", 150 },
    { WEAR_NECK_2,   "shapearmor necklace", 150 },
    { WEAR_BODY,     "shapearmor jacket",   150 },
    { WEAR_HEAD,     "shapearmor helmet",   150 },
    { WEAR_LEGS,     "shapearmor pants",    150 },
    { WEAR_FEET,     "shapearmor boots",    150 },
    { WEAR_HANDS,    "shapearmor gloves",   150 },
    { WEAR_ARMS,     "shapearmor shirt",    150 },
    { WEAR_ABOUT,    "shapearmor cloak",    150 },
    { WEAR_WAIST,    "shapearmor belt",     150 },
    { WEAR_WRIST_L,  "shapearmor bands",    150 },
    { WEAR_WRIST_R,  "shapearmor bands",    150 },
    { WEAR_FACE,     "shapearmor visor",    150 },
    { WEAR_NONE, NULL, 0 }
};

/* Droid: Uses dridereq. Cost 150 primal per piece. */
static const BOT_GEAR_PIECE gear_droid[] = {
    { WEAR_WIELD,    "dridereq whip",     150 },
    { WEAR_HOLD,     "dridereq whip",     150 },
    { WEAR_FINGER_L, "dridereq ring",     150 },
    { WEAR_FINGER_R, "dridereq ring",     150 },
    { WEAR_NECK_1,   "dridereq collar",   150 },
    { WEAR_NECK_2,   "dridereq collar",   150 },
    { WEAR_BODY,     "dridereq armor",    150 },
    { WEAR_HEAD,     "dridereq helmet",   150 },
    { WEAR_LEGS,     "dridereq leggings", 150 },
    { WEAR_FEET,     "dridereq boots",    150 },
    { WEAR_HANDS,    "dridereq gloves",   150 },
    { WEAR_ARMS,     "dridereq sleeves",  150 },
    { WEAR_ABOUT,    "dridereq cloak",    150 },
    { WEAR_WAIST,    "dridereq belt",     150 },
    { WEAR_WRIST_L,  "dridereq bracer",   150 },
    { WEAR_WRIST_R,  "dridereq bracer",   150 },
    { WEAR_FACE,     "dridereq mask",     150 },
    { WEAR_NONE, NULL, 0 }
};

/* Samurai: katana wield+hold, rest uses newbiepack gear from step 5 sweep. */
static const BOT_GEAR_PIECE gear_samurai[] = {
    { WEAR_WIELD,    "katana", 250 },
    { WEAR_HOLD,     "katana", 250 },
    { WEAR_NONE, NULL, 0 }
};

/* Lich: scythe wield, 13 armor slots. Command: licharmor. Cost: 150 primal. */
static const BOT_GEAR_PIECE gear_lich[] = {
    { WEAR_WIELD,    "licharmor scythe",    150 },
    { WEAR_HOLD,     "licharmor scythe",    150 },
    { WEAR_FINGER_L, "licharmor ring",      150 },
    { WEAR_FINGER_R, "licharmor ring",      150 },
    { WEAR_NECK_1,   "licharmor amulet",    150 },
    { WEAR_NECK_2,   "licharmor amulet",    150 },
    { WEAR_BODY,     "licharmor plate",     150 },
    { WEAR_HEAD,     "licharmor helmet",    150 },
    { WEAR_LEGS,     "licharmor leggings",  150 },
    { WEAR_FEET,     "licharmor boots",     150 },
    { WEAR_HANDS,    "licharmor gauntlets", 150 },
    { WEAR_ARMS,     "licharmor sleeves",   150 },
    { WEAR_ABOUT,    "licharmor cloak",     150 },
    { WEAR_WAIST,    "licharmor belt",      150 },
    { WEAR_WRIST_L,  "licharmor bracer",    150 },
    { WEAR_WRIST_R,  "licharmor bracer",    150 },
    { WEAR_FACE,     "licharmor mask",      150 },
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
    gear_tanarri,        /* BOT_CLASS_TANARRI       */
    gear_angel,          /* BOT_CLASS_ANGEL         */
    gear_undead_knight,  /* BOT_CLASS_UNDEAD_KNIGHT */
    gear_shapeshifter,   /* BOT_CLASS_SHAPESHIFTER  */
    gear_droid,          /* BOT_CLASS_DROID         */
    gear_samurai,        /* BOT_CLASS_SAMURAI       */
    gear_lich            /* BOT_CLASS_LICH          */
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

    /* Self-heal stale roster class_pref.  If the bot's in-game class is an
     * upgraded variant but the roster still has the base class_pref (e.g.
     * upgraded outside the normal bot_ai upgrade path), correct it so gear
     * logic uses the right own-class vnum range. */
    {
        int upgraded_pref = bot_pref_for_upgraded_class( ch->class );
        if ( upgraded_pref >= 0 && bot->roster->class_pref != upgraded_pref )
        {
            bot->roster->class_pref = upgraded_pref;
            save_bot_roster();
        }
    }

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

    /* Shapeshifters in animal form are blocked by act_obj.c from wearing
     * anything ("You cannot wear anything in this form").  Shift back to
     * human only when there's actually actionable gear work this tick —
     * otherwise we spam 'shift human' (and hit the SHAPE_COUNTER>35 fatigue
     * cap) every tick after combat. "Actionable" means a class piece sitting
     * in inventory ready to wear, OR a slot we can afford to (re)craft this
     * tick.  If we'd just be standing in human form waiting for primal,
     * stay in form. */
    if ( IS_CLASS(ch, CLASS_SHAPESHIFTER)
      && IS_AFFECTED(ch, AFF_POLYMORPH)
      && ch->pcdata->powers[SHAPE_FORM] != 0 )
    {
        bool gear_actionable = FALSE;
        const BOT_GEAR_PIECE *ge;
        OBJ_DATA *carry;
        int cp = bot->roster->class_pref;

        /* Class piece in inventory waiting to be worn. */
        if ( !bot->decap_recovery )
        {
            for ( carry = ch->carrying; carry != NULL; carry = carry->next_content )
            {
                if ( carry->wear_loc != WEAR_NONE ) continue;
                if ( bot_is_own_class_gear_vnum( cp, carry->pIndexData->vnum ) )
                {
                    gear_actionable = TRUE;
                    break;
                }
            }
        }

        /* Slot empty or holding newbiepack/prior-class gear AND we can
         * afford the upgrade right now. */
        if ( !gear_actionable )
        {
            for ( ge = bot_class_gear[cp]; ge->wear_slot != WEAR_NONE; ge++ )
            {
                OBJ_DATA *worn = get_eq_char( ch, ge->wear_slot );
                if ( worn != NULL
                  && bot_is_own_class_gear_vnum( cp, worn->pIndexData->vnum ) )
                    continue;   /* already correct */
                if ( ch->practice >= ge->primal_cost )
                {
                    gear_actionable = TRUE;
                    break;
                }
            }
        }

        if ( gear_actionable )
        {
            bot_cmd( ch, "shift human" );
            return;
        }
        /* Nothing useful to do in human form — stay in animal form. */
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
            /* Skip prior-class gear (e.g. moonarmour after werewolf->shapeshifter
             * upgrade).  Letting step 1.5 wear it puts the bot in a loop:
             * step 4 sees a class-vnum in the slot and skips, so the proper
             * shapearmor never gets crafted.  Step 2b will extract instead. */
            if ( !bot_is_own_class_gear_vnum( class_pref, obj->pIndexData->vnum ) )
                continue;

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

    /* Step 3: unclassed bot, mortal (level < 3), or decap recovery — fill
     * newbiepack slots only.  A bot can have class set but still be at level 2
     * if it was spawned/migrated with class pre-assigned without training
     * avatar; class gear (e.g. werewolf klaive) must not be created until
     * the bot has reached avatar (level 3).  During decap_recovery the bot is
     * re-training; class gear must not be created or worn until call all has
     * been issued and the flag cleared. */
    if ( ch->class == 0 || ch->level < 3 || bot->decap_recovery )
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

    /* Werewolf moonarmour pieces require DISC_WERE_LUNA >= 2, but the
     * klaive wield has no discipline requirement.  Per-entry skip below
     * lets the bot craft the klaive immediately while deferring moonarmour
     * until Luna 2 is trained. */

    /* Step 4: classed bot — class-gear pass (one slot upgraded per tick).
     *
     * obj_to_char() prepends to ch->carrying, so a class piece just created
     * via bot_cmd is at the head of the list and gets tried first by
     * "wear all".  The displaced newbiepack stays in inventory and is
     * extracted by step 2 on the following tick. */
    for ( entry = table; entry->wear_slot != WEAR_NONE; entry++ )
    {
        bool skip_craft = FALSE;
        /* Werewolf moonarmour requires DISC_WERE_LUNA >= 2.  The klaive
         * wield has no discipline requirement, so it's not skipped here. */
        if ( class_pref == BOT_CLASS_WEREWOLF
          && ch->power[DISC_WERE_LUNA] < 2
          && strncmp( entry->cmd, "moonarmour", 10 ) == 0 )
            skip_craft = TRUE;

        current = get_eq_char( ch, entry->wear_slot );

        /* Already has matching own-class gear here — nothing to do.  After a
         * class upgrade the bot may still be wearing prior-class gear (e.g.
         * moonarmour after werewolf->shapeshifter); fall through to the
         * upgrade path so it gets replaced with the new class's piece. */
        if ( current != NULL
          && bot_is_own_class_gear_vnum( class_pref, current->pIndexData->vnum ) )
            continue;

        /* Slot has newbiepack or prior-class gear — try to upgrade if we can afford it */
        if ( current != NULL
          && ( bot_is_newbiepack_vnum( current->pIndexData->vnum )
            || bot_is_class_gear_vnum( current->pIndexData->vnum ) ) )
        {
            if ( !skip_craft && ch->practice >= entry->primal_cost )
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
                        "[GEAR] %s: skip '%s' -- need %d primal (or discipline), have %d\n\r",
                        ch->name, entry->cmd, entry->primal_cost, ch->practice );
                    write_to_buffer( ch->desc->snoop_by, echo, 0 );
                    bot->last_gear_warn = now;
                }
            }
            continue;   /* can't afford yet; leave newbiepack, try next slot */
        }

        /* Slot is empty */
        if ( !skip_craft && ch->practice >= entry->primal_cost )
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
                    "[GEAR] %s: slot empty, skip '%s' -- need %d primal (or discipline), have %d\n\r",
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
     * from the class gear table.  These slots stay filled with newbiepack
     * permanently (e.g. drow WEAR_NECK_2 when the class table lacks it). */
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
