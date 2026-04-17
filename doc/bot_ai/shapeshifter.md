# Shapeshifter Class Mechanics

The Shapeshifter class in Dystopia MUD relies on the use of primal forces and various mutable forms that define their offensive and defensive capabilities.

## Combat Forms
Shapeshifters must shift into specialized forms during combat:
- **Tiger** (`shift tiger`): Focuses heavily on high physical damage at the cost of armor.
  - Grants the ability to `phase` (avoid damage).
  - Can use `shaperoar` to force opponents to flee.
  - *Bot Behavior:* Used as a fallback grinding form before Hydra is trained. Phase used defensively when taking heavy damage.
- **Hydra** (`shift hydra`): Best sustained DPS form with a powerful breath weapon.
  - Can use `breath` attack on opponents. The frequency and damage escalate as Hydra form is leveled up.
  - *Bot Behavior:* Primary grinding form. Highest damroll/hitroll (+450/+450), 1.6x damage multiplier, 5x passive fang hits at max level. Breath used at 70% rate as the main active ability.
- **Bull** (`shift bull`): Brutal physical form excelling in stuns and damage spikes.
  - Can use `charge` to stun an opponent and deal immediate damage.
  - Can use `stomp` to heavily damage and sever limbs from downed or overwhelmed opponents.
  - *Bot Behavior:* Used as the primary form for PvP combat to disable opponents. Mid-combat switch to Faerie if HP drops below 30%.
- **Faerie** (`shift faerie`): Defensive/evasive magically-attuned form.
  - Highly resistant to being hit due to massive evasion and speed.
  - Can use `faeriecurse` to indiscriminately web and curse targets.
  - Can use `faerieblink` to deal massive magical damage by shifting through limbo and striking from behind.
  - *Bot Behavior:* Used in PvP when low HP for evasion, and during PvP flee state. Faerieblink and faeriecurse used as active abilities.

## Progression and Training
A Shapeshifter uses the `formlearn` command with Primal (instead of the standard practice system for some classes) to level up their core capabilities.
- Max level for any individual form or path is 5.
- Cost scales with the level of the form: `80 * level + 80` primal.
- The `shiftpowers` track offers utility and passive benefits across all forms.

## Class Gear
Shapeshifter gear is not inherently found in the world, but is crafted through the `shapearmor` command using primal energy (150 primal per piece).
### Slots and Items
- `shapearmor knife` - WEAR_WIELD
- `shapearmor kane` - WEAR_HOLD
- `shapearmor ring` - WEAR_FINGER (Both)
- `shapearmor necklace` - WEAR_NECK (Both)
- `shapearmor jacket` - WEAR_BODY
- `shapearmor helmet` - WEAR_HEAD
- `shapearmor pants` - WEAR_LEGS
- `shapearmor boots` - WEAR_FEET
- `shapearmor gloves` - WEAR_HANDS
- `shapearmor shirt` - WEAR_ARMS
- `shapearmor cloak` - WEAR_ABOUT
- `shapearmor belt` - WEAR_WAIST
- `shapearmor bands` - WEAR_WRIST (Both)
- `shapearmor visor` - WEAR_FACE

## Utility Abilities
- `do_mistwalk`: 250 move cost to reform into mist and move directly to a target room.
- `do_hatform`: Morph into a tall hat (hide from others).
- `do_camouflage`: Customize item appearances and names.
- `do_shapeshift`: Disguise oneself as any other specified creature name.
*(Note: Bots generally skip these utilities.)*
