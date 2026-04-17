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
3. Remove `AFF_SANCTUARY`, `ACT_AGGRESSIVE`, and `spec_fun` (no spell-casting).
4. Place it in the room.

**Shopkeeper preservation:** Mobs with `pMobIndex->pShop != NULL` are never overridden — merchants and their inventory still spawn normally even inside grinding areas.

**Exception:** `weed.are` already has 5 random mobile resets per room hardcoded in its `#RESETS` block, so the engine skips the override for that area and lets the static resets fire.

### Zone Registration Tables (`src/area_levels.h`)

Two tables in `area_levels.h` control dynamic spawns and XP for each grind zone:

**`area_level_table`** — enables dynamic spawning and sets mob level for that area. Each entry maps an area filename to a mob level.

**`grind_zone_table`** — sets the XP/CP multiplier for kills in each zone. Each entry has `vnum_lo`, `vnum_hi`, `mult` (percentage: 100 = 1x, 200 = 2x), and `name`.

Both tables are NULL-terminated arrays. Check these files for current values.

### Fast Resets (`db.c` — `area_update()`)

Bot grind zones reset every ~1 minute (rather than the default ~15) so mobs repopulate before bots deplete them. Each zone is identified by a characteristic room VNUM in `area_update()` — when that room's area resets, the age is set to `15 - 1` to trigger the next reset quickly.

### Loot Tiers (`db.c` — `give_grind_loot()`)

When a dynamically-spawned mob is created, `give_grind_loot()` has a 15% chance (`GRIND_DROP_CHANCE`) to give it a random item from the zone's object VNUM range. The tier is selected by mob level from the `loot_tiers[]` array, which must be ordered by ascending level.

---

## Part 2 — Bot Grind Navigation

### How Tier Selection Works

Each bot picks a grind zone based on its current `max_hit`. The `grind_tiers[]` table in `src/bot.h` maps `max_hit` thresholds to one or more zone route arrays. The bot selects the highest tier whose `max_hit` is still greater than its own, then picks a random route from that tier's list.

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

### Boundary Rules (`src/bot_ai.c`)

The `bot_area_rules[]` array blocks specific exit directions at zone entry rooms so bots don't wander back out. Each entry has `vnum_lo`, `vnum_hi`, and a `forbidden_dirs` bitmask using `DIRMASK(DIR_*)`.

---

## Part 3 — Adding a New Grind Zone (Full Checklist)

### Prerequisites for a Grind Zone

Before making an area a grind zone, ensure:
- **No flying required:** All rooms must NOT be `SECT_AIR` (sector_type 9). Change to an appropriate type (0=inside, 1=city, 2=field, 4=hills).
- **No locked doors:** All door exit_flags must be 0 so bots can traverse freely.
- **No spell-casting mobs:** Remove `spec_cast_*` and `spec_breath_*` entries from the area's `#SPECIALS` section. The spawn code also NULLs `spec_fun` on dynamically spawned mobs as a safety net.
- **No sanctuary on mobs:** The spawn code strips `AFF_SANCTUARY` automatically, but clean prototypes are preferred.
- **At least a few `#MOBILES` entries** in the area file for random spawning.

### Step-by-Step Registration

1. **Build or prepare the area** (OLC or manually). Verify the prerequisites above.

2. **Find the route** using `scripts/find_route.py`:
   - Add the zone name and entry room VNUM to the `zone_rooms` dict in the script
   - Run: `wsl python3 scripts/find_route.py`
   - If the route is too long (>20 steps), consider adding a shortcut exit from a room near recall (e.g. room 3004 or 3005 in midgaard.are)

3. **Add the route array** in `src/bot.h`:
   ```c
   /* recall(3001)->path comment */
   static const char *zone_myzone[] = { "recall", "south", ..., NULL };
   ```

4. **Add it to `grind_tiers[]`** in `src/bot.h` at the appropriate `max_hit` threshold:
   ```c
   { 25000, { zone_myzone, zone_other }, 2 },
   ```

5. **Add to `area_level_table`** in `src/area_levels.h` with the target mob level:
   ```c
   { "myzone.are", 50 },
   ```

6. **Add to `grind_zone_table`** in `src/area_levels.h` with VNUM range and XP multiplier:
   ```c
   { 12345, 12400, 200, "MyZone" },
   ```

7. **Add a fast-reset block** in `area_update()` in `src/db.c` using a characteristic room VNUM:
   ```c
   pRoomIndex = get_room_index( 12345 );  /* MyZone entrance */
   if ( pRoomIndex != NULL && pArea == pRoomIndex->area )
       pArea->age = 15 - 1;
   ```

8. **Add a loot tier row** in `give_grind_loot()` in `src/db.c` (keep ordered by ascending mob level):
   ```c
   { 50, 12345, 12400 },   /* myzone */
   ```

9. **Add a boundary rule** in `bot_area_rules[]` in `src/bot_ai.c` to block the exit direction leading back out of the zone:
   ```c
   { 12345, 12345, DIRMASK(DIR_SOUTH) },
   ```

10. **Add an entry to `route_names[]`** in `src/bot_ai.c` so the watchbot logs show the zone name:
    ```c
    { zone_myzone, "myzone", "myzone.are" },
    ```

No changes to the `.are` file's `#RESETS` section are required — dynamic spawning overrides it.

### Adding a New Tier

To add a new tier above the current highest:

1. Change the current catch-all tier's `max_hit` from `999999` to the new threshold
2. Add a new entry at the end of `grind_tiers[]` with `999999` as the catch-all
3. Assign zones to the new tier
4. Update `GRIND_TIER_COUNT` if it's hardcoded (currently it's computed via `sizeof`)

---

## Part 4 — Route Pathfinding Script

`scripts/find_route.py` parses all `.are` files, builds a room graph, and runs BFS from room 3001 to find the shortest walkable path to any target room. Doors and locks are ignored.

### Usage

```bash
wsl python3 scripts/find_route.py
```

Output includes both the step list and a ready-to-paste C array for `bot.h`.

### Adding a New Target Zone

Edit the `zone_rooms` dict near the bottom of the script:

```python
zone_rooms = {
    'MORIA':    3900,
    'MY_ZONE':  12345,   # <- add here
    ...
}
```

Re-run the script. If the zone is not reachable from 3001, a `WARNING` line is printed — check that the entry room has a path back to Midgaard in the `.are` files, or add a new connection from a Midgaard room.

---

## Utility Scripts Summary

| Script | Purpose |
|--------|---------|
| `scripts/find_route.py` | BFS pathfinder — find bot nav routes from recall to any room |
| `scripts/randomize_resets.py` | Directly rewrites `#RESETS` in `.are` files with random mob spawns (offline use only) |
