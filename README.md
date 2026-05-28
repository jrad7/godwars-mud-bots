# Dystopia MUD

A copy of the Dystopia MUD codebase updated to compile with current GCC versions. All original documentation, readmes, and licenses are included untouched in the `doc/` folder.

## Building & Running

```bash
cd src
make clean && make
./startup &   # starts on port 8000
```

Requires the `build-essential` package.

---

## Bot System

The bot system simulates autonomous player characters on the server. Bots are full player characters — they use the standard command interpreter, accumulate experience, train stats, equip gear, pursue class upgrades, and interact with real players via chat. They are not NPCs.

### Population Management

Bots are managed from `txt/bots.txt` (roster up to 128) with 20–50 online at any time. Each bot has a lifespan tier that determines how long it plays before retiring:

| Tier | Count | Lifespan |
|------|-------|----------|
| Permanent | 14 | Never retire |
| Long | 14 | ~1 week playtime |
| Medium | 28 | ~3 days playtime |
| Short | 42 | ~4 hours playtime |

Sessions last 1–4 hours with 1–8 hour gaps between logins. New bots generate names from class-specific syllable pools.

### Personality Traits

Each bot rolls randomized traits on creation (0–100 scale):

- **Chattiness** — frequency of unprompted chat messages
- **Aggression** — likelihood of initiating PvP hunts
- **Explorer** — roaming tendency vs. staying in grinding zones

### AI States

The bot AI (`src/bot_ai.c`) runs a timer-driven state machine:

| State | Description |
|-------|-------------|
| `BOT_IDLE` | Standing, social chat, observing |
| `BOT_EXPLORING` | Navigating the world |
| `BOT_GRINDING` | Killing mobs for experience |
| `BOT_TRAINING` | Spending primal on stats/skills |
| `BOT_PVP_HUNT` | Seeking a player target |
| `BOT_PVP_FIGHT` | Active PvP combat |
| `BOT_SHOPPING` | Buying/selling at shops |
| `BOT_RESTING` | Recovering HP/mana |
| `BOT_LOGGING_OUT` | Farewell sequence before disconnect |
| `BOT_PVP_FLEE` | Fleeing from a hunter |

### Grinding Zones

Bots progress through six difficulty tiers based on max HP:

| Tier | Max HP | Zones |
|------|--------|-------|
| 1 | < 3,500 | Mud School, Smurf Village, Daycare |
| 2 | < 20,000 | Sewer, Shire, Mega City |
| 3 | < 30,000 | Moria, Thalos, Galaxy |
| 4 | < 50,000 | Canyon, Plains, Air |
| 5 | < 100,000 | Disney, Weed Fields, Drow Underdark |
| 6 | 100,000+ | Jobo Hell, Jobo Heaven |

Each zone is a pre-recorded navigation and combat sequence. A scatter pattern on arrival prevents bot congestion.

### Equipment System

`src/bot_gear.c` manages gear automatically each tick:

1. Ensure the bot is standing
2. Extract junk/looted items
3. Fill empty slots with newbiepack gear
4. Replace newbiepack with class-crafted gear once primal is earned
5. Sweep for uncovered slots

Each class has a dedicated vnum range (e.g., Vampire 33040–33055, Monk 33020–33032).

### PvP System

Each bot tracks:

- **Grudge list** (up to 16) — players who attacked this bot, with attack counts
- **Blacklist** (up to 16) — hunt targets that failed this session (skipped)
- **Nemesis** — the player with the most attacks against this bot
- **Chase mode** — ignores health gates when pursuing a fleeing target

Global PvP modes are admin-controlled:

| Mode | Behavior |
|------|----------|
| `PEACE` | All bot combat disabled |
| `NORMAL` | Individual aggression logic |
| `WAR` | All bots hunt relentlessly |

### Chat System

`src/bot_chat.c` handles:

- **Triggered responses** to greetings, farewells, "how are you", "what class are you", etc.
- **Tell handling** for private messages
- **Unprompted chat** on idle timers, frequency controlled by the chattiness trait

### Training & Class Upgrades

Bots train toward HP/mana/move targets (50k/35k/35k) and pursue class upgrades once they have sufficient quest points (40k) and PK score (1000). Base classes upgrade to advanced classes:

| Base | Advanced |
|------|----------|
| Vampire | Undead Knight |
| Monk | Angel |
| Ninja | Samurai |
| Demon | Tanarri |
| Drow | Droid |
| Werewolf | Shapeshifter |
| Mage | Lich |

### Class AI Framework

Each class can implement up to five vtable hooks in `src/bot_ai_<class>.c`:

```c
typedef struct {
    bool (*should_train)   (CHAR_DATA *ch);  // can afford next training tier?
    bool (*do_train)       (CHAR_DATA *ch);  // execute one training step
    bool (*buff_check)     (CHAR_DATA *ch);  // apply missing passive buffs
    void (*combat_action)  (CHAR_DATA *ch);  // pick one offensive ability
    bool (*between_fights) (CHAR_DATA *ch);  // setup between kills (stances, etc.)
} BOT_CLASS_AI;
```

Each hook issues at most one command per tick. NULL hooks are no-ops. Currently implemented classes: Vampire (all 5 hooks), Demon, Ninja, Monk, and others at varying levels of completeness.

### Resilience Features

- **Stuck detection** — ring buffer of last 10 commands; triggers zone change if a loop is detected
- **Decap recovery** — auto-recall and `call all` after decapitation
- **Blind recovery** — cures blindness after recall
- **Limb loss tracking** — calls for gear recovery when limbs are lost

### Admin Commands

| Command | Effect |
|---------|--------|
| `botwar` | Switch all bots to WAR mode |
| `botnormal` | Return bots to NORMAL mode |
| `botpeace` | Switch all bots to PEACE mode |

Requires Immortal rank or the `BOT_OVERSEER` role.

### Configuration

Key feature flags in `src/bot.h`:

```c
BOT_DEBUG                  // verbose logging
BOT_UPGRADES_ENABLED       // enable class upgrade pursuit
BOT_DISABLE_SOCIAL_AGGRO   // prevent NPC chain-aggro during grinding
BOT_MIN_ONLINE / BOT_MAX_ONLINE   // population bounds (default 20–50)
MAX_BOT_ROSTER             // roster capacity (default 128)
```

### Source Files

| File | Description |
|------|-------------|
| `src/bot.h` | Master header: structs, constants, function declarations |
| `src/bot_mgr.c` | Population manager, login/logout cycles, roster persistence |
| `src/bot_ai.c` | Generic state machine dispatcher |
| `src/bot_chat.c` | Chat trigger and unprompted dialogue system |
| `src/bot_gear.c` | Equipment management |
| `src/bot_ai_<class>.c` | Per-class AI vtable implementations (15 files) |

Full class references and the framework guide for adding new class AI are in `doc/bot_ai/`.
