# Lich Class Overview

The Lich is a powerful undead spellcasting class that relies on massive mana reserves to power their abilities. They utilize two paths of power: **Undead Lore** (Conjuring, Death, Life) and **Necromancy** (Necromantic, Chaos Magic). These disciplines are increased using experience points via the `studylore` command (up to level 5 each, at a cost of 10 million exp per level).

## Key Abilities

- **Damage / Status**: 
  - `chillhand`: Deals damage and debuffs the target's strength using chill touch.
  - `painwreck`: Channels unholy energies causing pure agony, damage, and a chance to stun the victim.
- **Area of Effect (AoE)**: 
  - `creepingdoom`: Swarms the room with insects, dealing damage based on target HP and draining half the Lich's mana.
  - `planarstorm`: Deals heavy planeshift damage to all non-safe targets in the room.
- **Utility / Defensive**: 
  - `planartravel`: Opens a 2-way portal to summon or travel to the target.
  - `chaosshield`: Shields the Lich's aura from those around them.
  - `earthswallow`: Swallows the victim into the earth, ejecting them at an altar.
  - `planeshift`: Phases the Lich into the spirit plane (ethereal).
  - `summongolem`: Summons elemental golems (fire, clay, stone, iron) depending on Conjuring Lore level.
- **Resource Management**: 
  - `polarity`: Drains the target's mana and uses it to restore the Lich's hitpoints.
  - `powertransfer`: Transfers 5,000 mana directly into a massive burst of hitpoints (7,500 - 10,000 heal).
  - `soulsuck`: Sucks the target's soul, dealing damage while simultaneously healing the Lich.
- **Gear Acquisition**: 
  - `licharmor <piece>`: Instantly generates a customized artifact piece for the Lich at the cost of 150 primal points per piece.

## Bot AI Design Strategy

The **bot_ai_lich** logic will follow the existing progression and combat loops observed in other advanced classes, tightly integrating the abilities listed above.

### State: Training & Progression
The bot will constantly monitor its `exp` pool. When exceeding 10 million `exp`, it will trigger `studylore` to level through its disciplines in sequence (e.g., maximizing Death and Life Lore early for damage and survivability, before pushing into Conjuring and Chaos Magic).

### State: Gear
Rather than looting or buying standard gear, the bot will use its `practice` points (primal) to issue `licharmor <piece>` commands, cycling through the entire 13-piece set upon reaching the 150 primal threshold for each item. 

### State: Combat
The bot will balance casting aggressive spells and managing its health/mana pool:
- When mana is plentiful but health is dropping, it will cast `powertransfer` for an instant self-heal.
- When fighting multiple enemies, it will trigger `planarstorm` or `creepingdoom`.
- Single targets will be debuffed with `chillhand` and stunned with `painwreck`.
- To maintain momentum without resting, it will use `soulsuck` to drain health from victims, and `polarity` to steal mana, keeping its resource loops fueled perpetually.
