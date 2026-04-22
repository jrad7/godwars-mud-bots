# Grind Areas (Player & Bot)

These are the zones the bot AI uses to grind. Because bots cycle through them
constantly, they are also the highest-traffic PvP areas in the game. Players
who want to find bots — to fight them, to follow them, or to avoid them —
should look here first.

All routes start at recall (room 3001, Temple of Midgaard). Every step is a
single movement command typed in order. Routes do not pass through locked
doors. `2S` notation in comments means "south, south".

## Tiers

A bot picks the highest tier whose `max_hit` ceiling is still above its own
`max_hit`, then chooses one zone from that tier at random. The tier ceilings
roughly track the player level the zone is balanced for.

| Tier | Bot `max_hit` ceiling | Approx. character level | Zones |
|------|----------------------|-------------------------|-------|
| 1 | < 3,500 | newbie / 1–20 | School, Smurf, Daycare |
| 2 | < 20,000 | 20–40 | Sewer, Shire, Mega |
| 3 | < 30,000 | 40–60 | Moria, Thalos, Galaxy |
| 4 | < 50,000 | 60–90 | Canyon, Plains, Air |
| 5 | < 100,000 | 90–130 | Disney, Weed, Drow |
| 6 | catch-all (top) | 130+ / hero | Hell, Heaven |

## Routes from recall

Mob level is the dynamic spawn level set by `area_level_table`. XP mult is
the per-zone multiplier from `grind_zone_table` (100 = 1x, 200 = 2x, 700 =
7x, 1400 = 14x). Higher tiers pay disproportionately more.

| Tier | Zone | Mob lvl | XP mult | Directions from recall |
|------|------|---------|---------|------------------------|
| 1 | School      | 10  | 1x   | up; open door; south |
| 1 | Smurf       | 10  | 1x   | 2S, 3W, N |
| 1 | Daycare     | 10  | 1x   | 2S, 6E, D, S |
| 2 | Sewer       | 25  | 1.5x | 4S, D |
| 2 | Shire       | 25  | 1.5x | 2S, 5W, N |
| 2 | Mega        | 25  | 1.5x | 3S, 2E, 2S, 11E, S |
| 3 | Moria       | 45  | 2x   | 2S, 4W, N |
| 3 | Thalos      | 45  | 2x   | 2S, 6E, 4S, 3W |
| 3 | Galaxy      | 45  | 2x   | S, W, D |
| 4 | Canyon      | 75  | 3x   | 2S, 6E, 4S, 2E, S, 2E, D, S |
| 4 | Plains      | 75  | 3x   | 2S, 4E, 3N, 2W, N |
| 4 | Air         | 75  | 3x   | S, U, N |
| 5 | Disney      | 125 | 7x   | S, D |
| 5 | Weed        | 125 | 7x   | 2S, 6E, 3N, 2E, 5U, 2E, D, E, N, E, N |
| 5 | Drow        | 125 | 7x   | 2S, 6E, 4S, 2E, S, 2E, D, W, S, 2D |
| 6 | Hell        | 150 | 14x  | 2S, 7W, 3D |
| 6 | Heaven      | 150 | 14x  | down |

## What grinding looks like in these zones

- **Dynamic spawns:** rooms are filled at reset with 5 random mobs from the
  area's VNUM range. Mobs do not wander between rooms (`ACT_STAY_AREA`),
  cannot cast spells, and do not have sanctuary. Shopkeepers still spawn
  normally.
- **Fast resets:** these zones tick every ~1 minute (vs. the default ~15) so
  bots do not run out of targets.
- **Loot:** every dynamically-spawned mob has a 15% chance to drop a random
  item from the zone's object VNUM range, tier-matched to mob level.

## Finding bots in the field

Bots cycle: travel → grind for a timer → return to recall → train/shop →
travel again. To find them:

1. Pick a tier matching their level. The grind zone they pick is random
   within the tier, so check several.
2. Walk the route above. Bots tend to clump near the entrance room (where
   `bot_area_rules` blocks them from leaving) and along the first few rooms.
3. Use `who` to confirm which bots are online and roughly what level they
   are, then match to the tier table above.

## PvP context

Engaging a bot in its grind zone is the most reliable way to start PvP. The
bot will:
- Add you to its grudge list on the first hit
- Promote you to nemesis if you become its top attacker
- Hunt you on sight from any state if you remain its nemesis

If the bot wins (or you flee while it's healthy), it will blacklist you and
not initiate again — but it will still fight back if you re-engage. See
`pvp_personality.md` for the full grudge/nemesis/blacklist system.

## Source of truth

- Routes and tier thresholds: `src/bot.h` (`grind_tiers[]`, `zone_*` arrays)
- Mob levels per area: `src/area_levels.h` (`area_level_table`)
- XP multipliers: `src/area_levels.h` (`grind_zone_table`)
- Spawn behavior, fast resets, loot: `src/db.c`
- Boundary rules (which exits bots cannot take at zone entry):
  `src/bot_ai.c` (`bot_area_rules[]`)

If routes ever drift from this doc, treat the source files as authoritative
and re-derive the table.
