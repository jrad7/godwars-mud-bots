# Drow Class — Bot AI Reference

## Overview

Drows use a **grant** system (`ch->pcdata->powers[1]` bitmask of `DPOWER_*` flags)
to acquire powers.  Powers are purchased with **drow class points**
(`pcdata->stats[DROW_POWER]`), which are earned per kill, similar to demon points.
Self-grant costs the **base price**; granting to another drow costs 5× more.

Class is selected at avatar (level 3) via `selfclass drow`.  Three optional
**professions** (warrior / mage / cleric) can later be self-granted once
`ch->generation >= 3`, or granted by a senior drow.

---

## Class Points (DROW_POWER)

| Field | Meaning |
|-------|---------|
| `pcdata->stats[DROW_POWER]`  | Current available power points |
| `pcdata->stats[DROW_TOTAL]`  | Total accumulated (informational) |
| `pcdata->stats[DROW_MAGIC]`  | Magic resistance rating |

Points are earned automatically on each kill (same mechanism as demon points in
`fight.c`).  The primary spend command is `grant self <power>`.

---

## Profession System

Granted via `grant self <profession>` or by another drow.  Requires `generation >= 3`.

| Command              | Flag         | Combat Effect |
|----------------------|--------------|---------------|
| `grant self warrior` | SPC\_DROW\_WAR | +2 extra attacks (multi\_hit), +3 extra attacks (do\_multi\_hit) |
| `grant self mage`    | SPC\_DROW\_MAG | Unlocks `chaosblast` (750 mana burst) |
| `grant self cleric`  | SPC\_DROW\_CLE | Unlocks `heal` (750 mana, heals `spl[BLUE_MAGIC]*3` HP) |

**Bot should self-grant `warrior`** for the passive extra-attack bonus.  Mage and
cleric require managing mana budgets that conflict with `drowfire` / `earthshatter`.

---

## All Drow Powers (DPOWER_*)

Powers are stored as bits in `ch->pcdata->powers[1]`.

| Constant             | Grant Arg      | Self-Cost | Effect |
|----------------------|----------------|-----------|--------|
| DPOWER\_DROWFIRE      | `drowfire`     | 2,500     | `drowfire <target>` — fire spell (100 mana) |
| DPOWER\_DARKNESS      | `darkness`     | 7,500     | `darkness` toggle — globe of darkness (500 mana); breaks web/ties |
| DPOWER\_DROWSIGHT     | `drowsight`    | 5,000     | `drowsight` toggle — truesight (`PLR_HOLYLIGHT`) |
| DPOWER\_LEVITATION    | `levitation`   | 1,000     | Passive — immune to trip and sweep |
| DPOWER\_DROWSHIELD    | `drowshield`   | 5,000     | `drowshield` toggle — `IMM_SHIELDED` (aura hidden) |
| DPOWER\_DROWPOISON    | `drowpoison`   | 2,500     | Passive — poisons target on each hit (fight.c) |
| DPOWER\_SHADOWWALK    | `shadowwalk`   | 10,000    | `shadowwalk <person>` — teleport to target (250 move) |
| DPOWER\_GAROTTE       | `garotte`      | 5,000     | `garotte <target>` — whip attack; auto-kills NPCs on lucky roll |
| DPOWER\_ARMS          | `spiderarms`   | 25,000    | Passive — disarm resistance (−30 / +30 to attacker/defender chance) |
| DPOWER\_DROWHATE      | `drowhate`     | 20,000    | `drowhate` toggle — `NEW_DROWHATE`: +650 max\_dam |
| DPOWER\_SPIDERFORM    | `spiderform`   | 25,000    | `spiderform` toggle — `NEW_DFORM`: +400 hitroll/damroll, −1000 armor, +650 max\_dam, extra hands |
| DPOWER\_WEB           | `web`          | 5,000     | `web <target>` — webbed immobilize (POS\_FIGHTING) |
| DPOWER\_DGAROTTE      | `dgarotte`     | 2,500     | `dgarotte <target>` — dark-enhanced garotte (requires GAROTTE + DGAROTTE) |
| DPOWER\_CONFUSE       | `confuse`      | 2,500     | `confuse` — forces target to flee (75 move; 75% chance) |
| DPOWER\_GLAMOUR       | `glamour`      | 5,000     | Cosmetic — rename items; no combat value |
| DPOWER\_EARTHSHATTER  | `earthshatter` | 7,500     | `earthshatter` — room AoE damage (150 mana) |
| DPOWER\_SPEED         | `speed`        | 7,500     | Passive — +3/+5 extra attacks; −50 parry/dodge chance for attacker |
| DPOWER\_TOUGHSKIN     | `toughskin`    | 7,500     | Passive — incoming damage `/= 3` |
| DPOWER\_DARKTENDRILS  | `darktendrils` | 25,000    | `darktendrils` toggle — `NEW_DARKTENDRILS`: extra multi\_hit attack each round + dodge proc (1-in-5 chance) |
| DPOWER\_FIGHTDANCE    | `fightdance`   | 10,000    | `fightdance` toggle — `NEW_FIGHTDANCE`: auto-attack vs NPCs each round (50% chance; requires whip or piercing weapon in WEAR\_WIELD or WEAR\_HOLD) |

---

## Passive Effects (always-on once granted)

These require no activation command.

| Power              | Effect in fight.c |
|--------------------|-------------------|
| DPOWER\_TOUGHSKIN   | `dam /= 3` on all incoming hits |
| DPOWER\_DROWPOISON  | `spell_poison` on every hit (scales with level×10–20) |
| DPOWER\_SPEED       | +3 extra attacks (multi\_hit), +5 extra attacks (do\_multi\_hit), −50 parry chance for attacker, −50 dodge chance for attacker |
| DPOWER\_ARMS        | Disarm: attacker −30, drow +30 |
| DPOWER\_LEVITATION  | Immune to trip and sweep (fight.c) |
| CLASS\_DROW base    | `max_dam += 500` flat bonus each round |
| NEW\_DROWHATE       | `max_dam += 650` (toggle) |
| NEW\_DFORM          | `max_dam += 650` (toggle); +400 hitroll/damroll |
| SPC\_DROW\_WAR       | +2 / +3 extra attacks per round |
| Backstab special   | Drow backstab: `dam += number_range(100,1000)` then `dam *= number_range(7,10)` |

---

## Toggle Buff Commands

These should be kept active at all times by the bot.

| Command       | Power Required          | State Check                          | Notes |
|---------------|-------------------------|--------------------------------------|-------|
| `drowsight`   | DPOWER\_DROWSIGHT        | `IS_SET(ch->act, PLR_HOLYLIGHT)`     | Truesight — see invisible/hidden |
| `drowhate`    | DPOWER\_DROWHATE         | `IS_SET(ch->newbits, NEW_DROWHATE)`  | +650 max\_dam |
| `darktendrils`| DPOWER\_DARKTENDRILS     | `IS_SET(ch->newbits, NEW_DARKTENDRILS)` | Extra multi\_hit round + dodge |
| `fightdance`  | DPOWER\_FIGHTDANCE       | `IS_SET(ch->newbits, NEW_FIGHTDANCE)` | Auto-attack; only useful with whip equipped |
| `spiderform`  | DPOWER\_SPIDERFORM       | `IS_SET(ch->newbits, NEW_DFORM)`     | +400 hitroll/damroll, +650 max\_dam; blocked if AFF\_POLYMORPH |

> **Note**: `drowshield` (`IMM_SHIELDED`) hides the drow's aura from others — it has
> no defensive value against NPCs.  The bot does not keep it active.

---

## Combat Commands (require target)

| Command           | Power Required    | Cost  | Notes |
|-------------------|-------------------|-------|-------|
| `garotte <t>`     | DPOWER\_GAROTTE    | 12-beat lag | Requires whip in WEAR\_WIELD or WEAR\_HOLD; auto-kills NPC on lucky roll (4 specific percents → victim->hit=1 + 1000 dam) |
| `drowfire <t>`    | DPOWER\_DROWFIRE   | 100 mana | Fire spell via `skill_lookup("drowfire")`; 12-beat lag |
| `earthshatter`    | DPOWER\_EARTHSHATTER | 150 mana | AoE damage to entire room; 12-beat lag |
| `web <t>`         | DPOWER\_WEB        | (spell) | Immobilizes target (AFF\_WEBBED); 12-beat lag |
| `confuse`         | DPOWER\_CONFUSE    | 75 move | Forces target to flee; 75% success; 16-beat lag |

> **garotte weapon requirement**: the bot must have a drow whip (`value[3] == 4`)
> in `WEAR_WIELD` or `WEAR_HOLD`.  Create one via `drowcreate whip` (costs 60
> primal).

---

## Whip Requirement and Setup

`garotte` and `fightdance` both require a whip (weapon type 4) or piercing weapon
(type 11) equipped.  The bot should:

1. Issue `drowcreate whip` (60 primal) when no whip is in inventory or wielded.
2. Issue `wield whip` on the following tick to equip it.

This is handled in the `between_fights` hook.

---

## Bot AI Grant Priority

Powers are purchased with `grant self <name>` in this order:

```
1.  levitation    (1,000)  — trip/sweep immunity; cheapest power
2.  drowpoison    (2,500)  — passive poison every hit
3.  drowsight     (5,000)  — truesight
4.  toughskin     (7,500)  — incoming dam /= 3 (huge survivability)
5.  speed         (7,500)  — +3/+5 extra attacks, major dodge bonus
6.  garotte       (5,000)  — burst damage / NPC instakill
7.  earthshatter  (7,500)  — AoE combat spell
8.  drowfire      (2,500)  — ranged fire attack
9.  web           (5,000)  — immobilize target
10. confuse       (2,500)  — force flee (combat opener)
11. fightdance   (10,000)  — auto-attack each round with whip
12. darkness      (7,500)  — escape utility; enables dark garotte
13. dgarotte      (2,500)  — enhanced garotte in darkness (requires garotte)
14. drowhate     (20,000)  — toggle +650 max_dam
15. darktendrils (25,000)  — toggle extra attacks + dodge
16. spiderarms   (25,000)  — disarm resistance
17. spiderform   (25,000)  — spider transform (+400 hit/dam, +650 max_dam)
18. drowshield    (5,000)  — PvP aura hide (low priority)
19. shadowwalk   (10,000)  — mobility (low priority for grinder)
(skip glamour — cosmetic only)
```

---

## Between-Fights Setup

Handled in the `between_fights` hook (runs each grinding tick, not in combat):

```
1. No whip in WEAR_WIELD/WEAR_HOLD, whip in inventory  → wield whip
2. No whip anywhere, practice >= 60                    → drowcreate whip
3. generation >= 3, no profession set                  → grant self warrior
```

---

## Pre-Combat Buff Order (buff_check, between fights)

```
1. drowsight    — if DPOWER_DROWSIGHT and not PLR_HOLYLIGHT
2. drowhate     — if DPOWER_DROWHATE and not NEW_DROWHATE
3. darktendrils — if DPOWER_DARKTENDRILS and not NEW_DARKTENDRILS
4. fightdance   — if DPOWER_FIGHTDANCE and not NEW_FIGHTDANCE (and whip equipped)
5. spiderform   — if DPOWER_SPIDERFORM and not NEW_DFORM and not AFF_POLYMORPH
```

---

## Combat Action Priority (each combat tick)

```
1. roll 1–30:   garotte <target>     — burst/instakill (needs whip)
2. roll 31–55:  drowfire <target>    — fire spell (mana >= 100)
3. roll 56–70:  earthshatter         — AoE (mana >= 150)
4. roll 71–83:  web <target>         — immobilize
5. roll 84–90:  confuse              — force flee (move >= 75)
```

---

## Source Files

| File               | Contents |
|--------------------|----------|
| `src/drow.c`       | `do_grant`, `do_drowfire`, `do_heal`, `do_shadowwalk`, `do_drowhate`, `do_darktendrils`, `do_fightdance`, `do_spiderform`, `do_drowsight`, `do_drowshield`, `do_darkness`, `do_confuse`, `do_earthshatter`, `do_drowcreate`, `do_drowpowers` |
| `src/fight.c`      | `do_garotte` (line ~5273), `do_dark_garotte` (line ~5339), passive effects: TOUGHSKIN, DROWPOISON, SPEED, ARMS, DARKTENDRILS, DROWHATE/DFORM max\_dam, backstab multiplier |
| `src/clan.c`       | `do_web` (line ~4274) |
| `src/merc.h`       | `NEW_DROWHATE` (65536), `NEW_DARKNESS` (131072), `NEW_DFORM` (16777216), `NEW_DARKTENDRILS` (67108864), `NEW_FIGHTDANCE` (536870912), `SPC_DROW_WAR` (128), `SPC_DROW_MAG` (256), `SPC_DROW_CLE` (512) |
| `src/drow.h`       | `DROW_POWER` (8), `DROW_TOTAL` (9), `DROW_MAGIC` (11), `DPOWER_*` bitmask constants |
| `src/bot_ai_drow.c`| Bot AI implementation (this class) |
