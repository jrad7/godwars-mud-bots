# Tanarri Class Reference (Bot AI)

## Overview

Tanarri is an upgrade class from Demon (`CLASS_TANARRI = 1024`). It is themed around the Blood Wars — a powerful demonic warrior/lord progression with 18 unlockable powers, rank-based stat scaling, and unique AoE combat abilities.

**Key files:**
- `src/tanarri.c` — primary implementation (~700 lines)
- `src/tanarri.h` — power/rank constants
- `src/fight.c` — combat integration (passive effects)
- `src/act_move.c:2621-2726` — rank promotion logic
- `src/clan.c` — toggle ability registration

---

## Rank System

Tanarri progresses through 6 ranks. Each rank gates 3 powers and costs exp to unlock via the `promote` command.

| Rank | Name               | Exp Cost | Total Powers Available |
|------|--------------------|----------|------------------------|
| 0    | Unranked           | —        | 0                      |
| 1    | TANARRI_FODDER     | 10M      | 3                      |
| 2    | TANARRI_FIGHTER    | 20M      | 6                      |
| 3    | TANARRI_ELITE      | 40M      | 9                      |
| 4    | TANARRI_CAPTAIN    | 80M      | 12                     |
| 5    | TANARRI_WARLORD    | 160M     | 15                     |
| 6    | TANARRI_BALOR      | 320M     | 18                     |

**Promotion requirements:** Must have unlocked all powers for the current rank tier AND earned the required exp (deducted on promotion).

---

## Resource: Tanarri Points (TPOINTS)

- Stored in `ch->pcdata->stats[TPOINTS]` (index 8)
- Used to unlock powers via the `bloodsacrifice` command
- Cost per power: `10,000 * (2^rank)`
  - Rank 1: 20,000 pts | Rank 2: 40,000 | Rank 3: 80,000
  - Rank 4: 160,000 | Rank 5: 320,000 | Rank 6: 640,000

---

## Powers by Rank

### Rank 1 — TANARRI_FODDER

| Power              | Type    | Description |
|--------------------|---------|-------------|
| TANARRI_TRUESIGHT  | Toggle  | Grants PLR_HOLYLIGHT — see invisible/hidden. Command: `truesight` |
| TANARRI_CLAWS      | Toggle  | Grants unarmed claw attacks. Command: `claws` |
| TANARRI_EARTHQUAKE | Active  | AoE physical damage vs all non-flying in room. 1000 mana, 12-tick cooldown. Command: `earthquake` |

### Rank 2 — TANARRI_FIGHTER

| Power                | Type    | Description |
|----------------------|---------|-------------|
| TANARRI_EXOSKELETON  | Passive | Reduces incoming physical damage by 80% (×0.20 multiplier) |
| TANARRI_FANGS        | Passive | Adds bonus fang attacks during multi_hit(); doubles if TANARRI_HEAD also active |
| TANARRI_TORNADO      | Active  | AoE lightning damage vs all FLYING targets in room (double strike each). 1500 mana, 12-tick cooldown. Command: `tornado` |

### Rank 3 — TANARRI_ELITE

| Power            | Type    | Description |
|------------------|---------|-------------|
| TANARRI_SPEED    | Passive | +3 attacks/round; opponent parry/dodge −17%; own parry/dodge +6% |
| TANARRI_MIGHT    | Passive | ×1.5 damage multiplier on all attacks; +500 max damage |
| TANARRI_CHAOSGATE| Active  | World teleport to any PC. 2000 move cost, 1/15 fail chance. Command: `chaosgate <target>` |

### Rank 4 — TANARRI_CAPTAIN

| Power          | Type    | Description |
|----------------|---------|-------------|
| TANARRI_FIERY  | Passive | Adds auto fire attack (gsn_fiery) each combat strike; double damage vs Angels |
| TANARRI_FURY   | Toggle  | +250 hitroll/damroll when ON; −5% opponent parry/dodge. Command: `fury` |
| TANARRI_HEAD   | Passive | Extra head: triggers extra fang (with FANGS); opponent parry/dodge −15%; own parry/dodge +5% |

### Rank 5 — TANARRI_WARLORD

| Power            | Type    | Description |
|------------------|---------|-------------|
| TANARRI_BOOMING  | Active  | Single-target physical damage; 25% stun chance (POS_STUNNED 1 round); scales with rank/2. Command: `booming <target>` |
| TANARRI_ENRAGE   | Active  | 60% chance to force berserk on NPC/PC target. 18-tick cooldown. Command: `enrage <target>` |
| TANARRI_FLAMES   | Active  | AoE fire (fireball) vs all in room (double strike each). 2000 mana, 12-tick cooldown. Command: `infernal` |

### Rank 6 — TANARRI_BALOR

| Power            | Type    | Description |
|------------------|---------|-------------|
| TANARRI_TENDRILS | Passive | 70% chance to prevent enemy from fleeing the room |
| TANARRI_LAVA     | Active  | Triple magma strike (×1.5 dmg) + AFF_FLAMING on target. 1000 mana + 1000 move, 18-tick cooldown. Command: `lavablast` |
| TANARRI_EMNITY   | Active  | 60% chance to force two PCs in room to attack each other. 24-tick cooldown. Command: `enmity <t1> <t2>` |

---

## Combat Stat Scaling (by Rank)

These bonuses scale with `ch->pcdata->rank` regardless of which powers are learned:

- **Attack count**: `+rank` extra attacks per round (`fight.c:1033, 1127`)
- **Max damage**: `+rank * 375` (max +2250 at rank 6) (`fight.c:1795`)
- **Magma damage**: ×1.5 multiplier when using TANARRI_LAVA

---

## Toggle Buffs (Always-On When Enabled)

| Toggle         | Command      | Effect |
|----------------|--------------|--------|
| Truesight      | `truesight`  | See invisible/hidden |
| Claws          | `claws`      | Unarmed claw attacks |
| Fury           | `fury`       | +250 hit/dam, −5% opp parry/dodge |

---

## Non-Combat Abilities

### Chaos Surge
- Command: `chaossurge <target>` (target must be PC)
- Damage scales by target alignment: ≤−500: 500 dmg | 0 to +500: 1000 | +500+: 1500
- 12-tick cooldown
- Available to all ranked Tanarri (no power check needed)

### Tanarri Equipment (`taneq`)
- Command: `taneq <item>`
- Cost: 150 practice points
- Creates class-exclusive gear (vnums 33200–33212): claymore, ring, bracer, collar, plate, helmet, boots, gauntlets, sleeves, leggings, cloak, belt, visor
- Non-Tanarri are zapped if they try to equip

---

## Passive Combat Summary (All Stacking)

When fully built out (rank 6, all powers), a Tanarri has:
- `+rank` base attacks + `+3` from SPEED = up to +9 extra attacks
- ×1.5 damage (MIGHT) + magma ×1.5 (LAVA) on lava strikes
- +500 max damage (MIGHT) + rank×375 (up to +2250) scaling
- Auto fire hits (FIERY) + auto fang hits (FANGS, doubled with HEAD)
- 80% physical damage reduction (EXOSKELETON)
- +250 hit/dam when FURY is on
- Opponent parry/dodge reduced by cumulative modifiers from SPEED, HEAD, FURY

---

## Special Mechanics

- **Alignment**: `bloodsacrifice` forces alignment to −1000
- **Regen rooms**: vnums 93330–93339 grant accelerated regeneration
- **Speech flavor**: Tanarri "booms" messages
- **Channel**: TANTALK (`CHANNEL_TANTALK = 33554432`) — class-exclusive chat
- **Equipment zap**: Non-Tanarri wearing Tanarri gear gets zapped and drops it

---

## Bot AI Priority Notes

### Toggle Buffs (enable at combat start or login)
1. `truesight` — always on, no cost
2. `claws` — always on, no cost
3. `fury` — always on (if rank 4+), small overhead but large combat gain

### Active Combat Abilities (priority order in fight loop)
1. `booming` — single-target stun/damage; use early in fight to potentially stun
2. `lavablast` — high damage with burn, use when available
3. `infernal` — AoE fire, great for multi-mob pulls
4. `earthquake` — AoE physical, good vs non-flying groups
5. `tornado` — AoE lightning vs flying mobs only
6. `enrage` — chaos utility; low priority for PvE grinding

### Utility
- `chaossurge` — anti-PC ability, low priority in PvE bot
- `chaosgate` — movement/escape; not needed for standard grinding
- `enmity` — PvP only, skip for PvE bot

### Training Order (suggested)
Rank 1: claws → earthquake → truesight  
Rank 2: exoskeleton → fangs → tornado  
Rank 3: speed → might → chaosgate  
Rank 4: fiery → fury → head  
Rank 5: booming → flames → enrage  
Rank 6: tendrils → lava → enmity  
