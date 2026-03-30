# Monk Class — Bot AI Reference

## Overview

Monks use a **chi energy** system (`ch->chi[CURRENT/MAXIMUM]`) as their primary damage
multiplier, combined with **fight styles / techniques** (bitflags in `ch->monkstuff`),
**four core abilities** (`ch->monkab[]`), and **mantras** (`ch->pcdata->powers[PMONK]`).
All progression is **immediate** — no research phase — paid in exp or primal.
Class is granted at avatar level by an existing monk via `guide <player>`.

---

## Chi System

| Field | Meaning |
|---|---|
| `ch->chi[CURRENT]` | Active chi level this fight (0–6) |
| `ch->chi[MAXIMUM]` | Mastery cap, raised via `learn chi` |

**Damage multiplier** (applied in `find_dam()`):

| `chi[CURRENT]` | Multiplier |
|---|---|
| 0 | ×1.0 (no bonus) |
| 1–2 | ×1.2 |
| 3 | ×1.5 |
| 4 | ×2.0 |
| 5 | ×2.5 |
| 6 | ×3.0 |

**Activating chi** (`chi` command, requires `POS_FIGHTING`):
cost = `500 + (chi[CURRENT]+1) × 20` move per step

**Raising `chi[MAXIMUM]`** (`learn chi`):
cost = `(chi[MAXIMUM]+1) × 1,000,000` exp per level (1M … 6M; 21M total to max)

Chi persists between fights. Use `relax` (outside combat only) to lower `chi[CURRENT]`
by 1 if needed.

---

## Stance System

Five basic stances trained via `autostance <name>` + the autodrop engine.
`ch->stance[slot]` holds XP; 200 XP = mastered. `ch->stance[12]` (MONK_AUTODROP)
holds the current training target.

| Slot | Constant | Name |
|---|---|---|
| 1 | `STANCE_VIPER` | viper |
| 2 | `STANCE_CRANE` | crane |
| 3 | `STANCE_CRAB` | crab |
| 4 | `STANCE_MONGOOSE` | mongoose |
| 5 | `STANCE_BULL` | bull |

Bot trains in order: viper → crane → crab → mongoose → bull.

---

## Fight Styles (50,000 exp each)

Learned via `learn fight <arg>`. Stored as bitflags in `ch->monkstuff`.
Checked with `IS_FS(ch, FS_*)`. 20 styles × 50K = 1,000,000 exp total.

| Constant | Arg |
|---|---|
| `FS_TRIP` | trip |
| `FS_KICK` | kick |
| `FS_BASH` | bash |
| `FS_ELBOW` | elbow |
| `FS_KNEE` | knee |
| `FS_HEADBUTT` | headbutt |
| `FS_DISARM` | disarm |
| `FS_BITE` | bite |
| `FS_DIRT` | dirt |
| `FS_GRAPPLE` | grapple |
| `FS_PUNCH` | punch |
| `FS_GOUGE` | gouge |
| `FS_RIP` | rip |
| `FS_STAMP` | stamp |
| `FS_BACKFIST` | backfist |
| `FS_JUMPKICK` | jumpkick |
| `FS_SPINKICK` | spinkick |
| `FS_HURL` | hurl |
| `FS_SWEEP` | sweep |
| `FS_CHARGE` | charge |

---

## Techniques (200,000 exp each)

Learned via `learn techniques <arg>`. Same `ch->monkstuff` field as fight styles.
8 techniques × 200K = 1,600,000 exp total.

| Constant | Arg | Command Unlocked | Notes |
|---|---|---|---|
| `TECH_SHIN` | shin | `shinkick` | Combo starter (COMB_SHIN) |
| `TECH_KNEE` | knee | `knee` | Combo step (COMB_KNEE) |
| `TECH_THRUST` | thrust | `thrustkick` | Combo starter (COMB_THRUST1/2) |
| `TECH_SPIN` | spin | `spinkick` | Combo finisher |
| `TECH_SWEEP` | sweep | `sweep`, `reverse` | Combo step / finisher |
| `TECH_ELBOW` | elbow | `elbow` | Damage filler |
| `TECH_BACK` | backfist | `backfist` | Damage filler |
| `TECH_PALM` | palm | `palmstrike` | Stuns target (POS_STUNNED) |

---

## Combo Chains (ch->monkcrap bitmask)

The game engine tracks combo state in `ch->monkcrap`. The bot just mixes techniques
at natural ratios; finishers trigger automatically when state aligns.

### Lightning Kick (Raptor Strike)
1. `thrustkick` → sets `COMB_THRUST1`
2. `thrustkick` → sets `COMB_THRUST2`
3. `spinkick` → **Lightning Kick**: up to 6 hits scaled by `chi[CURRENT]`
   — or — **Raptor Strike**: siphons half victim's mana into attacker's hp/move/mana

### Tornado Kick
1. `shinkick` → sets `COMB_SHIN`
2. `knee` → sets `COMB_KNEE` (requires `COMB_SHIN`)
3. `spinkick` → **Tornado Kick**: hits all room targets `(chi[CURRENT]+1)` times

### Choyoken (Spinning Reversal)
1. `reverse` → sets `COMB_REV1`
2. `sweep` → sets `COMB_SWEEP` (requires `COMB_REV1`)
3. `reverse` → **Choyoken**: drains half victim's move into attacker's move/hp/mana

### Neck Pinch (Paralyse)
1. `reverse` → sets `COMB_REV1`
2. `knee` → sets `COMB_KNEE`
3. `sweep` → **Neck Pinch**: paralyses victim for 30 lag beats

---

## Core Abilities (ch->monkab[], 0–4 levels, 500,000 exp each)

Learned via `learn abilities <arg>`. 4 abilities × 4 levels × 500K = 8,000,000 exp total.

| Index | Constant | Arg | Key Unlocks |
|---|---|---|---|
| 0 | `AWARE` | awareness | Awareness bonuses |
| 1 | `BODY` | body | ≥ 1: `adamantium`; ≥ 3: `spiritpower` (+200 hit/dam) |
| 2 | `COMBAT` | combat | Combat stat bonuses |
| 3 | `SPIRIT` | spirit | ≥ 3: `healingtouch`; ≥ 4: `deathtouch` |

---

## Mantras (ch->pcdata->powers[PMONK], 0–14)

Learned via `mantra power improve`. Cost: `(current_level + 1) × 10` primal per step.
Level 1 = 10 primal, level 14 = 140 primal (1,050 primal total to max).

| Level | Command | Toggle / Effect |
|---|---|---|
| 1 | `godseye` | Toggle `PLR_HOLYLIGHT` — truesight (see invis/hidden) |
| 2 | `godsbless` | Spell — casts godbless on self (3,000 mana) |
| 3 | `sacredinvis` | Toggle `AFF_HIDE` — holy invisibility (500 move) |
| 4 | `wrathofgod` | Combat — 4× `one_hit` vs NPC only |
| 5 | `flaminghands` | Toggle `NEW_MONKFLAME` — fire damage on strikes |
| 6 | `steelskin` | Toggle `NEW_MONKSKIN` — defensive damage shield |
| 7 | `godsbless` | (upgraded version) |
| 8 | `darkblaze` | Combat — blind target + strip detect invis/hidden (18-beat lag) |
| 8 | `godsfavor` | Toggle `NEW_MONKFAVOR` — Almighty's blessing (1,500 move) |
| 9 | `chaoshands` | Toggle `ITEMA_CHAOSHANDS` — all 5 elemental shields |
| 10 | `celestial` | Teleport — move to any target world-wide (250 move) |
| 11 | `cloak` | Toggle `NEW_MONKCLOAK` — Cloak of Life (1,000 move) |
| 12 | `godsheal` | Combat: +150 HP (400 mana); resting: +500 HP |
| 13 | `ghold` | Control — God's Hold on target |
| 14 | — | Max; all mantras learned |

---

## Toggle Buffs (bot buff_check)

All `newbits` flags checked with `IS_SET(ch->newbits, NEW_*)`.
`chaoshands` uses `IS_ITEMAFF(ch, ITEMA_CHAOSHANDS)`.

| Command | Flag | Requirement | Notes |
|---|---|---|---|
| `godseye` | `PLR_HOLYLIGHT` | PMONK ≥ 1 | Truesight |
| `adamantium` | `NEW_MONKADAM` | monkab[BODY] ≥ 1 | Hardened hands |
| `flaminghands` | `NEW_MONKFLAME` | PMONK ≥ 5 | Fire damage |
| `steelskin` | `NEW_MONKSKIN` | PMONK ≥ 6 | Damage shield |
| `spiritpower` | `NEW_POWER` | monkab[BODY] ≥ 3, move ≥ 100 | +200 hit/dam; costs 25 move |
| `godsfavor` | `NEW_MONKFAVOR` | PMONK ≥ 8, move ≥ 1,500 | Almighty's blessing |
| `chaoshands` | `ITEMA_CHAOSHANDS` | PMONK ≥ 9 | All 5 elemental shields |
| `cloak` | `NEW_MONKCLOAK` | PMONK ≥ 11, move ≥ 1,000 | Cloak of Life |

---

## Active Combat Commands

| Command | Requirement | Notes |
|---|---|---|
| `chi` | chi[CURRENT] < chi[MAXIMUM]; move ≥ 500+(level×20) | Raises chi; primary damage multiplier |
| `wrathofgod` | PMONK ≥ 4 | 4× `one_hit`, NPC targets only |
| `darkblaze` | PMONK ≥ 8 | Blinds; strips detect invis/hidden; 18-beat lag |
| `godsheal` | PMONK ≥ 12, mana ≥ 400 | +150 HP in combat; costs 400 mana |
| `thrustkick` | `TECH_THRUST` | Combo starter; leads to lightning kick |
| `spinkick` | `TECH_SPIN` | Combo finisher; fires lightning kick or tornado kick |
| `shinkick` | `TECH_SHIN` | Combo starter; leads to tornado kick |
| `knee` | `TECH_KNEE` | Combo step |
| `elbow` | `TECH_ELBOW` | Damage filler |
| `backfist` | `TECH_BACK` | Damage filler |
| `palmstrike` | `TECH_PALM` | Stuns target |
| `sweep` | `TECH_SWEEP` | Combo step / finisher |
| `reverse` | `TECH_SWEEP` | Choyoken combo starter |

---

## Bot AI Training Priority

1. Mantras 1–4 (primal — godseye, shield/scry, sacredinvis, wrathofgod)
2. Techniques: shin, knee, thrust, spin (unlock core combo chains)
3. Chi 1–2 (1M + 2M exp — 1.2× damage multiplier begins)
4. Fight styles: kick, trip, bash, knee, elbow
5. Techniques: sweep, elbow, backfist, palm
6. Mantras 5–9 (flaminghands through chaoshands)
7. Body ability 1–3 (adamantium at 1; spiritpower = +200 hit/dam at 3)
8. Chi 3 (3M exp — 1.5× multiplier)
9. Spirit ability 1–4 (healingtouch at 3; deathtouch at 4)
10. Remaining 15 fight styles
11. Mantras 10–14
12. Combat + Aware abilities to 4
13. Body / Spirit abilities to 4
14. Chi 4–6 (4M + 5M + 6M exp — up to 3× multiplier)
