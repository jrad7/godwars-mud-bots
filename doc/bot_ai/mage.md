# Battlemage Bot AI

**File:** `src/bot_ai_mage.c`  
**Class constant:** `CLASS_MAGE`  
**Bot class constant:** `BOT_CLASS_MAGE = 6`

---

## Class Overview

Battlemage is a caster/melee hybrid.  Its defining feature is a five-element
color magic system: each color (purple, red, blue, green, yellow) has a level
0-240 that scales every aspect of the class.  Before the class can be selected
the bot must grind all five colors to 100 and accumulate 5000 max mana, making
the pre-class phase the most distinctive part of the bot's lifecycle.

Post-class, the bot unlocks Invoke powers 1-10 (an escalating primal ladder),
activates defensive buffs, and fights primarily with `chant damage` — five
sequential elemental hits each dealing `color*4..color*5` damage.

---

## Pre-Class Requirements

Selected via `selfclass mage` at level 3.  Fails until:

| Condition | Value |
|-----------|-------|
| `spl[PURPLE_MAGIC]` | >= 100 |
| `spl[RED_MAGIC]`    | >= 100 |
| `spl[BLUE_MAGIC]`   | >= 100 |
| `spl[GREEN_MAGIC]`  | >= 100 |
| `spl[YELLOW_MAGIC]` | >= 100 |
| `max_mana`          | >= 5000 |

The `bot_do_train` pre-class gate in `bot_ai.c` calls `do_train` for
`BOT_CLASS_MAGE` before attempting selfclass.  `do_train` returns `TRUE`
while grinding and `FALSE` when all prereqs are met, at which point
`selfclass mage` fires normally.

---

## Color Training Mechanic

`magic.c:improve_spl()` fires after every spell cast.  The dtype passed is
`skill_table[sn].target`, which equals the color index.  Color constants and
their matching TAR_ values (from `merc.h`):

| Color | Index | TAR value | Training spell | Arg |
|-------|-------|-----------|---------------|-----|
| PURPLE_MAGIC | 0 | TAR_IGNORE (0) | `faerie fog` | — |
| RED_MAGIC | 1 | TAR_CHAR_OFFENSIVE (1) | `curse` | `self` |
| BLUE_MAGIC | 2 | TAR_CHAR_DEFENSIVE (2) | `cure light` | `self` |
| GREEN_MAGIC | 3 | TAR_CHAR_SELF (3) | `detect hidden` | — |
| YELLOW_MAGIC | 4 | TAR_OBJ_INV (4) | `identify` | `ring` |

`improve_spl` fires regardless of whether the spell effect applied (e.g.
already cursed, already detecting, ring already identified), so spells can
be repeated each tick without waiting for effects to expire.

**Cap:** 100 pre-class, 240 post-class (non-Lich limit).

---

## Invoke Power System

Unlocked via `invoke learn`, costing `(current_PINVOKE + 1) * 20` primal.

| PINVOKE | Power unlocked | Active cost |
|---------|---------------|-------------|
| 1  | Teleport anywhere | — |
| 2  | Mageshield (multi-attack buff) | 25 primal |
| 3  | Scry | — |
| 4  | Discharge (AoE mageshield detonation) | — |
| 5  | Deflector (defensive passive) | 5 primal |
| 6  | Steelshield (skin to steel) | 5 primal |
| 7  | Deeper magic understanding | — |
| 8  | Illusions (dodge chance) | 5 primal |
| 9  | Beast mode (+4 extra attacks) | 10 primal |
| 10 | Complete mastery / `invoke all` | — |

Total primal to unlock all: 20+40+60+80+100+120+140+160+180+200 = **1100 primal**

---

## Gear

Command: `magearmor <item>`, 60 primal per piece.

| Slot | Command |
|------|---------|
| WEAR_FINGER_L/R | `magearmor ring` |
| WEAR_NECK_1/2   | `magearmor collar` |
| WEAR_BODY       | `magearmor robe` |
| WEAR_HEAD       | `magearmor cap` |
| WEAR_LEGS       | `magearmor leggings` |
| WEAR_FEET       | `magearmor boots` |
| WEAR_HANDS      | `magearmor gloves` |
| WEAR_ARMS       | `magearmor sleeves` |
| WEAR_ABOUT      | `magearmor cape` |
| WEAR_WAIST      | `magearmor belt` |
| WEAR_WRIST_L/R  | `magearmor bracer` |
| WEAR_FACE       | `magearmor mask` |
| WEAR_WIELD      | `magearmor staff` |
| WEAR_HOLD       | `magearmor dagger` |

VNUMs 33000–33013 (within the 33000–33199 class gear range).

---

## Hook Details

### `should_train`
Returns `TRUE` if:
- Level 3 and class still 0 (pre-class grinding)
- Any `spl[i]` < 240
- `PINVOKE` < 10

### `do_train`
**Pre-class:**
1. Practice all spells in `pre_class_practice[]` (faerie fog, curse, remove curse,
   cure light, armor, bless, detect hidden, detect invis, stone skin, identify)
2. Train mana until `max_mana >= 5000`
3. Return `TRUE` while any color < 100 (colors grind in `between_fights`)
4. Return `FALSE` when all prereqs met — selfclass fires

**Post-class:**
- Buy next invoke level when `practice >= (PINVOKE+1)*20`

### `buff_check`
Activates missing invokes, one per call.  At PINVOKE >= 9, uses `invoke all`
if any buff is missing and sufficient primal is available.  Otherwise activates
individually: mageshield → deflector → steelshield → illusions → beast.

**Mageshield cycling:** `discharge` in `combat_action` removes mageshield.
`buff_check` re-applies it on the next tick.  This is intentional — the bot
aggressively cycles discharge+mageshield for maximum DPS.

### `combat_action`
| Priority | Condition | Action |
|----------|-----------|--------|
| 0 | HP < 40% and mana >= 1500 | `chant heal` |
| 1 | ITEMA_MAGESHIELD active and PINVOKE >= 4 | `discharge` |
| 2 | 15% random chance and mana >= 1500 | `chaos <target>` |
| 3 | mana >= 1000 (default) | `chant damage` |

`chant damage` fires 5 elemental hits: each deals `color*4..color*5` + damroll,
capped at 1000 vs players.  Total at max colors (240 each): ~4800–6000 damage.

`discharge` deals `magic_power * 3.5 .. magic_power * 4.5` to all room targets,
then removes mageshield (NEW_MULTIARMS stays set).

### `between_fights`
Each call casts one spell targeting the lowest color below cap.  The bot must
have the spell practiced (learned[sn] >= 1) or the tick is skipped.

**YELLOW identify cycle** — TAR_OBJ_INV requires the ring to be in inventory,
not worn.  When YELLOW is selected the bot uses a multi-tick cycle gated by
`bot->spell_training` (declared in `bot.h`):

1. Tick 1: set `spell_training=TRUE`, `remove ring`
2. Tick 2+: `cast 'identify' ring` (repeats while YELLOW is still the lowest color)
3. Final tick: `wear ring`, clear `spell_training`

`bot_ensure_geared()` in `bot_ai.c` checks `spell_training` and skips the
entire gear pass while it is set, preventing the ring from being immediately
re-equipped or replaced between identify casts.

---

## Bot Roster (bot_mgr.c)

14 mage bots added matching the drow/werewolf count:

**Permanent (5):** Zorn, Elyx, Siveth, Kavar, Vordyn  
**Long (5):** Arcyn, Rendal, Calix, Pyrion, Thexan  
**Short (4):** Flux, Lyrex, Nexar, Oryx
