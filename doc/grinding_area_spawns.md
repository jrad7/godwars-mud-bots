# Grinding Area System

## Overview

Two complementary systems govern how bots grind in this MUD:

1. **Dynamic Mob Spawns** — the server auto-fills grinding areas with mobs on reset, controlled by `area_levels.h` and `db.c`.
2. **Bot Grind Navigation** — the bot AI selects a zone tier based on the bot's `max_hit`, navigates there, and fights until the timer expires. Defined in `src/bot.h`.

---

## Part 1 — Dynamic Mob Spawns

When an area resets, `reset_room()` in `db.c` checks whether `pRoom->area->mob_level > 0`. If so, the area is treated as a Grinding Area and spawns are generated dynamically rather than from the static `#RESETS` block.

**Reset logic per room:**
1. Pick 5 random mobile VNUMs from within the area's `lvnum`–`uvnum` range.
2. Create each mob and set `ACT_STAY_AREA` so it doesn't wander out of the zone.
3. Place it in the room.

**Shopkeeper preservation:** Mobs with `pMobIndex->pShop != NULL` are never overridden — merchants and their inventory still spawn normally even inside grinding areas.

**Exception:** `weed.are` already has 5 random mobile resets per room hardcoded in its `#RESETS` block, so the engine skips the override for that area and lets the static resets fire.

### Zone Registration Tables (`src/area_levels.h`)

Two tables in `area_levels.h` control dynamic spawns and XP for each grind zone:

**`area_level_table`** — enables dynamic spawning and sets mob level for that area:

| Area File | Mob Level |
|-----------|-----------|
| `school.are` | 10 |
| `sewer.are` | 20 |
| `smurf.are` | 30 |
| `moria.are` | 40 |
| `canyon.are` | 50 |
| `plains.are` | 60 |
| `weed.are` | 75 |
| `thalos.are` | 100 |
| `shire.are` | 125 |
| `heaven.are` | 200 |
| `hell.are` | 300 |

**`grind_zone_table`** — sets the XP/CP multiplier for kills in each zone (100 = 1×):

| Zone | VNUM Range | XP Mult |
|------|-----------|---------|
| School | 3700–3760 | 100% |
| Sewer | 7000–7445 | 110% |
| Smurf | 100–129 | 125% |
| Moria | 3900–4172 | 135% |
| Canyon | 9201–9260 | 150% |
| Plains | 300–350 | 175% |
| Weed | 30232–30261 | 200% |
| Thalos | 5200–5280 | 250% |
| Shire | 1100–1157 | 300% |
| Hell | 30100–30200 | 400% |
| Heaven | 99000–99100 | 500% |

### Fast Resets (`db.c` — `area_update()`)

Bot grind zones reset every ~1 minute (rather than the default ~15) so mobs repopulate before bots deplete them. Each zone is identified by a characteristic room VNUM:

| Zone | Room VNUM used |
|------|---------------|
| Mud School | `ROOM_VNUM_SCHOOL` |
| Smurf | 104 |
| Canyon | 9201 |
| Hell | 30100 |
| Heaven | 99000 |
| Shire | 1100 |
| Sewer | 7030 |
| Moria | 3900 |
| Plains | 300 |
| Thalos | 5200 |

### Loot Tiers (`db.c` — `give_grind_loot()`)

When a bot kills a dynamically-spawned mob, `give_grind_loot()` has a 15% chance to drop a random item from the zone's object VNUM range. The tier is selected by mob level:

| Mob Level ≤ | Loot VNUM Range | Zone |
|-------------|-----------------|------|
| 5 | 3700–3760 | School |
| 20 | 7000–7445 | Sewer |
| 30 | 100–129 | Smurf |
| 40 | 3900–4172 | Moria |
| 50 | 9201–9260 | Canyon |
| 60 | 300–350 | Plains |
| 75 | 30232–30261 | Weed |
| 100 | 5200–5280 | Thalos |
| 125 | 1100–1157 | Shire |
| 150 | 99000–99100 | Heaven |
| 200 | 30100–30200 | Hell |

### Registering a New Grinding Area

1. Build the area (OLC or manually). It needs at least a few `#MOBILES` entries.
2. Add it to **`area_level_table`** in `src/area_levels.h` with the target mob level.
3. Add it to **`grind_zone_table`** in `src/area_levels.h` with VNUM range and XP multiplier.
4. Add a fast-reset block in **`area_update()`** in `src/db.c` using a characteristic room VNUM.
5. Add a loot tier row in **`give_grind_loot()`** in `src/db.c` (keep ordered by mob level).

No changes to the `.are` file are required.

---

## Part 2 — Bot Grind Navigation

### How Tier Selection Works

Each bot picks a grind zone based on its current `max_hit`. The `grind_tiers[]` table in `src/bot.h` maps `max_hit` thresholds to one or more zone route arrays. The bot selects the highest tier whose `max_hit` is still greater than its own (i.e. it uses the first tier where it fits), then picks a random route from that tier's list.

```c
for (i = 0; i < GRIND_TIER_COUNT; i++) {
    if (ch->max_hit < grind_tiers[i].max_hit) {
        /* use this tier */
        break;
    }
}
```

### Route Format

Each route is a `NULL`-terminated `const char *` array of MUD commands, starting with `"recall"` and ending at the zone entrance room. The bot queues them via `bot_cmd()`.

```c
static const char *zone_sewer[] = {
    "recall", "south", "south", "south", "south", "down", NULL
};
```

Routes ignore locked doors — if a gate blocks the path in the `.are` file but has no lock flags set (`exit_flags == 0`), movement succeeds. Doors that are actually locked (non-zero `exit_flags` with a key requirement) will block the bot; avoid routing through them.

### Current Grind Tiers

| Tier (max_hit < N) | Zone | Area File | Entry Room | Route from Recall |
|--------------------|------|-----------|------------|-------------------|
| 5,000 | Mud School | `school.are` | 3700 | `up, open door, south` |
| 8,000 | Sewer | `sewer.are` | 7030 | `S, S, S, S, down` |
| 10,000 | Smurf Village | `smurf.are` | 101 | `S, S, W, W, W, N` |
| 15,000 | Moria | `moria.are` | 3900 | `S, S, W, W, W, W, N` |
| 20,000 | Canyon | `canyon.are` | 9201 | `S, S, E×6, S×4, E×2, S, E×2, D, S` |
| 30,000 | Plains | `plains.are` | 300 | `S, S, E×4, N×3, W×2, N` |
| 40,000 | Weed | `weed.are` | — | `S, S, E×6, N×3, E×2, U×5, E×2, D, E, N, E, N` |
| 50,000 | Thalos | `thalos.are` | 5200 | `S, S, E×6, S×4, W×3` |
| 60,000 | Shire | `shire.are` | 1100 | `S, S, W×5, N` |
| 80,000 | Jobo's Hell | `hell.are` | 30100 | `S, S, W×7, D, D, D` |
| 999,999 | Jobo's Heaven | `heaven.are` | 99000 | `down` |

Tiers are approximate — adjust thresholds in `grind_tiers[]` in `src/bot.h` to tune which zones bots graduate into.

### Adding a New Grind Zone to the Bot System

1. **Find the route** using `scripts/find_route.py` (see Part 3 below).
2. **Add the route array** in `src/bot.h`:
   ```c
   /* recall(3001)->path comment */
   static const char *zone_myzone[] = { "recall", "south", ..., NULL };
   ```
3. **Add it to `grind_tiers[]`** in `src/bot.h` at the appropriate `max_hit` threshold:
   ```c
   { 25000, { zone_myzone }, 1 },
   ```
4. **Add a boundary rule** in `bot_area_rules[]` in `src/bot_ai.c` to block the exit direction that leads back out of the zone:
   ```c
   { entry_room_vnum, entry_room_vnum, DIRMASK(DIR_SOUTH) },
   ```
5. **Add an entry to `route_names[]`** in `src/bot_ai.c` so the watchbot logs show the zone name.
6. **Register the zone** in `src/area_levels.h` — both `area_level_table` (mob level) and `grind_zone_table` (VNUM range + XP mult).
7. **Add a fast-reset block** in `area_update()` in `src/db.c`.
8. **Add a loot tier row** in `give_grind_loot()` in `src/db.c` (keep ordered by mob level).

See Part 1 for the full tables of current values to use as reference.

---

## Part 3 — Route Pathfinding Script

`scripts/find_route.py` parses all `.are` files, builds a room graph, and runs BFS from room 3001 to find the shortest walkable path to any target room. Doors and locks are ignored (as the bot system currently does).

### Usage

```bash
python scripts/find_route.py
```

Output includes both the step list and a ready-to-paste C array for `bot.h`:

```
SEWER (room 7030):
  Steps: ['south', 'south', 'south', 'south', 'down']
  C array: { "recall", "south", "south", "south", "south", "down", NULL }
```

### Adding a New Target Zone

Edit the `zone_rooms` dict near the bottom of the script to add the zone name and its entry room VNUM:

```python
zone_rooms = {
    'MORIA':    3900,
    'SEWER':    7030,
    'MY_ZONE':  12345,   # <- add here
    ...
}
```

Re-run the script. If the zone is not reachable from 3001, a `WARNING` line is printed — check that the entry room has a path back to Midgaard in the `.are` files.

### How the Parser Works

The `.are` exit format is:
```
D[0-5]
description~
keyword~
exit_flags  key_vnum  to_room
```

The parser reads field index 2 (`to_room`) as the destination. Fields 0 and 1 are the door flags and key VNUM respectively — the BFS ignores them entirely.

---

## Utility Scripts Summary

| Script | Purpose |
|--------|---------|
| `scripts/find_route.py` | BFS pathfinder — find bot nav routes from recall to any room |
| `scripts/randomize_resets.py` | Directly rewrites `#RESETS` in `.are` files with random mob spawns (offline use only) |
