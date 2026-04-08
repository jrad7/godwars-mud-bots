# Samurai Bot AI Research and Implementation Plan

## Class Overview

The Samurai (`CLASS_SAMURAI`) is a martial-arts and weapon-focused class in the Dystopia MUD. 

### Key Abilities
1. **Katana (`katana`)**: Samurai can create their own relic weapon, a katana, using 250 primal points. The sword is spawned at 100 condition and 100 toughness, with bonuses to hit and damage.
2. **Bladespin (`bladespin`)**: A combat buff toggle that enhances their combat techniques. Requires mastery (1000) in weapon skills 0, 1, and 3.
3. **Hologram Transfer (`hologramtransfer`)**: An out-of-combat teleportation ability that allows the Samurai to trace paths to their targets (costs 1000 movement points). Not relevant for combat loop but could be used in movement logic if desired.
4. **Focus (`focus`)**: The core mechanic of Samurai combat. Focusing slows down the battle and builds up `SAMURAI_FOCUS`. It requires spending focus points that scale based on usage. 
5. **Martial Combos (`martial`)**: Samurai learn four specific battle techniques that cost 150 million exp each.
   - `slide` (+1 Focus)
   - `sidestep` (+2 Focus)
   - `block` (+4 Focus)
   - `countermove` (+8 Focus)
   When used in combat, these techniques increase the Samurai's focus points (`ch->pcdata->powers[SAMURAI_FOCUS]`) and execute an immediate reactive strike (`gsn_lightningslash`) along with attempting a `check_samuraiattack` which triggers different special effects based on the current `SAMURAI_FOCUS` level.
   
### Combat Dynamics (check_samuraiattack)
When a Samurai successfully lands a combo move (Slide, Sidestep, Block, Countermove), the focus level is evaluated to trigger bonus effects. 
- **Focus 10**: Triple lightningslash attacks.
- **Focus 15**: Disarm attempt.
- **Focus 20**: Paralyze the victim (WAIT_STATE 24).
- **Focus 25**: Hurl the victim.
- **Focus 30**: Huge self-heal (2000-4000 hit points).
- **Focus 35**: Massive finishing combo (backfist, thrustkick, monksweep, jumpkick, lightningslash).

*Note: The samurai cannot use the same focus combo special twice in a row, checked via `SAMURAI_LAST`.*

## Proposed Strategy for Combat Action
- If `SAMURAI_FOCUS` >= 35, the Samurai bot will use `focus` to bleed focus points so it does not exceed 40 and remains capable of executing martial techniques.
- Combine `slide`, `sidestep`, `block`, and `countermove` based on how close they are to reaching the next multiple of 5 without exceeding 40.
- Ensure the state machine tries to hit different increments depending on current needs, or specifically avoids repeating the same increment that triggered `SAMURAI_LAST`.

## Open Questions for Clarification
1. For equipment, the `katana` command costs 250 primal to generate a sword. I am assuming I should add `katana` to the gear loading logic in `bot_gear.c`. What other standard slot gear should a Samurai load? Do they use generic gear (like Droid or Monk gear structure) for Head, Body, Legs, etc., or do they rely completely on the standard game shop gear outside of the Katana?
2. `do_bladespin` requires `wpn[0]`, `wpn[1]`, and `wpn[3]` to be >= 1000. Do bots automatically grind their weapon skills up just by fighting, or must we manually code a command to `practice` them, or write specific handling?
3. In `class.h` we have `CLASS_SAMURAI` as 16, and in `bot.h` `BOT_CLASS_COUNT` is currently 12. I'll add `BOT_CLASS_SAMURAI` as 12, incrementing count to 13.
4. For AI testing, I will update `BOT_TEST_CLASSES_COUNT` and array to test Samurai exclusively.

Please review this research plan and let me know the answers to the questions above.
