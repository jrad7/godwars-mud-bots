# Combat

Everything that happens once `fighting` is set lives in `src/fight.c`. Combat
runs on the violence pulse (`PULSE_VIOLENCE`, ~3 seconds). On every pulse,
`violence_update()` walks the character list and, for each combatant, calls
`multi_hit()` against its current target.

## The round loop

`violence_update()` is the entry point. For each character with a `fighting`
target:

1. Decrement `fight_timer` and class rage counters.
2. If the combatants are two PCs, lock in PK fight timer — minimum 10 ticks,
   up to +3 per round up to 25. (This prevents insta-recall flight and makes
   `is_safe` return FALSE for the duration.)
3. Both combatants must be awake and in the same room. Otherwise the fight
   stops.
4. Call `multi_hit(ch, victim, TYPE_UNDEFINED)`.
5. Trigger autoassist: charmed mobs, mounts, groupmates, and same-type NPCs
   auto-attack the original target. NPC assist has a 12.5% chance regardless
   of type (`number_bits(3) == 0`).

`PLR_LLM` players get a compressed per-round summary instead of the normal
flood of hit lines: `[ROUND] You->target(hp%): dealt:X taken:Y HP:cur/max`.
See `g_llm_*` tracking in `violence_update()` and `dam_message()`.

## A single attack: one_hit

`one_hit()` resolves one swing. Hit/miss, damage, defenses.

### To-hit roll

1. Compute `thac0`: NPCs use 20→0, PCs use `SKILL_THAC0_00`→`SKILL_THAC0_32`
   interpolated over level, then subtract `char_hitroll(ch)`.
2. Compute `victim_ac = max(-100, char_ac(victim) / 10)`. Blind attacker
   takes -4. Superstance DAMAGE_1/2/3 adds 100/200/300 to `victim_ac`
   (makes target easier to hit).
3. `diceroll = number_bits(5)` reduced to `[0,19]`. Miss if roll is 0 or
   (not 19 and less than `thac0 - victim_ac`). Natural 19 always hits.

Miss calls `damage(ch, victim, 0, dt)` — still triggers weapon-skill
training and stance training.

### Damage roll (on hit)

Base dice depends on attacker:
- NPC: `number_range(level/2, level*3/2)`, +50% if wielding.
- PC with weapon: `dice(wield->value[1], wield->value[2])`.
- PC with razorclaws + wolf: `dice(25,35)`; vamp claws only: `dice(10,20)`.
- PC with monk adam hands: `dice(10,25)`.
- PC unarmed default: `dice(4,10)`.
- Ninja belts add flat dice (BELT_SEVEN +1d5 up to BELT_TEN +16d20).

Then, in order:
- `+ char_damroll(ch)`.
- `×2` if victim is asleep.
- Weapon-skill bonus: `dam + dam * (min(350, ch->wpn[dt-1000]+1) / 60)`.
  This is the single biggest non-class multiplier. A PC with a weapon skill
  capped near 350 roughly 6× their base damage.
- Victim resistance item: `dam *= 0.75`.
- PK divisor: `dam /= 2` if both PC (the "slow down pk" halving).
- NPC damage cap: NPCs over 2000 dam get halved past that threshold.
- Upgrade multiplier: +5% per `upgrade_level` (5 levels max, +25%).
- **Huge stack of class multipliers** — see the section below.
- Stance damage (attacker superstance) and stance resist (victim
  superstance) apply here.
- Class-vs-class shields (tanarri exoskeleton, lich flat /5, shapeshift
  form-level reductions, etc.) — see `cap_dam()`.
- Damage-type modifiers (gsn_backstab, gsn_circle by belt, heavenlyaura,
  etc.) applied last before the cap.

### Damage cap

`cap_dam()` enforces `ch->damcap[DAM_CAP]`, recomputed by `update_damcap()`
whenever fighting starts. Base cap is 1000 and grows with class, rank,
generation, items, and a small stance bonus. Base defensive caps subtract
from the attacker's cap. Builders get 30000. See `update_damcap()`.

After caps:
- `dam = dam + dam/rand(2,5) + rand(10,50)` for PC victims (variance).
- `×= rand(2,4) * rand(2,3) / rand(4,6)` extra PC variance.
- Ceiling: 1,000,000 absolute, then `rand(damcap-200, damcap+100)` if over
  `damcap[DAM_CAP]`. Sanctuary halves again.
- `randomize_damage()` scales by `(d100 + 50) / 100`, giving a final 0.5×
  to 1.5× spread.

### Defenses (in damage())

In order — first one that succeeds ends the swing:
1. NPC-only `disarm` and `trip` rolls (`level * 0.5%` each).
2. **Dodge** (`check_dodge`). Base skill = `victim->wpn[0] * 0.5 -
   ch->wpn[dt-1000] * 0.1`, capped [20,80] before class adds. Huge pile of
   class bonuses and rank bonuses pile on top. Monkey stance countering and
   viper/mantis/tiger/wolf `can_bypass` disable stance-based dodge bonuses.
3. Superstance dodge if `STANCEPOWER_DODGE` and stance >100.
4. **Parry** (`check_parry`). Only fires for standard melee `dt` in
   [1000,1012], or when attacker is a monk. Requires a wielded weapon,
   werewolf bear-claws, monk adamantium, or shapeshifter form to count.
   Similar chance math as dodge.
5. Superstance parry if `STANCEPOWER_PARRY` and stance >100.

After defenses, `dam_message()` prints the hit and `hurt_person()` applies
damage, sets position, handles death and cloaks/absorbs.

## is_safe: when combat is blocked

`is_safe()` returns TRUE (and often prints why) under any of:
- Either side's `in_room` is NULL.
- Both PCs but either lacks `CAN_PK`.
- Either PC has a post-train `safe_counter > 0` (10 ticks post avatar).
- Victim is writing a note.
- Either side is ethereal or in the shadowplane.
- Either side is headless or polymorphed into an object.
- Victim is AFK.
- Victim is linkdead with `timer > 1` and no fight timer — prevents
  exploiting ld in the first round of a fight.
- Ragnarok: upgraded cannot attack non-upgraded and vice-versa.
- Room is `ROOM_SAFE` (not during ragnarok).
- Either side has `AFF_PEACE` or `ITEMA_PEACE`.

Exception: if `victim->fight_timer > 0`, the fight is already in progress
and `is_safe` returns FALSE regardless of the above (some of them).

## Multi-attacks: number_attacks

After the first guaranteed swing, `multi_hit()` calls `one_hit()` again
`number_attacks(ch, victim)` times. Base is 1. Buffs stack additively:

- NPC attacker: +1 at levels 50/100/500/1000/1500, +2 at 2000, plus
  `min(20, hitsizedice)` base. High-level mobs can swing a lot.
- Stance speed procs: viper/mantis/tiger +1 on a percent roll (stance%*0.5),
  wolf +2, superstance `STANCEPOWER_SPEED` +2.
- Upgrade level: +1 per upgrade tier.
- Vampire `DISC_VAMP_CELE` adds in 0.5 increments at tier 1/6/8/10.
- Tanarri +rank. Droid +`CYBORG_LIMBS`. Angel +`ANGEL_JUSTICE`.
  Werewolf +2 for lynx>2, up to +5 from boar>6 via move pool.
- Undead-knight +`WEAPONSKILL/2`, lich flat +5. Shapeshifter +2 base plus
  form-specific.
- Demon `DEM_SPEED` +2, `WARP_QUICKNESS` +3, `DISC_DAEM_ATTA`/2.
- Drow `SPC_DROW_WAR` +2, `DPOWER_SPEED` +3.
- Mage `ITEMA_BEAST` +4. Samurai flat +5. `ITEMA_SPEED` +2.
- Ninja by belt: +1 (belts 1-2), +2 (3-5), +3 (6-7), +4 (8-9), +5 (belt 10),
  plus `NPOWER_CHIKYU>=3` +3.

Subtracted by target's defensive speed:
- Victim `DISC_VAMP_CELE>=3` → -CELE/3. `DISC_WERE_MANT>=3` → -MANT/3.
  `ITEMA_AFFMANTIS` flat -1. Demon `DISC_DAEM_ATTA` → -ATTA/2.
- 25% chance of -1 each round (the "unlucky" roll).

Floor at 1. A spec'd ninja or samurai easily hits double digits per round.

## Hand slots and weapon choice

Dystopia supports four wield slots: `WEAR_WIELD`, `WEAR_HOLD`, `WEAR_THIRD`,
`WEAR_FOURTH`. `THIRD_HAND`/`FOURTH_HAND` bits gate the extra slots. Inside
`multi_hit()`, each round rolls a random `wieldtype` pattern (bitmask
1/2/4/8) and that picks which weapon the hand swings with. With four
weapons, the round still swings one primary choice, then secondary rounds
pick again. Right-handed/left-handed player flags bias the first unarmed
swing (50/50 default, fully locked with `PLR_RIGHTHAND`/`PLR_LEFTHAND`).

## Bonus procs (after the main swings)

`multi_hit()` fires a long cascade of bonus one-shots after the normal
swings, if the victim is still fighting:

- Vampire `VAM_FANGS` — 2 bonus bites (4 vs NPCs).
- Demon warps — `WARP_SPIKETAIL`, `WARP_SHARDS`, `WARP_MAGMA`,
  `WARP_VENOMTONG`, `WARP_WINGS` each proc at 1-in-3.
- Shapeshifter form-specific procs (hydra fangs, tiger claws/fangs, bull
  headbutt/hooves, faerie fireball/buffet).
- Vampire `VAM_HORNS/TAIL/HEAD/WINGS` — headbutt/sweep/fangs/buffet procs.
- Werewolf `NEW_QUILLS`, `NEW_SLAM` (cheapshot stun), `DISC_WERE_RAPT`
  rfangs.
- Samurai `NEW_BLADESPIN` — random small chance for kick/knee/slash procs.
- Drow `NEW_DARKTENDRILS` 4-hit proc.
- Mage `NEW_MULTIARMS` + `ITEMA_MAGESHIELD` — 5-hit mage-shield flurry.
- Monk `BODY>=4` or Angel `ANGEL_AURA` — heavenlyaura multi-hit.
- Angel `ANGEL_HALO` — curse/web/heal/fireball/godbless on 1-in-5.
- Droid `NEW_CUBEFORM` — stuntubes + stinger multi-hit.
- Tanarri `TANARRI_FANGS`/`TANARRI_FIERY` procs.
- Undead-knight `WEAPONSKILL>9` — 3 lightningslash procs.
- Lich discipline procs — fireball/chillhand/deathaura when respective
  lore >4.
- Ninja `NPOWER_NINGENNO>=5` — shiroken multi-hit (50/50 per round).
- PK power proc: if `get_ratio(ch) > 2499`, 1-in-10 chance of a supreme hit.

Any `ITEMA_SHOCKSHIELD`/`FIRESHIELD`/`ICESHIELD`/`ACIDSHIELD`/`CHAOSSHIELD`
on the victim retaliates with `lightning bolt`/`fireball`/`chill touch`/
`acid blast`/`chaos blast` per swing that lands.

Incidental poison applications: vampire `VAM_SERPENTIS`, werewolf
`DISC_WERE_SPID`, drow `DPOWER_DROWPOISON`, ninja `NPOWER_NINGENNO>=5` each
call `spell_poison` scaled off level.

## Angel eye-for-an-eye

When a PC Angel with `ANGEL_EYE` is hit for >100, `angel_eye()` reflects
`dam/4..dam/5` (capped 275–325) straight back at the attacker. This fires
inside `hurt_person()`, after normal damage has been applied. The angel's
hit pool is unchanged; the reflection is its own `hurt_person()` call.

## Death, corpses, and body parts

`hurt_person()` sets position via `update_pos()`:
- hp ≤ -10 (PC, hero): holds at -10, stops fighting (hero death freeze).
- hp < -9 (PC, non-hero): auto-kill via `do_killperson`, death counter +1.
- hp < -6 (NPC): `POS_DEAD`.
- hp ≤ -6: `POS_MORTAL`. ≤ -3: `POS_INCAP`. Else: `POS_STUNNED`.

`raw_kill()` handles the actual kill:
- NPC: `make_corpse()`, extract. Increments `killed` on the prototype and
  in `kill_table`.
- PC: corpse + strip affects + wipe location HP + restore to
  `POS_RESTING` with hp/mana/move floored at 1. Saves the pfile. Clears
  `DEMON_POWER`, `itemaffect`, hitroll/damroll/saving_throw. Runs
  `do_call(victim, "all")` so all owned items come back.
- Bots: if they have no gear post-death, auto-issue a newbie pack, wear
  all, transition to `BOT_RESTING`. Bots never die naked.

`make_corpse()` timers: NPC 4-8 ticks, PC 25-40.

`make_part()` drops body parts (head, arm, leg, heart, brain, eyeball,
etc.) with 4-7 tick timers. A PC's own head is special: it becomes a
`chobj` linked to the player, letting them act through it while headless.

`behead()` is the non-fatal "lose your head" state: stops fighting, makes
corpse, extracts, re-drops in the room, creates the head object, wipes
affects, locks the player at 1 hp and `POS_STANDING`, sets `LOST_HEAD`, and
polymorphs them into their severed head.

## Decapitation (player-vs-player)

`do_decapitate` is how one PC actually keeps the other's head. Rules:
- Must be in the target's room and target `position <= 1` (mortal/incap).
- Cannot decap in `ROOM_ARENA`.
- `reachedDecapLimit(ch)` blocks once a per-size threshold is hit —
  players must gain more max_hit before decapping again.
- Cannot decap the same person twice in a row ("spamcap"). `last_decap[0]`
  remembers the previous victim.
- `fair_fight(ch, victim)` gates the fair case. Unfair decap goes down the
  "paradox counter" path: you still behead them but the attacker gains a
  paradox counter tick, and after 3 ticks `do_paradox(ch,"self")` fires
  (paradox is bad — check the skill source for consequences).

On a fair decap:
- Attacker gets half the victim's experience.
- +500 QP, random 1000-2000 bounty credit; victim's existing bounty is
  paid out via `award_pkill_bounty`.
- Attacker's `fight_timer += 10` (post-decap PK cooldown).
- If attacker is a demon: +1 soul.
- Victim is beheaded, level reset to 2, mage item-affects stripped,
  `rage = 0` (non-ninjas), `pkill`/`pdeath` stats updated.
- `decap_message()` picks the flavor line by the attacker's weapon type:
  weapon value[3] 1=slash, 2=stab, 3=slash, 4=strangle/whip, 5=claw,
  6=blast, 7=pound, 8=crush, 9=grep, 10=bite, 11=pierce, 12=suck. Unarmed
  is "head torn off".
- Both sides save. Kingdom kill/death counters update.

`fair_fight()`:
- Both PCs must be level 3 (avatar).
- Both must be 17+ hours old (newbies protected), except bots are exempt.
- Both must have `getMight()` >= 150 (no squashing baby toons).
- Attacker must not have reached decap limit.
- Range: if aggressor has <1000 might, defender ≥ 80% of aggressor. If
  aggressor ≥ 1000, defender ≥ 75%.

Ragnarok has its own path: `ragnarokdecap()` teleports the victim to
`ROOM_VNUM_ALTAR`, clears their stats, resets them to level 2, re-trains
them to avatar, and restores them.

## Weapon-skill training

Every successful `one_hit()` (hit OR miss, as long as `!is_safe`) calls
`improve_wpn()` and `improve_stance()`. That is the only way to raise
`wpn[]`. Standing in a safe room means you train nothing. `WPN_TRAIN_TICKS`
is currently 1.

## XP

`group_gain()` → `xp_compute()` is where kill XP is minted.

`xp_compute()` base curve:
- Victim level ≤ 50: `xp = 300 - clamp(-5, 3 - lvl*5/3, 6) * 50` (caps at
  550 for L5+).
- Victim level > 50: `xp = level * 11` (so 550 at L50, 1100 at L100, 4400
  at L400 — tracks mob HP growth).

Then:
- `xp -= xp * rand(-2,2) / 8` (popularity jitter).
- Fuzz to `rand(xp*3/4, xp*5/4)`.
- `xp *= victim->level * 0.60`.
- `xp *= 1 + XP_KILL_BONUS`.
- Scoreboard counters update.
- If xp > 499 and the player is researching a discipline, they earn
  discipline points (2 per threshold, more at lvl >200/>400), and `xp -= 500`.
- Final `xp` is multiplied by `group_gain`'s modifiers:
  - +25% if alignment conflict (good vs evil).
  - -25% if matched alignments.
  - +25% for MCCP users (compressed connection).
  - +200% for newbies (≤4h of game age).
  - +100% during happy hour (`global_exp`).
  - +`min(100, pkscore/50)` for positive PK ratio.
  - -5% for kingdom tithe.
  - ×2 doubled then `/ (0.75 * members)` if grouped.
- `grind_zone_table` multiplier applies: 100=1x, 200=2x, 700=7x, 1400=14x.
  See `grind_areas.md`.
- Minimum floor: if `0 < xp < 4000`, `xp = rand(3000,5000)`.
- Cap: 1,500,000,000 per kill.
- `ACT_NOEXP` mobs and specific VNUMs in the blacklist (kingdom healers,
  altar mobs) always return 0.

Demon/Drow/Droid/Tanarri also get class points (`DEMON_CURRENT`/`TOTAL` or
`DROW_POWER`) equal to `victim->level * 4` (or *8 for droid/tanarri),
scaled by the grind multiplier.

## Other PC-facing commands in fight.c

- `do_kill` — initiate combat. Werewolf boar power + position-standing
  target triggers a charge-stun. Angel always swoops. Both PCs get +3 to
  `fight_timer` at engage.
- `do_backstab` — requires a piercing weapon (value[3]==11), target not
  fighting, target at full HP. Damage multiplies by `rand(2,4)` (drow
  adds flat 100-1000 then ×rand(7,10); ninja `NINGENNO>=2` ×rand(50,60)).
  High-belt ninjas get bonus re-stabs.
- `do_flee` — 4-tick wait. Fails if webbed, blind, tied, 0 move, tanarri
  fury, vampire coil, tanarri tendrils, mage illusions, monk/werewolf
  jawlock, UK bog aura. 6 random-direction attempts. Success prints "You
  flee from combat! Coward!" and stops the fight.
- `do_rescue` — pulls you onto your groupmate's attacker. Skill check.
- `do_kick` — bonus damage vs werewolves if wearing silver boots. Kick
  damage capped at 750 in PK. Stance damage bonus applies.
- `do_punch` — high-level monk stun punch. Requires target at full HP and
  standing. Damage capped at 1000. On success, target goes `POS_STUNNED`
  and a random jaw/nose shatter applies. Werewolf boar targets break the
  attacker's fingers instead.
- `do_hurl` (KaVir) — throw target through a doorway. Passes through
  closed/locked doors (forces them open), prismatic walls stop you.
  Damage scales with attacker level.
- `do_berserk` — 5 random NPCs in the room get multi-hit.
- `do_disarm` — active disarm command (NPCs also roll passive disarm in
  `damage()`). Target must not be `IMM_DISARM`.
- `do_circle` — ninja-only, `NPOWER_NINGENNO>=6`, piercing weapon, 1 hit
  plus 25% chance of a second. 8-tick wait.
- `do_autostance` — set which stance to auto-drop into when combat
  starts. Gated by prerequisite stance levels (mantis needs crane+viper
  at 200, wolf needs the whole tree, etc.).

## Classes at a glance (combat flavor)

Class-by-class multipliers in `one_hit`/`cap_dam`/`check_dodge`/`check_parry`
are dense. Rough combat identity:

- **Vampire** — generations and disciplines (`DISC_VAMP_*`). Celerity adds
  attacks and parry/dodge. Potence adds damage. Fortitude cuts incoming
  damage. Trueblood is top rank.
- **Werewolf** — discipline tree (`DISC_WERE_*`). Bear boosts damage and
  HP, mantis boosts dodge/parry, boar adds attacks and counter-stuns,
  lynx adds attacks, rapture adds bonus fangs. Silver damage threshold
  from `siltol`.
- **Demon** — demon powers (DEM_*), warps (`WARP_*`), discipline points
  (`DISC_DAEM_*`). Very high `number_attacks` ceiling once ATTA is maxed.
  In rooms `93420-93426` demon damcap doubles and victim demon damcap
  halves — that's the demon home plane.
- **Drow** — `SPC_DROW_WAR`, `DPOWER_*`. Tough skin, drow speed, drow
  poison, dark tendrils 4-hit, dark fight-dance for parry-ignore.
- **Droid** — cyborg limbs stack attack/damage/dodge. `CYBORG_BODY` cuts
  incoming.
- **Tanarri** — rank-based. Exoskeleton, might (+50% damage), fangs, fury,
  tendrils (anti-flee), speed.
- **Angel** — `ANGEL_JUSTICE/PEACE/HARMONY/LOVE` boost hit/parry/dam/cap.
  Halo and aura trigger random spells mid-round. Eye reflects damage.
- **Samurai** — flat +5 attacks baked in, -25 to dodge/parry for enemies,
  1.75× damage reduction. Wpn-skill>1000 feeds into damcap.
- **Monk** — stance-focused. `monkab[COMBAT]` adds attacks. Chi adds
  damage (up to 3×) and resistance. `monkblock` buffs dodge/parry.
  Adamantium hands can parry unarmed. Flame covers punches.
- **Ninja** — belt-ranked. Each belt adds damage multiplier and flat
  attacks. Chikyu levels lock in major PvP buffs. High belts get bonus
  backstab re-rolls and parry/dodge.
- **Undead-knight** — `WEAPONSKILL` scales attacks, damage, damcap,
  parry. Auras (death/fear/bog) trigger in combat. Spirit reduces damage
  taken.
- **Lich** — flat /5 incoming damage, +5 attacks, discipline procs
  (CON_LORE, NECROMANTIC, DEATH_LORE, CHAOS, LIFE) each add damcap. Soft
  -15 to dodge/parry because liches tank, not duck.
- **Mage** — `ITEMA_BEAST` is the big buff (+4 attacks, -30 parry for
  enemies, dodge -10, +750 damcap). `ITEMA_STEELSHIELD`, `ITEMA_DEFLECTOR`
  layer defense. Multi-arms → mageshield 5-flurry.
- **Shapeshifter** — form decides combat (HYDRA/TIGER/BULL/FAERIE each
  with its own *_LEVEL). Flat ×1.4 damage with form-specific extra on top.
  Faerie phases through some hits (dam=0 on lucky roll). ×2.5 damage
  reduction baseline plus form stacking.

## Stances

Stances live in `ch->stance[0]` (current stance index) and
`ch->stance[N]` (level in that stance). The basic stances get dedicated
indices: `STANCE_VIPER`, `STANCE_MANTIS`, `STANCE_TIGER`, `STANCE_WOLF`,
`STANCE_CRANE`, `STANCE_MANTIS`, `STANCE_MONKEY`, `STANCE_CRAB`,
`STANCE_SWALLOW`, `STANCE_DRAGON`, `STANCE_BULL`, `STANCE_MONGOOSE`.
Each stance trains up to 200 (mastery).

Baseline stance effects:
- VIPER/MANTIS/TIGER — +1 attack on stance%*0.5% proc.
- WOLF — +2 attacks on proc.
- BULL/DRAGON/WOLF/TIGER — flat `max_dam` bonus (200-250).
- CRANE/MANTIS — parry chance += stance*0.25.
- MONGOOSE/SWALLOW — dodge chance += stance*0.25.
- MONKEY — counter/bypass disables victim stance defenses.
- CRAB/DRAGON/SWALLOW — reduce attacker's damcap (-250 each).
- Dam bonus (`dambonus()`): at stance >100, dam scales up (BULL/DRAGON/
  WOLF/TIGER) or down (CRAB/DRAGON/SWALLOW) by stance/100.

Superstances (indices SS1-SS5, position `stance[0] > 12`) are custom
stances whose powers are bitflags in `stance[stance+6]`:
`STANCEPOWER_SPEED`, `STANCEPOWER_DODGE`, `STANCEPOWER_PARRY`,
`STANCEPOWER_BYPASS`, `STANCEPOWER_DAMAGE_1/2/3`, `STANCEPOWER_RESIST_1/2/3`,
`STANCEPOWER_DAMCAP_1/2/3`, `STANCEPOWER_REV_DAMCAP_1/2/3`. Active only
when the superstance's level exceeds 100. At 200 mastery,
`multi_hit()` can trigger `special_move()` once per round at 5% roll.

`autostance` sets `stance[MONK_AUTODROP]` so you automatically drop into
your stance on `set_fighting()`. Prerequisites: mantis needs crane and
viper at 200; monkey needs crane and mongoose at 200; swallow needs crab
and mongoose; tiger needs bull and viper; dragon needs crab and bull;
wolf (werewolf-only) needs tiger/swallow/monkey/mantis/dragon all at 200.

## Stances of note for PvP targeting

A PC at `stance[0] == STANCE_MONKEY` counters stance-based defenses — so
attacking a monkey PC, your `can_counter(victim)` is true and their
viper/mantis/tiger/wolf/superstance attack bonuses are disabled. A PC in
viper/mantis/tiger/wolf/superstance-bypass sets `can_bypass(ch, victim)`
TRUE, which disables the victim's stance-based defensive bonuses (parry,
dodge). So the rock-paper-scissors in PvP is: monkey counters attack
speed; viper/mantis/tiger/wolf/superstance bypass defense.

## Source pointers

- Round driver: `violence_update`, `multi_hit`, `number_attacks` —
  `src/fight.c:86-1220`.
- Hit resolution and damage: `one_hit`, `randomize_damage`, `cap_dam`,
  `damage` — `src/fight.c:1226-2122`.
- Safety gates: `is_safe` — `src/fight.c:2325-2443`.
- Defenses: `check_parry`, `check_dodge` — `src/fight.c:2448-2909`.
- Positioning and death: `update_pos`, `set_fighting`, `stop_fighting`,
  `make_corpse`, `make_part`, `raw_kill`, `behead` — `src/fight.c:2914-3339`.
- XP and rewards: `group_gain`, `xp_compute` — `src/fight.c:3341-3552`.
- Flavor messages: `dam_message` — `src/fight.c:3554` onward.
- Command handlers: `do_kill`, `do_backstab`, `do_flee`, `do_rescue`,
  `do_kick`, `do_punch`, `do_berserk`, `do_hurl`, `do_disarm`,
  `do_decapitate`, `do_circle`, `do_autostance`, `do_killperson` —
  `src/fight.c:4153-5832`.
- Fair-fight gate: `fair_fight` — `src/fight.c:5841-5881`.

If this doc drifts from the code, treat `src/fight.c` as authoritative and
re-derive.
