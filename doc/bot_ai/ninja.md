# Ninja Class — Bot AI Reference

## Overview

Ninjas combine two independent progression tracks: a **belt rank** system
(`ch->pcdata->rank`, purchased with exp) and three **principle trees**
(`ch->pcdata->powers[NPOWER_*]`, purchased with primal via `principles <name> improve`).
Class is acquired at avatar level from an existing ninja via `guide <player>`.

Primary damage multipliers:

- **Chikyu** — passive toughness/damage/attacks (dominates early and mid game).
- **Belt rank** — extra attacks per round (scales 1–5 from belt 1–10).
- **Rage/michi** — `max_dam += rage * 4`; full michi = +400 max\_dam.

---

## Belt Rank Progression

| Belt | `pcdata->rank` | Exp Cost | Extra Attacks |
|------|----------------|----------|---------------|
| 1    | BELT\_ONE (7)  | 5M       | +1            |
| 2    | BELT\_TWO (8)  | 10M      | +1            |
| 3    | BELT\_THREE (9)| 15M      | +2            |
| 4    | BELT\_FOUR (10)| 20M      | +2            |
| 5    | BELT\_FIVE (11)| 25M      | +2            |
| 6    | BELT\_SIX (12) | 30M      | +3            |
| 7    | BELT\_SEVEN(13)| 35M      | +3            |
| 8    | BELT\_EIGHT(14)| 40M      | +4            |
| 9    | BELT\_NINE (15)| 45M      | +4            |
| 10   | BELT\_TEN (16) | 50M      | +5            |

Belts are bought with `train belt<n>`.  No prerequisites other than rank and exp.

---

## Principle System

Three trees, each 1–6 levels.  Cost per step: `(current_level + 1) × 10` primal
(`ch->practice`).  Total to max all three: 630 primal.

Command: `principles <sora|chikyu|ningenno> improve`

### Chikyu (`NPOWER_CHIKYU`, index 2)

| Level | Effect |
|-------|--------|
| 1     | Incoming damage `/= 2.2` (passive — fight.c) |
| 2     | `max_dam += 500` (flat bonus in fight.c) |
| 3     | +3 extra attacks per round |
| 4     | Improved dodge bonus |
| 5     | Further dodge improvement |
| 6     | `harakiri` command unlocked (sets hp/mana/move = 1; **bot must never call this**) |

### Ningenno (`NPOWER_NINGENNO`, index 3)

| Level | Effect / Command |
|-------|-----------------|
| 1     | `tsume` toggle — IronClaws (`VAM_CLAWS`); unarmed attack enhancement |
| 2     | `hakunetsu` (strangle) — 4× backstab vs full-HP target; 12-beat lag |
| 3     | `mienaku` — guaranteed flee (200 move) |
| 4     | Auto-poison on shiroken throws |
| 5     | Passive shiroken: 3–5 auto-hits each combat round |
| 6     | `circle` command — 1–2 one\_hit strikes; requires piercing weapon (value[3]==11) |

### Sora (`NPOWER_SORA`, index 1)

| Level | Effect / Command |
|-------|-----------------|
| 1     | `scry` — remote room inspection |
| 2     | `koryou` — (spirit ability) |
| 3     | `kakusu` — enhanced hide (`AFF_HIDE`; 500 move; requires no fight\_timer) |
| 4     | `mitsukeru` — silent walk |
| 5     | `kanzuite` — truesight toggle (`PLR_HOLYLIGHT`; 500 move) |
| 6     | `bomuzite` — sleep gas (room AoE; cannot be used in combat) |

---

## Rage / Michi System

| Field | Meaning |
|-------|---------|
| `ch->rage` | Current rage level (0–100+) |
| `max_dam += rage * 4` | Applied in fight.c every round |

**Full michi** (`rage = 100`) adds **+400 max\_dam** per round.

**Activating** (`michi` command, requires `POS_FIGHTING`, 500 move):
`ch->rage += 100`, 12-beat `WAIT_STATE`.

Rage persists between fights.  Once `rage >= 100`, michi is no longer needed.

---

## Toggle / Passive Buff Commands

| Command  | Requirement          | State Check                      | Notes |
|----------|----------------------|----------------------------------|-------|
| `kanzuite` | NPOWER\_SORA ≥ 5, move ≥ 500 | `IS_SET(ch->act, PLR_HOLYLIGHT)` | Truesight; usable outside combat (`POS_MEDITATING`) |
| `tsume`  | NPOWER\_NINGENNO ≥ 1 | `IS_VAMPAFF(ch, VAM_CLAWS)`      | IronClaws; requires `POS_FIGHTING` — handle in combat\_action |

> **Note**: `tsume` requires `POS_FIGHTING` in the command table so it
> **cannot** be issued in `buff_check`.  It must be checked and activated
> as the first action in `combat_action` each fight.

---

## Combat Commands (require target or active fight)

| Command    | Requirement              | Notes |
|------------|--------------------------|-------|
| `michi`    | POS\_FIGHTING, move ≥ 500, rage < 100 | +100 rage → +400 max\_dam; 12-beat lag |
| `hakunetsu`| NPOWER\_NINGENNO ≥ 2; `victim->hit == victim->max_hit` | 4× backstab opener; 12-beat lag; POS\_STANDING |
| `circle`   | NPOWER\_NINGENNO ≥ 6; wielded piercing weapon (value[3]==11) | 1–2 hits; 8-beat lag; conflicts with tsume |

> **hakunetsu** requires the target to be at **full HP** — not viable mid-combat.
> Useful only as a fight opener; the bot grinding loop handles this naturally.
>
> **circle** requires a piercing weapon in hand.  A ninja with `tsume` active
> has no wielded weapon.  Bots using tsume should skip circle.

---

## Passive Combat Effects

| Source | Effect |
|--------|--------|
| NPOWER\_CHIKYU ≥ 1 | Incoming `dam /= 2.2` each hit (fight.c) |
| NPOWER\_CHIKYU ≥ 2 | `max_dam += 500` (flat bonus) |
| NPOWER\_CHIKYU ≥ 3 | +3 extra attacks per round |
| Belt rank          | Extra attacks per round (see belt table above) |
| NPOWER\_NINGENNO ≥ 5 | Automatic shiroken throw each round: 3–5 `one_hit` calls |
| Belt ≥ BELT\_EIGHT + circle | Belt 8 doubles circle damage (fight.c) |
| `ch->rage`         | `max_dam += rage * 4` per round |

---

## Bot AI Training Priority

Principles are cheap (primal) and should be bought eagerly.
Belts are expensive (exp) and accumulate over time.
Both resources are independent — buy each when available.

**Principles** (in order, primal):
```
1.  Chikyu  → 1   (incoming dam /= 2.2 — biggest single survivability gain)
2.  Chikyu  → 2   (+500 max_dam)
3.  Chikyu  → 3   (+3 extra attacks)
4.  Ningenno → 1  (tsume — unarmed claws)
5.  Ningenno → 2  (hakunetsu strangle opener)
6.  Sora    → 5   (kanzuite truesight; buys Sora 1-5 in sequence)
7.  Ningenno → 5  (passive shiroken: auto 3-5 hits/round)
8.  Ningenno → 6  (circle — lower priority; conflicts with tsume build)
9.  Chikyu  → 6   (harakiri unlock; bot does not call harakiri)
10. Sora    → 6   (bomuzite; completes tree)
```

**Belts** (in order, exp):
```
11. Belt 1  (5M exp)
12. Belt 2  (10M exp)
...
20. Belt 10 (50M exp — full +5 extra attacks)
```

---

## Combat Action Priority (each combat pulse)

```
1. NINGENNO ≥ 1, !VAM_CLAWS  → tsume          (activate claws; POS_FIGHTING only)
2. rage < 100, move ≥ 500    → michi           (+100 rage → +400 max_dam)
3. (fallback)                → normal multi_hit loop
```

---

## Pre-Combat Buff Order (buff\_check, between fights)

```
1. kanzuite — if Sora ≥ 5 and not PLR_HOLYLIGHT and move ≥ 500
```

---

## Source Files

| File              | Contents |
|-------------------|----------|
| `src/ninja.c`     | `do_principles`, `do_michi`, `do_kakusu`, `do_kanzuite`, `do_stalk`, `do_tsume`, `do_mienaku`, `do_bomuzite`, `do_strangle` |
| `src/fight.c`     | `do_circle` (line ~5400), Chikyu damage reduction/max\_dam/attacks, belt extra attacks, Ningenno shiroken auto-fire, rage max\_dam |
| `src/clan.c`      | `do_claws` (VAM\_CLAWS handler shared with vampires/demons) |
| `src/interp.c`    | Command table entries: michi (POS\_FIGHTING), tsume (POS\_FIGHTING), kanzuite (POS\_MEDITATING), hakunetsu/strangle (POS\_STANDING), circle (POS\_FIGHTING) |
| `src/merc.h`      | `BELT_ONE`–`BELT_TEN` (7–16), `VAM_CLAWS`, `PLR_HOLYLIGHT`, `IS_VAMPAFF` |
| `src/ninja.h`     | `NPOWER_SORA` (1), `NPOWER_CHIKYU` (2), `NPOWER_NINGENNO` (3), `HARA_KIRI` (5) |
| `src/bot_ai_ninja.c` | Belt + principle training, buff\_check (kanzuite), combat\_action (tsume + michi) |
