# Undead Knight Class Reference (Bot AI)

## Overview

Undead Knight is a prestige evolution class from Vampire (`CLASS_UNDEAD_KNIGHT = 4096`). It is
themed around death magic and melee mastery — a heavy melee damage dealer with passive damage
reduction, retaliatory auras, and powerword burst spells.

**Key files:**
- `src/undead_knight.c` — all UK-specific command implementations
- `src/undead_knight.h` — power array constants and aura bit constants
- `src/fight.c` — combat integration (extra attacks, damage multipliers, defense, flee prevention)
- `src/update.c` — `update_knight()` passive regen and powerword cooldown tick
- `src/kav_fight.c` — weapon skill rating (`max_skl` formula)
- `src/jobo_util.c` — MIGHT_AURA strip on death
- `src/lich.c` — `do_soulsuck` (shared with Lich)
- `src/upgrade.c` — Vampire→Undead Knight class evolution requirements
- `src/clan.c` — `do_truesight`, `do_command` (shared with Vampire)

---

## Rank (Generation) Titles

Undead Knights display generation-based titles. Generation 1 is the highest prestige.

| Generation | Title          |
|------------|----------------|
| 1          | Fallen Paladin |
| 2          | Undead Lord    |
| 3–5        | Undead Knight  |
| 6+         | Skeleton Knight|

---

## Power Array Layout (`ch->pcdata->powers[]`)

Undead Knights have **4 trainable tracks** plus 2 internal slots, stored in `ch->pcdata->powers[]`:

| Constant        | Index | Theme                         | Max Level |
|-----------------|-------|-------------------------------|-----------|
| `NECROMANCY`    | 1     | Auras, command, cloak         | 10 (0–10)  |
| `INVOCATION`    | 2     | Powerword offensive spells    | 5 (0–5)    |
| `UNDEAD_SPIRIT` | 3     | Passive damage reduction      | 10 (0–10)  |
| `WEAPONSKILL`   | 4     | Extra attacks, damage, parry  | 10 (0–10)  |
| `AURAS`         | 5     | Bitmask of active auras       | bitfield  |
| `POWER_TICK`    | 6     | Powerword cooldown (counts down) | 0–4    |

**Training commands:**
- `gain necromancy|invocation|spirit` — advances Necromancy, Invocation, or Spirit
- `weaponpractice` — advances WeaponSkill (also sets HP/mana/move to 1 as a blood sacrifice)

**Training cost:** `(current_level * 60) + 60` **practice points (primal)** per level step.
(e.g., level 0→1 costs 60, level 1→2 costs 120, level 8→9 costs 600)

---

## Aura Toggle Buffs (`powers[AURAS]`)

Toggled with `aura [bog|death|fear|might]`. Stored as bits in `ch->pcdata->powers[AURAS]`.

| Constant     | Bit | Command      | Requirement        | Effect |
|--------------|-----|--------------|--------------------|--------|
| `BOG_AURA`   | 1   | `aura bog`   | NECROMANCY >= 6    | 70% chance to prevent fleeing opponents from escaping |
| `DEATH_AURA` | 2   | `aura death` | NECROMANCY >= 2    | Retaliates with `gsn_deathaura` hits against every attacker each round |
| `FEAR_AURA`  | 4   | `aura fear`  | NECROMANCY >= 9    | On being hit: applies −20 damroll / −20 hitroll affect (20 ticks) to attacker |
| `MIGHT_AURA` | 8   | `aura might` | NECROMANCY >= 4    | +300 damroll / +300 hitroll (applied directly, not via affect system) |

**Important:** `MIGHT_AURA` is stripped on death in `jobo_util.c`. The bot must re-enable it in
`buff_check` after each death and each login.

---

## Powerwords (`do_powerword`) — Invocation Track

Usable only during `POS_FIGHTING`. Available when `ch->pcdata->powers[POWER_TICK] == 0`.
After use, `POWER_TICK` is set to the cooldown value and decrements by 1 each game tick.

| Powerword         | Required INVOCATION | Effect                                                                   | POWER_TICK |
|-------------------|---------------------|--------------------------------------------------------------------------|------------|
| `powerword blind` | >= 1                | Strips PLR_HOLYLIGHT; applies AFF_BLIND (−4 hitroll, 60 ticks) to target | 3          |
| `powerword kill`  | >= 3                | Deals 10% current HP (cap 1500 vs PC / 5000 vs NPC); slays mobs < level 100 | 2       |
| `powerword flames`| >= 4                | AoE: fires `gsn_fireball` twice against every character in room          | 2          |
| `powerword stun`  | == 5 (max)          | Applies WAIT_STATE 24 to target (frozen for ~6 seconds)                  | 4          |

---

## Active Abilities

| Command         | Position     | Requirement         | Effect                                                              |
|-----------------|--------------|---------------------|---------------------------------------------------------------------|
| `unholysight`   | POS_STANDING | None (any UK)       | Toggles `PLR_HOLYLIGHT` (true-see, detect hidden/invis)             |
| `aura [x]`      | POS_STANDING | Varies by aura      | Toggle aura buffs (see table above)                                 |
| `bloodrite`     | POS_STANDING | None                | Heals 500–1000 HP; costs 500 mana                                   |
| `soulsuck`      | POS_FIGHTING | SPIRIT >= 4, align < 0 | Deals 250–1000 damage to a PC in room; heals UK for same amount  |
| `command`       | POS_STANDING | NECROMANCY >= 4     | Mind-control a mob into the bot's service                           |
| `ride`          | POS_STANDING | None                | Teleports to a target (NPC/IMMUNE_SUMMON only); costs 600 move      |
| `knightarmor`   | POS_STANDING | None                | Creates class armor piece; costs 150 primal per piece               |

---

## Combat Mechanics (fight.c)

### Extra Attacks (offensive)
- Base extra attack count: `+WEAPONSKILL / 2` (melee) or `+WEAPONSKILL / 3` (unarmed)
- At WEAPONSKILL >= 10 (maxed): 3 additional `gsn_lightningslash` hits per round

### Damage Multipliers (offensive)
- `WEAPONSKILL > 4`: `dam *= 1.2`
- `WEAPONSKILL > 8`: additionally `dam *= 1.3` (stacks — effectively ×1.56 at max)
- vs `CLASS_SHAPESHIFTER`: additional `dam *= 1.2`

### Max Damage Cap Increase
- `max_dam += WEAPONSKILL * 275` (up to +2750 at WEAPONSKILL 10)

### Damage Reduction (defensive)
- `dam *= (100 − UNDEAD_SPIRIT * 6) / 100` (up to 60% reduction at SPIRIT 10)
- vs CLASS_SHAPESHIFTER attackers: additional `dam *= 0.75`
- vs CLASS_SAMURAI attackers: additional `dam *= 0.70`

### Dodge / Parry / Flee
- Flee prevention: attacker's flee roll `−(WEAPONSKILL * 4)` (harder for enemies to flee UK)
- Dodge: UK's dodge improved by `WEAPONSKILL * 4`
- Parry (UK attacking): enemy parry chance `−(WEAPONSKILL * 3.5)`
- Parry (UK defending): UK's own parry chance `+(WEAPONSKILL * 4.5)`

### Weapon Skill Rating (kav_fight.c)
```
max_skl = UMAX(200, WEAPONSKILL * 50)
```
At skill 0–4: 200; skill 5: 250; skill 9: 450.

---

## Passive Bits / Flags

- **Cloak of Death** (`NEW_CLOAK`, bit 32768 in `ch->newbits`): Automatically granted when
  `NECROMANCY > 9` (i.e., maxed at level 9 meaning it checks > 9, so this is post-max — see
  source). Acts as a one-time death-save: when HP drops to 0, restores `min(max_hit * 10%, 4000)`
  HP and removes the bit. The bit is restored by `update_knight()` on the next tick.
- **Unholysight** (`PLR_HOLYLIGHT` in `ch->act`): Truesight toggle via `unholysight`, no level
  requirement for UK.

---

## Tick Update (`update_knight`)

Called each pulse for all undead knights:

1. Extra regen in home regen rooms (vnums **93300–93309**): `werewolf_regen(ch, 1)` bonus
2. Standard regen: `werewolf_regen(ch, 2)`
3. Decrement `POWER_TICK` by 1 (powerword cooldown)
4. `regen_limb(ch)` — regenerate any severed limbs

---

## Knight Armor (`do_knightarmor`)

Cost: **150 practice points per piece**. Vnums: **29975–29991** (class-locked).

| Piece       | VNUM  | Wear Slot    |
|-------------|-------|--------------|
| longsword   | 29976 | WEAR_WIELD   |
| shortsword  | 29977 | WEAR_HOLD    |
| ring        | 29978 | WEAR_FINGER  |
| bracer      | 29979 | WEAR_WRIST   |
| collar      | 29980 | WEAR_NECK    |
| helmet      | 29981 | WEAR_HEAD    |
| leggings    | 29982 | WEAR_LEGS    |
| boots       | 29983 | WEAR_FEET    |
| gauntlets   | 29984 | WEAR_HANDS   |
| chains      | 29985 | WEAR_ARMS    |
| cloak       | 29986 | WEAR_ABOUT   |
| belt        | 29987 | WEAR_WAIST   |
| visor       | 29988 | WEAR_FACE    |
| plate       | 29975 | WEAR_BODY    |
| (trophy)    | 29989 | —            |

> **Integration note:** Knight armor vnums (29975–29991) are **outside** the 33000–33299 range that
> `bot_gear.c:bot_is_class_gear_vnum()` currently checks. The cleanup logic in bot_gear.c needs to
> be updated to recognize this range.

---

## Communication

- `knighttalk` — Undead Knight-only global channel (`CHANNEL_KNIGHTTALK`)

---

## Origin

Undead Knights evolve from **Vampire** (generation 1) at the Temple Altar of Midgaard (vnum 3054).
The upgrade is irreversible. On upgrade, `clearshit()` resets: max HP/mana/move reset to 5000,
generation reset to 5, exp zeroed, all power arrays cleared.

---

## Bot AI Priority Notes

### Toggle Buffs (enable at login / after death)
1. `unholysight` — always on; no cost; enables true-see
2. `aura death` — always on (NECROMANCY >= 2); free retaliation hits against every attacker
3. `aura might` — always on (NECROMANCY >= 4); +300 damroll/hitroll; re-enable after every death
4. `aura bog` — always on (NECROMANCY >= 6); prevents enemies from fleeing (great for PvE grind)
5. `aura fear` — always on (NECROMANCY >= 9); debuffs attackers on hit

### Active Combat Abilities (POS_FIGHTING, priority order)
1. `powerword stun` — INVOCATION >= 5; POWER_TICK == 0; freeze target for ~6s; very high priority
2. `powerword kill` — INVOCATION >= 3; POWER_TICK == 0; 10% current HP + instant-kill low mobs
3. `powerword flames` — INVOCATION >= 4; POWER_TICK == 0; AoE room damage; lower priority (friendly fire risk in groups)
4. `powerword blind` — INVOCATION >= 1; POWER_TICK == 0; debuff only, lowest priority
5. `soulsuck` — SPIRIT >= 4, alignment < 0; damage + self-heal; use at all HP levels (free healing)

### Healing (POS_STANDING, between_fights)
- `bloodrite` — heals 500–1000 HP for 500 mana; use when HP < 80%

### Suggested Training Order
**WEAPONSKILL first** — most combat benefit (extra attacks, damage multipliers, max_dam):
- WeaponSkill 1 → 2 → 3 → 4 → 5 (damage multiplier unlock at 5)
- Necromancy 1 → 2 (death_aura)
- Necromancy 3 → 4 (might_aura — +300 hit/dam)
- WeaponSkill 6 → 7 → 8 → 9 (second multiplier unlock, max combat output)
- Spirit 1 → 2 → 3 → 4 → 5 (progressive damage reduction; soulsuck at 4)
- Necromancy 5 → 6 (bog_aura — flee prevention)
- Invocation 1 → 2 → 3 (powerword kill)
- Spirit 6 → 7 → 8 → 9 (max damage reduction: 54%)
- Invocation 4 → 5 (powerword stun — strongest powerword; max level)
- Necromancy 7 → 8 → 9 (fear_aura at 9)
- Necromancy 10 (cloak of death — one-time death save)
