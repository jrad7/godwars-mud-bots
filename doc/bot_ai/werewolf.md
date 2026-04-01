# Werewolf Class — Bot AI Reference

## Overview

Werewolves use the same **research + train** progression as vampires and demons,
with 12 disciplines (`ch->power[DISC_WERE_*]`).  Called **Guardians** in-game
(`disciplines` shows "Guardians").  Class is selected at avatar via
`selfclass werewolf`.

Primary damage multipliers:

- **DISC_WERE_BEAR ≥ 5**: `dam *= 1.2` — flat 20% melee damage boost
- **Rage > 99**: `max_dam += ch->rage + 400` — scales with accumulated rage
- **DISC_WERE_BOAR 7+**: huge extra attacks based on `ch->move`
- **DISC_WERE_LYNX 2+**: +2 extra attacks per round
- **DISC_WERE_RAPT 1+**: automatic `rfangs` extra attack every round

Primary survival:

- **DISC_WERE_BOAR ≥ 3**: incoming `dam /= 2` (halves all incoming damage)
- **WOLF_COCOON** (Owl 8): second `dam /= 2` (combined with Boar 3 → 1/4 damage)

---

## Discipline System

Same mechanism as vampires and demons:

1. **Start research**: `research <discipline_name>`
   - Sets `pcdata->disc_research = disc_index`, `disc_points = 0`
2. **Accumulate points**: 1 point per kill (2–3 for high-level mobs)
   - Points needed: `(current_level + 1) * 10`
3. **Points done**: `disc_points` set to 999
4. **Train**: `train <discipline_name>` — increments `power[disc_index]`, resets fields
5. **Cancel**: `research cancel`

---

## All Werewolf Disciplines

| Constant       | Idx | Name          | Key Effects |
|----------------|-----|---------------|-------------|
| DISC\_WERE\_BEAR | 18 | bear          | 2+: break doors; 3+: disarm resistance (with claws); 5+: `dam *= 1.2`; 6: `roar`; 7: `skin`/`rend`; 8: `slam` auto shoulder slam |
| DISC\_WERE\_LYNX | 19 | lynx          | 1+: no sleep; 2+: +2 extra attacks (multi\_hit), +1 (do\_multi\_hit); 3+: dodge/parry bonus |
| DISC\_WERE\_BOAR | 20 | boar          | 2+: auto knockdown charge (1/3 chance, POS\_STANDING target); 3+: `dam /= 2` on all incoming; 4+: absorb backstab damage; 7: `rend`; 7+: extra attacks (`move/8000` or +5 if move>40k); 10: max |
| DISC\_WERE\_OWL  | 21 | owl           | 5: `staredown` (force flee); 6: `disquiet` (debuff, gnosis 1); 7: `reshape` (cosmetic); 8: `cocoon` (WOLF\_COCOON `dam /= 2`, gnosis 2) |
| DISC\_WERE\_SPID | 22 | spider        | 1+: passive poison on every hit; 2: `web` command (immobilize) |
| DISC\_WERE\_WOLF | 23 | wolf          | 2: `rage` (enter wolfman form); 3: `calm`; 4: `razorclaws` toggle (requires claws active) |
| DISC\_WERE\_HAWK | 24 | hawk          | 5: `quills` toggle (NEW\_QUILLS); 6: `burrow` (teleport to werewolf player); 7: `wither` (limb wither, gnosis 3) |
| DISC\_WERE\_MANT | 25 | mantis        | 3+: attacker disarm chance −`power*3`; 6+: parry/dodge bonus (scales with power) |
| DISC\_WERE\_RAPT | 26 | raptor        | 1+: auto `rfangs` extra attack every round; 3: `perception` toggle; 5: `devour` (eat corpse); 7: `shred` (requires shadowplane); 8: `jawlock` toggle; 10: `talons` burst |
| DISC\_WERE\_LUNA | 27 | luna          | 1: `flameclaws` toggle; 2: `moonarmour`; 3: `motherstouch`; 4: `gmotherstouch`; 5: `sclaws` toggle; 6: `moongate`; 8: `moonbeam` |
| DISC\_WERE\_PAIN | 28 | pain          | 10: `max_dam += 750`; 9+: extra two hits on quills proc |
| DISC\_WERE\_CONG | 29 | congregation  | Unknown synergy effects |

---

## Rage System

| Field / Flag | Meaning |
|---|---|
| `ch->rage` | Accumulated rage (no cap) |
| `ch->rage > 99` | `max_dam += ch->rage + 400` applied each fight round |
| `SPC_WOLFMAN` | Wolfman form; also applies rage bonus to hitroll/damroll |

**Building rage** (when not in wolfman form):

- `rage` command → `ch->rage += number_range(40,60)`; call twice to exceed 99
- `calm` command → `ch->rage -= number_range(60,90)`; drops back below 99

> The `max_dam` bonus (`rage > 99`) activates in `fight.c` without requiring
> `SPC_WOLFMAN`.  Wolfman form additionally grants the rage bonus to hitroll/damroll
> (`act_info.c` score).

---

## Passive Combat Effects (always-on once researched)

| Source | Effect |
|--------|--------|
| DISC\_WERE\_BEAR ≥ 5 | `dam *= 1.2` (all melee damage outgoing) |
| DISC\_WERE\_BOAR ≥ 2 | Auto knockdown charge 1/3 chance (POS\_STANDING target) |
| DISC\_WERE\_BOAR ≥ 3 | `dam /= 2` all incoming |
| DISC\_WERE\_BOAR ≥ 4 | Absorb / negate backstab damage |
| DISC\_WERE\_BOAR ≥ 7 | Extra attacks: `+move/8000` (or +5 if `move>40000`) |
| DISC\_WERE\_LYNX ≥ 2 | +2/+1 extra attacks per round |
| DISC\_WERE\_MANT ≥ 3 | Attacker disarm −`power*3`; parry/dodge bonuses |
| DISC\_WERE\_RAPT ≥ 1 | Auto `rfangs` attack every combat round |
| DISC\_WERE\_SPID ≥ 1 | Passive `spell_poison` on every hit |
| DISC\_WERE\_PAIN ≥ 10 | `max_dam += 750` flat |
| `ch->rage > 99` | `max_dam += ch->rage + 400` (fight.c) |
| NEW\_QUILLS + PAIN ≥ 10 | Quills proc fires two extra `one_hit` calls |
| WOLF\_COCOON | `dam /= 2` incoming (costs gnosis to activate) |
| SPC\_WOLFMAN | `incoming_backstab *= 0.8`; rage bonus to hitroll/damroll |

---

## Toggle / Passive Buff Commands

These should be kept active at all times by the bot.

| Command      | Requirement        | State Check | Notes |
|--------------|--------------------|-------------|-------|
| `flameclaws` | DISC\_WERE\_LUNA ≥ 1 | `IS_SET(ch->newbits, NEW_MONKFLAME)` | Flaming claws; fires extra burns |
| `rage`       | DISC\_WERE\_WOLF ≥ 2 | `IS_SET(ch->special, SPC_WOLFMAN)` | Wolfman form; requires rage > 99 first |
| `quills`     | DISC\_WERE\_HAWK ≥ 5 | `IS_SET(ch->newbits, NEW_QUILLS)` | Auto multi\_hit quills attack each round |
| `skin`       | DISC\_WERE\_BEAR ≥ 7 | `IS_SET(ch->newbits, NEW_SKIN)` | −100 armor |
| `slam`       | DISC\_WERE\_BEAR ≥ 8 | `IS_SET(ch->newbits, NEW_SLAM)` | Auto shoulder slam (1 in 2–5 rounds) |
| `rend`       | DISC\_WERE\_BEAR/BOAR ≥ 7 | `IS_SET(ch->newbits, NEW_REND)` | Rends target equipment |
| `perception` | DISC\_WERE\_RAPT ≥ 3 | `IS_SET(ch->newbits, NEW_PERCEPTION)` | Detect stealthy/hidden |
| `jawlock`    | DISC\_WERE\_RAPT ≥ 8 | `IS_SET(ch->newbits, NEW_JAWLOCK)` | Prevents target from fleeing |

---

## Combat Commands (require target or active fight)

| Command | Requirement | Cost | Notes |
|---------|-------------|------|-------|
| `moonbeam <t>` | DISC\_WERE\_LUNA ≥ 8 | 500 mana | 500/750/1000 damage by target alignment; 12-beat lag |
| `talons`       | DISC\_WERE\_RAPT ≥ 10 | — | 2000–4000 dam vs NPCs, 400–600 vs players; 12-beat lag |
| `roar`         | DISC\_WERE\_BEAR ≥ 6 | — | 1-in-6 chance to force flee; 18-beat lag |
| `staredown <t>`| DISC\_WERE\_OWL ≥ 5 | — | Force flee (must be fighting); Owl 6+ higher success; 16-beat lag |
| `motherstouch self` | DISC\_WERE\_LUNA ≥ 3 | 50 mana (combat) | +100 HP self-heal; 16-beat lag |
| `web <t>`      | DISC\_WERE\_SPID ≥ 2 | (spell) | Immobilize (AFF\_WEBBED); 12-beat lag |

---

## Between-Fight Setup

Handled in `between_fights` hook:

```
1. ch->rage < 100 and Wolf ≥ 2  → rage    (build rage toward >99 for max_dam bonus)
2. HP < max_hit and Raptor ≥ 5  → devour  (eat NPC corpse in room for 100–250 HP)
```

> The `rage` command when NOT in wolfman form adds 40–60 rage. Issue it twice
> across two ticks to push `ch->rage > 99`.  The max\_dam bonus (`+rage+400`)
> then activates automatically in every fight round.

---

## Bot AI Training Priority

```
1.  Boar  → 3    (incoming dam /= 2 — biggest single survivability gain)
2.  Bear  → 5    (dam *= 1.2 — 20% damage boost)
3.  Lynx  → 2    (+2 extra attacks per round)
4.  Raptor → 1   (auto rfangs every round)
5.  Spider → 1   (passive poison on every hit)
6.  Luna  → 1    (flameclaws toggle unlock)
6b. Luna  → 2    (moonarmour unlock — gear system requires DISC_WERE_LUNA ≥ 2)
7.  Bear  → 7    (skin and rend toggles)
8.  Bear  → 8    (slam auto-proc)
9.  Hawk  → 5    (quills toggle)
10. Wolf  → 2    (rage/wolfman form)
11. Mantis → 3   (dodge/disarm resist bonus)
12. Raptor → 3   (perception toggle)
13. Boar  → 7    (extra attacks from move pool)
14. Wolf  → 4    (razorclaws)
15. Raptor → 8   (jawlock — prevent flee)
16. Luna  → 3    (motherstouch self-heal in combat)
17. Spider → 2   (web command)
18. Raptor → 10  (talons burst)
19. Luna  → 8    (moonbeam 500-mana burst)
20. Owl   → 5    (staredown — force flee)
21. Pain  → 10   (+750 max_dam)
22. Mantis → 6   (full dodge/parry bonus)
23. Owl   → 8    (cocoon — gnosis-gated dam/2)
24. Max remaining disciplines to 10
```

---

## Combat Action Priority (each combat pulse)

```
1. roll 1–20:  moonbeam <target>     — Luna ≥ 8, mana ≥ 500
2. roll 21–45: talons                — Raptor ≥ 10
3. roll 46–65: motherstouch self     — Luna ≥ 3, mana ≥ 50, HP < max_hit*75%
4. roll 66–78: roar                  — Bear ≥ 6
5. roll 79–88: staredown <target>    — Owl ≥ 5
6. roll 89–95: web <target>          — Spider ≥ 2
```

---

## Source Files

| File | Contents |
|------|----------|
| `src/ww.c` | `do_sclaws`, `do_moonbeam`, `do_moongate`, `do_gmotherstouch`, `do_motherstouch`, `do_flameclaws`, `do_moonarmour`, `do_rend`, `do_skin`, `do_jawlock`, `do_perception`, `do_roar`, `do_slam`, `do_shred`, `do_talons`, `do_devour`, `do_staredown`, `do_disquiet`, `do_reshape`, `do_cocoon`, `do_quills`, `do_burrow`, `do_wither`, `do_razorclaws` |
| `src/clan.c` | `do_rage` (SPC\_WOLFMAN, rage build), `do_calm` (rage reduce), `do_web`, `do_claws` |
| `src/fight.c` | DISC\_WERE\_BEAR dam boost, DISC\_WERE\_BOAR incoming reduction/knockdown, DISC\_WERE\_LYNX/BOAR extra attacks, DISC\_WERE\_RAPT auto rfangs, NEW\_QUILLS multi\_hit, NEW\_SLAM proc, rage max\_dam bonus, DISC\_WERE\_PAIN max\_dam |
| `src/act_move.c` | `do_research`, `do_train`, discipline name table (indices 18–29) |
| `src/merc.h` | `DISC_WERE_*` constants (18–29), `SPC_WOLFMAN` (4), `NEW_SLAM` (1), `NEW_QUILLS` (2), `NEW_JAWLOCK` (4), `NEW_PERCEPTION` (8), `NEW_SKIN` (16), `NEW_REND` (128), `NEW_MONKFLAME` (256), `NEW_SCLAWS` (512) |
| `src/garou.h` | `WOLF_RAZORCLAWS` (1), `WOLF_COCOON` (2), `GCURRENT` (0), `GMAXIMUM` (1), `IS_GAR1(ch, flag)` |
| `src/bot_ai_werewolf.c` | Bot AI implementation (this class) |
