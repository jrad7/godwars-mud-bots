# Vampire Class — Bot AI Reference

## Overview

Vampires use a **discipline** system (`ch->power[DISC_VAMP_*]`) with 19+ disciplines,
each advanceable to level 10. Discipline advancement requires **research + training**
(not automatic). Blood (`condition[COND_THIRST]`) fuels most powers. Class is selected
at avatar (level 3) via `selfclass vampire`.

---

## Age Rank Progression

Rank is stored in `ch->pcdata->rank`. It caps how high disciplines can be trained.

| Command             | Exp Cost    | Rank Constant      | Disc Max |
|---------------------|-------------|--------------------|----------|
| (start)             | —           | AGE_CHILDE (0)     | 5        |
| `train ancilla`     | 1,500,000   | AGE_NEONATE (1)    | 5        |
| `train elder`       | 7,500,000   | AGE_ANCILLA (2)    | 7        |
| `train methuselah`  | 15,000,000  | AGE_ELDER (3)      | 9        |
| `train lamagra`     | 30,000,000  | AGE_METHUSELAH (4) | 10       |
| `train trueblood`   | 60,000,000  | AGE_LA_MAGRA (5)   | 10       |
| (max)               | —           | AGE_TRUEBLOOD (6)  | 10       |

Additional: `train sunlight` (1,000,000 exp) grants `IMM_SUNLIGHT`.

---

## Discipline Advancement System

1. **Start research**: `research <discipline_name>`
   - Sets `pcdata->disc_research = disc_index`, `disc_points = 0`
2. **Accumulate points**: Gain 1 `disc_point` per kill (2-3 for high-level mobs)
   - Points needed: `(current_level + 1) * 10`
3. **Points done**: `disc_points` set to 999, message sent to player
4. **Train**: `train <discipline_name>`
   - Increments `power[disc_index]`, resets `disc_research = -1`, `disc_points = 0`
5. **Cancel**: `research cancel` — abandons current research

Only one discipline can be researched at a time.

---

## All Vampire Disciplines

| Constant         | Idx | Name            | Commands Unlocked |
|------------------|-----|-----------------|-------------------|
| DISC_VAMP_CELE   | 2   | celerity        | passive (dodge / extra attacks) |
| DISC_VAMP_FORT   | 3   | fortitude       | passive (damage reduction) |
| DISC_VAMP_OBTE   | 4   | obtenebration   | `shroud` (1), `lamprey` (5), `shadowstep` (4), `grab` (8), `shadowgaze` (10) |
| DISC_VAMP_PRES   | 5   | presence        | `awe` (1), `mindblast` (2), `entrance` (3), `summon` (4), `far` (9) |
| DISC_VAMP_QUIE   | 6   | quietus         | `spit` (1), `infirmity` (2), `bloodagony` (3), `assassinate` (4), `vsilence` (5), `flash` (9) |
| DISC_VAMP_THAU   | 7   | thaumaturgy     | `taste` (1), `cauldron` (2), `theft` (4), `tide` (5), `spew` (6), `gourge` (8) |
| DISC_VAMP_AUSP   | 8   | auspex          | `truesight` (1), `readaura` (2), `scry` (3), `astralwalk` (4), `unveil` (5) |
| DISC_VAMP_DOMI   | 9   | dominate        | `command` (1), `mesmerise` (2), `possession` (3), `acid` (4), `baal` (5), `forget` (8) |
| DISC_VAMP_OBFU   | 10  | obfuscate       | `vanish` (1), `mask` (2), `shield` (3), `conceal` (5) |
| DISC_VAMP_POTE   | 11  | potence         | passive (unarmed damage × `power * 0.4`) |
| DISC_VAMP_PROT   | 12  | protean         | `claws` (2), `change` (3), `earthmeld` (4), `flamehands` (5) |
| DISC_VAMP_SERP   | 13  | serpentis       | `darkheart` (1), `scales` (1), `tongue` (4), `tendrils` (4), `cserpent` (5), `coil` (8) |
| DISC_VAMP_VICI   | 14  | vicissitude     | `fleshcraft` (2), `bonemod` (3), `dragonform` (4), `plasma` (5) |
| DISC_VAMP_DAIM   | 15  | daimoinon       | `guardian` (1), `fear` (2), `portal` (3), `bloodwall` (2), `vtwist` (5), `binferno` (6), `servant` (8) |
| DISC_VAMP_ANIM   | 16  | animalism       | `pigeon` (3), `share` (4), `frenzy` (5) |
| DISC_VAMP_CHIM   | 39  | chimerstry      | `mirror` (1), `formillusion` (2), `controlclone` (4) |
| DISC_VAMP_THAN   | 40  | thanatosis      | `hagswrinkles` (1), `putrefaction` (2), `withering` (4), `drainlife` (5) |
| DISC_VAMP_OBEA   | 41  | obeah           | `purification` (7) |
| DISC_VAMP_NECR   | 42  | necromancy      | `preserve` (2), `spiritgate` (3), `spiritguard` (4), `zombie` (5), `bloodwater` (5) |
| DISC_VAMP_MELP   | 43  | melpominee      | `scream` (1) |

---

## Toggle / Passive Buff Commands

These should be kept active at all times by the bot.

| Command        | Disc Required  | State Check                            | Notes |
|----------------|----------------|----------------------------------------|-------|
| `truesight`    | Auspex 1       | `IS_SET(ch->act, PLR_HOLYLIGHT)`       | See invisible / hidden |
| `awe`          | Presence 1     | `IS_EXTRA(ch, EXTRA_AWE)`              | Combat intimidation aura |
| `claws`        | Protean 2      | `IS_VAMPAFF(ch, VAM_CLAWS)`            | Primary melee weapon; drops wielded items |
| `spiritguard`  | Necromancy 4   | `IS_SET(ch->flag2, AFF_SPIRITGUARD)`   | Defensive spirit companion |
| `coil`         | Serpentis 8    | `IS_SET(ch->newbits, NEW_COIL)`        | Combat coil posture |
| `shroud`       | Obtenebration 1| `IS_SET(ch->act, AFF_HIDE)`            | Hide in shadows; blocked if fight_timer > 0 |

One-time permanent effects (not toggles):

| Command      | Disc Required  | Effect |
|--------------|----------------|--------|
| `darkheart`  | Serpentis 1    | Rips out heart → `IMM_STAKE`. Costs ~100 blood. Permanent. |

---

## Combat Commands (require target)

| Command          | Disc Required   | Notes |
|------------------|-----------------|-------|
| `theft <t>`      | Thaumaturgy 4   | Steal 30-40 blood from target; kills mob when practice hits 0 |
| `assassinate <t>`| Quietus 4       | High single-hit burst damage |
| `lamprey <t>`    | Obtenebration 5 | Shadow drain attack |
| `tendrils <t>`   | Serpentis 4     | Serpentis melee attack |
| `drainlife <t>`  | Thanatosis 5    | Drains target HP, heals self |
| `withering <t>`  | Thanatosis 4    | Reduces target stats (debuff) |
| `mindblast <t>`  | Presence 2      | Mental stun |
| `bloodagony <t>` | Quietus 3       | Blood damage |
| `fear <t>`       | Daimoinon 2     | Fear effect |

## Area / Self Combat Commands

| Command        | Disc Required  | Blood Cost | Notes |
|----------------|----------------|------------|-------|
| `scream`       | Melpominee 1   | 50         | Damages all characters in room |
| `purification` | Obeah 7        | 5000 move  | Heals 25% max HP; not usable in combat |

---

## Blood Management

- Pool: `ch->pcdata->condition[COND_THIRST]`
- Max pool ≈ `20000 / ch->generation` (generation 6 → ~3333)
- Most abilities cost 50–300 blood
- `theft <target>` is the primary in-combat refuel method
- Blood drains passively over time

---

## Passive Disciplines (always active)

- **Potence** (DISC_VAMP_POTE): unarmed damage multiplier — `dam *= power * 0.4` (monk2.c:43)
- **Celerity** (DISC_VAMP_CELE): dodge/parry bonus, extra attack chance (fight.c)
- **Fortitude** (DISC_VAMP_FORT): implied defense/damage reduction

No activation command needed. Higher level = stronger effect automatically.

---

## Bot AI Implementation (bot_ai.c)

### Bot Research Priority Order

```
1.  Potence    → 5   (core passive: damage multiplier)
2.  Celerity   → 5   (core passive: dodge + extra attacks)
3.  Fortitude  → 5   (core passive: defense)
4.  Protean    → 2   (unlock claws)
5.  Obtenebration → 5  (shroud + lamprey)
6.  Presence   → 2   (awe + mindblast)
7.  Auspex     → 1   (truesight)
8.  Thaumaturgy → 4  (theft of vitae)
9.  Quietus    → 4   (assassinate)
10. Serpentis  → 4   (tendrils)
11. Thanatosis → 5   (withering + drainlife)
12. Obfuscate  → 1   (vanish)
13. Necromancy → 4   (spiritguard)
14. Max out core disciplines to 10 (Potence/Celerity/Fortitude/Protean/Obtenebration/Presence)
```

### Combat Action Priority (each combat tick)

```
0. blood < 100     → theft <target>          (refuel first)
1. roll 1-25       → drainlife <target>      (heal + damage)
2. roll 26-40      → assassinate <target>    (burst damage)
3. roll 41-55      → lamprey <target>        (shadow drain)
4. roll 56-65      → tendrils <target>       (serpentis)
5. roll 66-75      → withering <target>      (debuff)
6. roll 76-85      → scream                  (area, needs 50 blood)
7. roll 86-90      → mindblast <target>      (stun)
```

### Pre-Combat Buff Order (between fights)

```
1. truesight   (Auspex 1)    — if not PLR_HOLYLIGHT
2. awe         (Presence 1)  — if not EXTRA_AWE
3. claws       (Protean 2)   — if not VAM_CLAWS
4. spiritguard (Necromancy 4)— if not AFF_SPIRITGUARD
```

---

## Source Files

| File             | Contents |
|------------------|----------|
| `src/vamp.c`     | Most vampire commands (~4300 lines) |
| `src/clan.c`     | awe, claws, truesight, vanish, darkheart, shield, etc. |
| `src/act_move.c` | `do_train`, `do_research`, `disc_points_needed`, `gain_disc_points` |
| `src/fight.c`    | Celerity passive combat effects |
| `src/merc.h`     | `DISC_VAMP_*` constants, `AGE_*` constants, field definitions |
| `src/bot_ai.c`   | Bot AI implementation (vampire section) |
