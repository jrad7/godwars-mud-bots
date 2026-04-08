# Spider Droid (Cyborg) Class Research

## Overview
The Spider Droid (or Cyborg) is a mechanical/drow hybrid class (`CLASS_DROID`). They use Drider Power (`DROID_POWER` / class points) to upgrade their implants (Face, Legs, and Body). They also use primal points (practices) to forge class-specific drider equipment.

## Powers & Progression
Upgrading implants is done via `implant <face|legs|body> improve`.
The cost is based on the current level:
- Level 1: 25,000 power
- Level 2: 50,000 power
- Level 3: 100,000 power
- Level 4: 200,000 power
- Level 5: 400,000 power
- Level 6 (Body only): 800,000 power

**Prerequisites:**
- Body implants require at least 1 Leg implant.
- Face implant level 4 requires at least Body implant level 4.

**Implants:**
- **Face (Max 5):** Grants enhanced vision. Level 1+ allows `infravision`. Level 4+ gives bio-mechanical eyes, shields aura, and improves senses to see stats/locations.
- **Legs (Max 5):** Enhances speed and fighting skills. Level 5 grants poison spit capabilities. (Having both Legs 5 + Body 5 enables `stuntubes`).
- **Body (Max 6):** Enhances armor and combat abilities.
  - Level 1: Increased armor.
  - Level 2: Absorb certain attacks.
  - Level 3: Body slam on first attack.
  - Level 4: Cloaking / invsibility.
  - Level 5: Poisoned stingers. Enables `cubeform` and `stuntubes` (with Legs 5).
  - Level 6: Fast regeneration.

## Active Abilities
- **Infravision (`infravision`):** Toggles unholy sight (requires Face 1).
- **Stuntubes (`stuntubes`):** Combat ability. Requires Legs 5 and Body 5. Costs 1000 move. Hits the enemy 3 times and applies Poison and Flaming to the victim.
- **Cubeform (`cubeform`):** Transformation ability. Requires Body 5. Costs 2000 move and 2000 mana. Transforms the user into an Avatar of Lloth, granting +250 hitroll and +250 damroll permanently until toggled off. 
- **Dridereq (`dridereq <piece>`):** Spends 150 primal (practice) to forge class equipment. Valid pieces: whip, ring, collar, armor, helmet, leggings, boots, gloves, sleeves, cloak, belt, bracer, mask.

## Bot Integration Needs
1. **Gear (`bot_gear.c`):** Spider Droids need a table to load their equipment using `dridereq <piece>`. 
2. **AI (`bot_ai_droid.c`):**
   - **Training:** Priorities for spending DROID_POWER on implants.
   - **Buffs:** Need to toggle `infravision` and activate `cubeform` (Avatar of Lloth) once Body 5 is reached.
   - **Combat:** Fire off `stuntubes` when in combat and move >= 1000.
3. **Manager (`bot_mgr.c`):** Add `BOT_CLASS_DROID = 11`, increment count to 12, add a naming convention for the droids.
