# Angel Class Reference (Bot AI)

## Overview

Angel is a prestige evolution class from Monk (`CLASS_ANGEL = 2048`). It is themed around divine justice and mercy — a hybrid support/combat class with strong self-healing, passive damage reduction, and moderate offensive output.

**Key files:**
- `src/angel.c` — all angel ability implementations
- `src/angel.h` — power/bit constants
- `src/fight.c` — combat integration (damage, defense, attack count)
- `src/update.c:1749-1764` — `update_angel()` passive regen and ANGEL_EYE upkeep
- `src/update.c:1546-1560` — ANGEL_EYE tick cost
- `src/act_move.c:2729-2794` — power training via `train` command
- `src/upgrade.c` — Monk→Angel class evolution requirements

---

## Rank (Generation) Titles

Angels display generation-based titles. Generation 1 is the highest prestige.

| Generation | Title          |
|------------|----------------|
| 1          | Arch Angel     |
| 2          | Cherubim       |
| 3          | Seraphim       |
| 4          | Guardian Angel |
| 5          | Angel          |
| Other      | Nephalim       |

---

## Power Track System

Angels have **4 power tracks**, each 0–5 levels, stored in `ch->pcdata->powers[]`:

| Constant            | Index | Theme                  |
|---------------------|-------|------------------------|
| `ANGEL_PEACE`       | 1     | Defense / Healing      |
| `ANGEL_LOVE`        | 2     | Support / Regen        |
| `ANGEL_JUSTICE`     | 3     | Offense / Mobility     |
| `ANGEL_HARMONY`     | 4     | Defense / Aura / Banish|
| `ANGEL_POWERS`      | 5     | Toggle bit flags       |
| `ANGEL_PEACE_COUNTER`| 6    | Cooldown timer         |

**Training:** `train peace|love|justice|harmony` — costs `(current_level + 1) * 10,000,000` exp per level.

---

## Toggle Bit Flags (`powers[ANGEL_POWERS]`)

| Constant      | Bit | Command       | Requirement        | Effect |
|---------------|-----|---------------|--------------------|--------|
| `ANGEL_WINGS` | 1   | `awings`      | ANGEL_JUSTICE >= 1 | Trip immunity; enables `swoop` |
| `ANGEL_HALO`  | 2   | `halo`        | ANGEL_JUSTICE >= 2 | Random spell proc each combat round |
| `ANGEL_AURA`  | 4   | `angelicaura` | ANGEL_HARMONY >= 2 | Extra `heavenlyaura` multi_hit per attack |
| `ANGEL_EYE`   | 8   | `eyeforaneye` | ANGEL_JUSTICE >= 5 | Reflects portion of incoming damage back; costs 1600 move + 1600 mana per tick |

---

## PEACE Track

Governs defense, parry enhancement, and self-healing.

| Level | Ability       | Type   | Effect | Cooldown |
|-------|---------------|--------|--------|----------|
| 1     | `gpeace`      | Toggle | Applies `AFF_PEACE` — see AFF_PEACE section | None (mana upkeep per tick) |
| 2     | `spiritform`  | Toggle | Applies `AFF_ETHEREAL` (intangible) | None |
| 3     | `innerpeace`  | Active | Heal self for `ANGEL_PEACE * 500` HP; costs 1500 mana | 18 ticks |
| 5     | `houseofgod`  | Active | Cast `do_peace` at level 12 — heals all PCs in room; 50-tick cooldown (`ANGEL_PEACE_COUNTER`) | 24 ticks |

**Passive combat effects (scale with ANGEL_PEACE level):**
- Defender dodge chance: opponent's dodge roll `+ANGEL_PEACE * 9%` (harder to hit you via `fight.c:2472, 2700`)
- Defender parry chance: opponent's parry roll `+ANGEL_PEACE * 3%` (`fight.c:2517, 2745`)

**AFF_PEACE upkeep:** Costs mana per tick (via `update_safe_powers`, `cost = 800`). Angels do not lose AFF_PEACE from mana drain alone; it requires explicit toggle-off.

---

## LOVE Track

Governs detection, form shift, ally healing, and passive regen.

| Level | Ability      | Type    | Effect | Cooldown |
|-------|--------------|---------|--------|----------|
| 1     | `gsenses`    | Toggle  | Grants `PLR_HOLYLIGHT` — see invisible/hidden | None |
| 2     | `gfavor`     | Toggle  | Transform to angel form: +400 damroll/hitroll, costs 2000 mana + 2000 move; forces alignment to 1000 | None |
| 3     | `forgiveness`| Active  | Heal a non-NPC, non-Tanarri target with alignment >= 0 for 1000–1500 HP | 16 ticks |
| 4+    | (passive)    | Passive | Auto-regen HP/mana/move at 2× werewolf_regen rate per tick; auto-regen lost limbs | Always |
| 5     | `martyr`     | Active  | Restore all PCs in room to full (via `do_restore`); self drops to 1 HP/mana/move. Requires self at max HP first | 6 ticks |

---

## JUSTICE Track

Governs offensive combat, mobility, and PvP punishment abilities.

| Level | Ability          | Type   | Effect | Cooldown |
|-------|------------------|--------|--------|----------|
| 1     | `awings`         | Toggle | Toggle `ANGEL_WINGS` flag | None |
| 1     | `swoop`          | Active | Teleport to target's room; costs 500 move; requires `ANGEL_WINGS`; blocked in ROOM_ASTRAL | Instant |
| 2     | `halo`           | Toggle | Toggle `ANGEL_HALO` flag | None |
| 3     | `sinsofthepast`  | Active | Apply `AFF_FLAMING` + `AFF_POISON` to non-NPC, non-angel target; then `one_hit` with `gsn_wrathofgod` | 12 ticks |
| 4     | `touchofgod`     | Active | Deal 100–200 damage to non-NPC target; 33% stun (POS_STUNNED) | 18 ticks |
| 5     | `eyeforaneye`    | Toggle | Toggle `ANGEL_EYE` flag | None |

**Passive combat effects (scale with ANGEL_JUSTICE level):**
- Attack count: `+ANGEL_JUSTICE` extra attacks per round (`fight.c:1035`)
- Damage multiplier: `dam *= (1 + ANGEL_JUSTICE / 10)` (`fight.c:1388`, noted as weak in the source)
- Max damage bonus: `+ANGEL_JUSTICE * 125` (`fight.c:1808`)
- Attacker dodge reduction: enemy dodge roll `-ANGEL_JUSTICE * 9%` (`fight.c:2439, 2665`)
- Attacker parry reduction: enemy parry roll `-ANGEL_JUSTICE * 3%` (`fight.c:2516, 2744`)

**Swoop combat bonus:** If `ANGEL_JUSTICE >= 5`, `swoop` always triggers a `multi_hit` on landing. Otherwise 33% chance (`fight.c:4062`).

**ANGEL_HALO procs** (fight.c:871–896) — each combat round, equal 20% chance of one:
- Cast `curse` at level 50
- Cast `web` at level 50
- Cast `improved heal` at level 50
- Cast `fireball` at level 50
- Cast `godbless` at level 50

---

## HARMONY Track

Governs aura buffs, damage reduction, and banishment of evil targets.

| Level | Ability       | Type   | Effect | Cooldown |
|-------|---------------|--------|--------|----------|
| 2     | `angelicaura` | Toggle | Toggle `ANGEL_AURA` flag | None |
| 3     | `gbanish`     | Active | Deal 500/1000/1500 damage to evil PC (based on alignment ≤0, ≤−500 brackets); 30% chance to teleport victim to hell (ROOM_VNUM_HELL) | 18 ticks |
| 5     | `harmony`     | Active | Cast `spirit kiss` at level 100–200 on target in room (heals and buffs) | 12 ticks |

**Passive combat defense (scale with ANGEL_HARMONY level):**
- Incoming damage reduction: `dam *= (100 − ANGEL_HARMONY * 12) / 100` (`fight.c:1643`)
  - Level 1: 12% reduction | Level 3: 36% | Level 5: 60%

**Max damage bonus:** `+ANGEL_HARMONY * 125` (`fight.c:1810`)

---

## Max Damage Contribution (All Tracks)

Each power track level adds 125 to max damage cap:

| Scenario             | Max Damage Bonus |
|----------------------|-----------------|
| One track at level 5 | +625            |
| All four at level 5  | +2500           |

Formula: `max_dam += ANGEL_JUSTICE*125 + ANGEL_PEACE*125 + ANGEL_HARMONY*125 + ANGEL_LOVE*125` (`fight.c:1808-1811`)

---

## ANGEL_EYE Retaliation Mechanic

When `ANGEL_EYE` is active and the angel takes a hit > 100 damage from a non-angel:
- Reflect `dam/4` to `dam/5` back to attacker (capped at 275–325)
- Will not kill the caster (reflected damage capped to `victim->hit - 1`)

**Upkeep (via `update_safe_powers`):** Costs 1600 move + 1600 mana per tick (`cost*2` where `cost=800`). Automatically removed when either resource is too low.

Source: `fight.c:2080-2098`, `update.c:1546-1560`

---

## Passive: `update_angel()` (Every Tick)

Called each heartbeat for all angels (`update.c:1749-1764`):

- If `ANGEL_LOVE > 3`: auto-regen HP/mana/move at 2× werewolf_regen rate; auto-regen lost limbs
- Decrement `ANGEL_PEACE_COUNTER` (houseofgod cooldown timer)
- If in angel sanctuary rooms (vnums 93340–93349): regen at 1× rate regardless of LOVE level

---

## Equipment Creation (`angelicarmor`)

No power requirement — any angel can use. Costs **150 practice points** per piece.

| Piece     | VNUM  |
|-----------|-------|
| ring      | 33180 |
| bracer    | 33181 |
| necklace  | 33182 |
| belt      | 33183 |
| helmet    | 33184 |
| cloak     | 33185 |
| visor     | 33186 |
| plate     | 33187 |
| leggings  | 33188 |
| boots     | 33189 |
| gauntlets | 33190 |
| sleeves   | 33191 |
| sword     | 33192 |

Items are quest-bound to the creator.

---

## Communication

- `prayer` — Angel-only global channel (`CLASS_ANGEL` or immortal)

---

## Restrictions & Interactions

- `sinsofthepast` — blocked vs angels and NPCs
- `touchofgod` — blocked vs NPCs and safe zones
- `gbanish` — blocked vs NPCs, alignment > 500, safe zones
- `forgiveness` — blocked vs NPCs, Tanarri, alignment < 0
- `martyr` — requires self at full HP before use
- `gfavor` — forces `ch->alignment = 1000`; applying while already in angel form removes it
- `swoop` — blocked if target is in `ROOM_ASTRAL` or caster is in `ROOM_ASTRAL`; also blocked if target has `IMM_TRAVEL`
- `ANGEL_WINGS` — grants immunity to trip (`fight.c:3985`)
- `AFF_ETHEREAL` — flagged as cheating if used in arena rooms (`update.c:1507`)
- `AFF_PEACE` — flagged as cheating if active in arena (`update.c:1505`)

---

## Origin

Angels evolve from **Monk** (generation 1) at the Temple Altar of Midgaard (vnum 3054). Monks must have all superstances (stances 19–23) trained and meet high HP/mana/move/QP/pkscore thresholds.

---

## Bot AI Priority Notes

### Toggle Buffs (enable at login / combat start)
1. `gsenses` — always on, no cost, enables true-seeing
2. `awings` — always on (if ANGEL_JUSTICE >= 1); trip immunity + enables swoop
3. `gfavor` — enable after each login/regen; large hit/dam bonus; re-enable if dispelled
4. `angelicaura` — always on (if ANGEL_HARMONY >= 2); free extra hit per round
5. `halo` — always on (if ANGEL_JUSTICE >= 2); random combat spell procs
6. `gpeace` — always on (if ANGEL_PEACE >= 1); mana upkeep but worthwhile for defense
7. `eyeforaneye` — situational; high mana/move upkeep, valuable in sustained PvP

### Active Combat Abilities (priority order in fight loop — PvE)
Note: `sinsofthepast`, `touchofgod`, and `gbanish` are **PC-only** — cannot be used vs NPCs.

1. `innerpeace` — highest priority when HP < 50%; costs 1500 mana, heals ANGEL_PEACE×500
2. `harmony` — cast spirit kiss on self or ally for healing/buff (12-tick cooldown)
3. `swoop` — mobility; use to reach target room, triggers combat entry bonus at JUSTICE 5

### Healing Abilities (priority order)
1. `innerpeace` — self-heal; use when HP < 50%
2. `houseofgod` — powerful AoE heal for whole room; long 50-tick cooldown; low priority for solo bot
3. `forgiveness` — ally heal; only works on non-NPC PC with alignment >= 0; PvP/group support

### Utility
- `spiritform` — escape tool (AFF_ETHEREAL); use only when fleeing is blocked
- `martyr` — risky for bots (drops to 1 HP); skip unless specifically designed for group support bot

### Suggested Training Order
**ANGEL_JUSTICE first** — most combat benefit (attacks, accuracy, max damage):
- Justice 1 → 2 → 3 (awings, halo, sinsofthepast)
- Harmony 2 (angelicaura — free extra hit)
- Justice 4 → 5 (touchofgod, eyeforaneye)
- Harmony 3 → 4 → 5 (gbanish, harmony)
- Peace 1 → 2 → 3 (gpeace, spiritform, innerpeace)
- Love 1 → 2 → 3 → 4 (gsenses, gfavor, forgiveness, passive regen)
- Peace 4 → 5 (innerpeace upgrade, houseofgod) — optional endgame
- Love 5 (martyr) — optional group support only
