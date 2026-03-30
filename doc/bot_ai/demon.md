# Demon Class — Bot AI Reference

## Overview

Demons use the same **research + train** progression as vampires, with 9 disciplines
(`ch->power[DISC_DAEM_*]`). Unlike vampires there is no age-rank cap — disciplines
can be raised freely as long as research is complete. The primary damage multiplier
is `DISC_DAEM_ATTA` (extra attacks + flat max_dam per level). `DISC_DAEM_IMMU`
provides damage reduction. A separate **demon points** pool
(`pcdata->stats[DEMON_CURRENT]` / `DEMON_TOTAL`) funds body-part powers via
`inpart`. Class is selected at avatar (level 3) via `selfclass demon`.

---

## Discipline System

Same mechanism as vampires:

1. **Start research**: `research <discipline_name>`
   - Sets `pcdata->disc_research = disc_index`, `disc_points = 0`
2. **Accumulate points**: 1 point per kill (2–3 for high-level mobs)
   - Points needed: `(current_level + 1) * 10`
3. **Points done**: `disc_points` set to 999
4. **Train**: `train <discipline_name>` — increments `power[disc_index]`, resets fields
5. **Cancel**: `research cancel`

---

## All Demon Disciplines

| Constant         | Idx | Name         | Key Effects |
|------------------|-----|--------------|-------------|
| DISC_DAEM_HELL   | 30  | hellfire     | Fire proc in combat (>3); immolate (2); dinferno (3); daemonseed (7); hellfire walls (8) |
| DISC_DAEM_ATTA   | 31  | attack       | +50 max\_dam/level; +level/2 extra attacks; unlocks body parts; disarm resistance |
| DISC_DAEM_TEMP   | 32  | temptation   | (minor / unknown effects) |
| DISC_DAEM_MORP   | 33  | morphosis    | Polymorph power level (`get_polymorph_power`) |
| DISC_DAEM_CORR   | 34  | corruption   | `caust` at level 4 (poisons a weapon) |
| DISC_DAEM_GELU   | 35  | geluge       | `frostbreath` (2); icy skin (5); `entomb` ice walls (6); `gust` (7) |
| DISC_DAEM_DISC   | 36  | discord      | `unnerve` (1); `evileye` (2); `chaosportal` teleport (4) |
| DISC_DAEM_NETH   | 37  | nether       | `deathsense` truesight (2); `leech` HP drain (4) |
| DISC_DAEM_IMMU   | 38  | immunae      | Passive damage reduction: `100 - (power * 4)`% of incoming damage |

---

## Attack Discipline Milestones (DISC_DAEM_ATTA)

Each level of `attack` is gained via research/train and has side-effects at
specific levels:

| Level | Effect |
|-------|--------|
| 1     | `claws` command unlocked; VAM\_CLAWS auto-set on train |
| 2     | `fangs` command unlocked |
| 3     | `tail` grows (VAM\_TAIL auto-set); `rage` command unlocked |
| 4     | `horns` command unlocked; `calm` command unlocked |
| 5     | `wings` command unlocked (VAM\_WINGS auto-set) |
| 7     | `blink` command unlocked |
| 10    | Max — +500 max\_dam, +5 extra attacks |

---

## Toggle / Passive Buff Commands

These should be kept active at all times.

| Command      | Requirement                            | State Check                              | Notes |
|--------------|----------------------------------------|------------------------------------------|-------|
| `claws`      | DISC\_DAEM\_ATTA ≥ 1                  | `IS_VAMPAFF(ch, VAM_CLAWS)`             | Primary unarmed weapon; drops wielded items |
| `fangs`      | DISC\_DAEM\_ATTA ≥ 2                  | `IS_VAMPAFF(ch, VAM_FANGS)`             | Bite attack in combat |
| `horns`      | DISC\_DAEM\_ATTA ≥ 4 or DEM\_HORNS   | `IS_DEMAFF(ch, DEM_HORNS)`              | Extra gore attack |
| `wings`      | DISC\_DAEM\_ATTA ≥ 5 or DEM\_WINGS   | `IS_DEMAFF(ch, DEM_WINGS)`              | Wing sweep attack |
| `tail`       | DEM\_TAIL inpart                       | `IS_DEMAFF(ch, DEM_TAIL)`               | Tail whip attack |
| `deathsense` | DISC\_DAEM\_NETH ≥ 2                  | `IS_SET(ch->act, PLR_HOLYLIGHT)`        | Unholy truesight — see invisible/hidden |

> **Note**: `claws` removes all wielded weapons on activation. Do not attempt to
> enchant weapons (immolate/caust/freezeweapon) once claws are active — the bot
> has no wielded weapon to enchant.

---

## Combat Commands (require target)

| Command              | Requirement             | Notes |
|----------------------|-------------------------|-------|
| `frostbreath <t>`    | DISC\_DAEM\_GELU ≥ 2   | Frost breath; respects `TIMER_CAN_BREATHE_FROST` |
| `frostbreath all`    | DISC\_DAEM\_GELU ≥ 2   | AOE version; same timer |
| `leech <t>`          | DISC\_DAEM\_NETH ≥ 4   | Drains up to 300 HP from target; heals self |
| `cone <t>`           | CLASS\_DEMON + 100 mana | Cone of fire; 10-beat lag |
| `unnerve <t>`        | DISC\_DAEM\_DISC ≥ 1   | Strips target's combat stance |
| `blink <t>`          | DISC\_DAEM\_ATTA ≥ 7   | Pop-in/pop-out assault; 40-beat lag |

---

## Passive Combat Effects

| Discipline           | Effect |
|----------------------|--------|
| DISC\_DAEM\_ATTA     | `max_dam += power * 50`; extra attacks `+= power / 2`; disarm resistance |
| DISC\_DAEM\_IMMU     | `damage *= (100 - power*4) / 100` (incoming damage reduction) |
| DISC\_DAEM\_HELL > 3 | Random fire burst proc each round in fight.c |
| `pcdata->souls`      | `max_dam += MIN(350, souls * 70)` (PvP souls, minor for bot) |

---

## Demon Points (`DEMON_CURRENT` / `DEMON_TOTAL`)

Stored in `pcdata->stats[DEMON_CURRENT]` and `pcdata->stats[DEMON_TOTAL]`.
Earned by receiving power from a patron demon (`pray` command transfers points),
or from DTOKEN items (`act_obj.c`).

### Self-Inpart Priority

Use `inpart <self> <power>` — self-cost is 1× the base (non-self = 25×).
Spend points in this order for a grinding bot:

| Command                    | Cost  | Flag          | Effect |
|----------------------------|-------|---------------|--------|
| `inpart <self> might`      | 7500  | DEM\_MIGHT    | Strength bonus |
| `inpart <self> toughness`  | 7500  | DEM\_TOUGH    | Defence bonus |
| `inpart <self> speed`      | 7500  | DEM\_SPEED    | Speed bonus |
| `inpart <self> nightsight` | 3000  | DEM\_EYES     | Permanent see-in-dark |
| `inpart <self> tail`       | 5000  | DEM\_TAIL     | Unlocks `tail` toggle |
| `inpart <self> graft`      | 20000 | DEM\_GRAFT    | Extra arms (+2 attacks via extra hands) |

### Warp System — Bot Should Avoid

`obtain` costs 15,000 demon points but awards a **random** warp, including
bad mutations (INFIRMITY, GBODY, SCARED, MAGMA, WEAK, SLOW, VULNER, CLUMSY,
STUPID). The bot **must not** call `obtain` — the risk of a harmful warp
outweighs any benefit.

---

## Bot AI Training Priority

```
1.  Attack  → 1   (unlock claws — primary melee)
2.  Attack  → 2   (unlock fangs)
3.  Attack  → 5   (horns + wings, max body parts before diminishing returns)
4.  Immunae → 5   (20% damage reduction — survivability while grinding)
5.  Nether  → 2   (deathsense truesight)
6.  Nether  → 4   (leech in-combat HP drain)
7.  Discord → 1   (unnerve — strips stance)
8.  Geluge  → 2   (frostbreath)
9.  Hellfire → 3  (fire combat proc active)
10. Attack  → 7   (blink unlocked)
11. Immunae → 10  (full 40% damage reduction)
12. Geluge  → 6   (entomb)
13. Corruption → 4 (caust — not used while claws active, but may matter later)
14. Attack  → 10  (full +500 max_dam, +5 extra attacks)
15. Hellfire → 8  (hellfire walls)
16. Max remaining disciplines
```

---

## Combat Action Priority (each combat tick)

```
0. mana < 100                → skip cone this tick
1. Neth ≥ 4, roll 1–30      → leech <target>        (HP drain + self-heal)
2. Gelu ≥ 2, roll 31–55     → frostbreath <target>  (frost damage, check timer)
3. mana ≥ 100, roll 56–75   → cone <target>         (fire burst, costs 100 mana)
4. Disc ≥ 1, roll 76–88     → unnerve <target>      (strip stance)
5. Atta ≥ 7, roll 89–95     → blink <target>        (pop-out strike)
```

---

## Pre-Combat Buff Order (buff\_check, between fights)

```
1. deathsense  — if Neth ≥ 2 and not PLR_HOLYLIGHT
2. claws       — if Atta ≥ 1 and not VAM_CLAWS
3. fangs       — if Atta ≥ 2 and not VAM_FANGS
4. horns       — if (Atta ≥ 4 or DEM_HORNS) and not IS_DEMAFF(DEM_HORNS)
5. wings       — if (Atta ≥ 5 or DEM_WINGS) and not IS_DEMAFF(DEM_WINGS)
6. tail        — if DEM_TAIL inpart and not IS_DEMAFF(DEM_TAIL)
```

---

## Source Files

| File             | Contents |
|------------------|----------|
| `src/demon.c`    | `do_obtain`, `do_inpart`, `do_horns`, `do_tail`, `do_wings`, `do_cone`, `do_dstake` |
| `src/daemon.c`   | `do_leech`, `do_entomb`, `do_gust`, `do_caust`, `do_unnerve`, `do_wfreeze`, `do_immolate`, `do_dinferno`, `do_hellfire`, `do_seed`, `do_frostbreath`, `do_blink`, `do_graft` |
| `src/clan.c`     | `do_claws`, `do_fangs`, `do_rage`, `do_calm` |
| `src/daemon.c`   | `do_deathsense` |
| `src/act_move.c` | `do_research`, `do_train`, discipline name table (index 30–38) |
| `src/fight.c`    | DISC\_DAEM\_ATTA extra attacks, max\_dam bonus, IMMU damage reduction |
| `src/merc.h`     | `DISC_DAEM_*` constants (indices 30–38), `DEMON_CURRENT/TOTAL/POWER/PPOWER` |
| `src/demon.h`    | `DEM_*` bitmask constants, `DPOWER_*` array indices |
| `src/bot_ai_demon.c` | Stub vtable (all NULL — to be implemented) |
