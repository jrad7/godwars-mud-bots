# Dynamic Grinding Area Mob Spawns

## Overview

The Dystopia MUD engine has been modified to automatically manage mob spawns in designated "Grinding Areas". Instead of relying on static definitions inside the `.are` files (which must be painstakingly maintained or generated via scripts), the MUD engine will dynamically intercept room resets and intelligently spawn random mobs.

## How it Works

When an area resets, the engine parses its list of components (`#RESETS`). The `reset_room()` function in `db.c` executes the following logic for Grinding Areas:

1. **Detection:** The engine checks if `pRoom->area->mob_level > 0`. This flags the area as a Grinding Area (configured in `src/area_levels.h`).
2. **Dynamic Generation:** For every room in the grinding area, the engine picks 5 random valid `vnums` within the area's `lvnum` and `uvnum` boundaries, creates the mobile, applies the `ACT_STAY_AREA` flag to prevent them from wandering into other zones, and places them into the room.
3. **Smart Ignoring:** During the parsing of `#RESETS`, generic Mob (`M`), and subsequent Equipment (`E`) / Give (`G`) resets are functionally ignored and skipped. 
4. **Shopkeeper Preservation:** The logic ensures that shopkeepers (`pMobIndex->pShop != NULL`) bypass this override, allowing essential merchants and their inventory to spawn uninterrupted even inside grinding areas.

## Exceptions

The `weed.are` area natively contained 5 random mobile resets within the `#RESETS` definition in its file. It acts functionally identical, therefore the engine bypasses `weed.are` explicitly to allow its predefined resets to fire naturally.

## Adding New Grinding Zones

Because this system is entirely decoupled from the actual `.are` files, making a new Grinding Area extremely dense with dynamic mobs requires **zero** file scripting.
1. Build your new area via OLC (e.g. `coolzone.are`). Ensure it contains a few mobile templates (#MOBILES).
2. Register `coolzone.are` inside the `area_level_table` found in `src/area_levels.h` along with its target player level.
3. Upon booting the game, the engine will automatically ignore its static generic spawns and fill every room with 5 random mobs scaling dynamically!

## Utility Scripts

A Python script has also been packaged alongside the codebase at `scripts/randomize_resets.py`.
While the MUD automatically overrides resets via `reset_room()`, this utility script can be leveraged if you need to permanently alter `#RESETS` directly in the `.are` files to clear out bloat.

### Usage
From AWS WSL, or any Python 3 compatible terminal:
```bash
python3 scripts/randomize_resets.py area/smurf.are area/school.are
```

### Script Behavior
- Uses advanced matching to locate `#MOBILES`, `#ROOMDATA`/`#ROOMS`.
- Compiles lists of all instantiated room IDs and mobile VNUMs.
- Safely targets the `#RESETS` section, dropping any old `M`, `E`, and `G` lines.
- Appends precisely 5 `M 0 <vnum> 200 <roomvnum>` commands to every room in the targeted area.
