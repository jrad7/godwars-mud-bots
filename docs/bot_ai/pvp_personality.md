# Bot PvP Personality System

Bots track who has attacked them and how often, remember fights they could not win, and carry a single session nemesis they will attack on sight.

All state is per-session — cleared when the bot logs out, never saved to disk.

---

## Data Structures

### `BOT_GRUDGE_ENTRY` (`bot.h`)

```c
typedef struct {
    char name[32];
    int  attack_count;
} BOT_GRUDGE_ENTRY;
```

One entry per player who has attacked this bot during the current session. `attack_count` is incremented every time that player lands the opening hit.

### New fields in `BOT_DATA` (`bot.h`)

| Field | Type | Purpose |
|---|---|---|
| `nemesis` | `char[32]` | Name of the grudge-list player with the highest `attack_count` |
| `grudge_list` | `BOT_GRUDGE_ENTRY[16]` | All players who have attacked this bot this session |
| `grudge_count` | `int` | Number of entries in `grudge_list` |
| `blacklist` | `char[16][32]` | Players the bot has given up attacking (bot-initiated only) |
| `blacklist_count` | `int` | Number of entries in `blacklist` |
| `pvp_bot_initiated` | `bool` | TRUE when the bot chose this hunt unprovoked |

Limits: `BOT_PVP_GRUDGE_MAX 16`, `BOT_PVP_BLACKLIST_MAX 16`.

---

## Three Behavioral Components

### 1. Grudge List

Tracks every player who has attacked this bot and how many times. Used to bias target selection toward players the bot has a score to settle with.

**Population:** `bot_handle_pvp_attack()` — called the first tick a player is found as `ch->fighting`. Calls `bot_pvp_add_grudge()`, which either adds a new entry (count=1) or increments an existing one, then recalculates the nemesis.

**Effect on target selection** (`bot_find_pvp_target`):
- First pass scans only valid grudge-list members (skipping any that are also blacklisted).
- If at least one grudge target passes `bot_is_valid_pvp()`, the bot picks from those exclusively.
- Only if no grudge target is reachable does it fall through to a random valid target.

**Removal:** When the bot issues a `decapitate` or `gensteal` command against a target, that name is removed from the grudge list (revenge satisfied). The nemesis is recalculated afterward.

---

### 2. Nemesis

The single player with the most attacks on this bot this session. The nemesis is always the `grudge_list` entry with the highest `attack_count`, recalculated by `bot_pvp_update_nemesis()` every time the grudge list changes.

**Effect — attack on sight:**

Every AI tick, before the state dispatcher runs, the central PvP block checks whether the nemesis is in the same room:

```
if nemesis is in ch->in_room
  and bot_is_valid_pvp() passes
  and state != PVP_FIGHT / PVP_FLEE / LOGGING_OUT
  and ch->position != POS_FIGHTING
→ set pvp_target = nemesis
→ pvp_bot_initiated = TRUE
→ state = BOT_PVP_FIGHT
→ issue "kill <nemesis>"
```

This fires from any state: grinding, exploring, idle, training, shopping, or resting. The bot drops what it is doing immediately.

The nemesis only triggers on same-room presence (not world-wide). If the nemesis is not reachable via `bot_is_valid_pvp()` (safe room, wrong level range, etc.) the bot does not pursue.

**Clearing the nemesis:**
- When the bot kills the nemesis (decap/gensteal issued), `nemesis[0] = '\0'`.
- If the killed player was the only grudge-list entry, the nemesis stays clear.
- If others remain on the grudge list, the next-highest attacker becomes the new nemesis.

---

### 3. Blacklist

Players the bot has tried to kill and failed. The bot will not select them as a hunt target again this session, but will still fight back if attacked.

**Population — bot-initiated fights only.** The flag `pvp_bot_initiated` is set TRUE at every hunt launch site and FALSE when the bot decides to fight back against an attacker. Blacklisting only occurs when `pvp_bot_initiated` is TRUE.

Three triggers add a player to the blacklist:

| Trigger | Location |
|---|---|
| Hunt failed — target became NULL/invalid before kill | `bot_state_pvp_hunt()`, target-lost branch |
| Bot fled at low HP (<50% max_hit when target left room) | `bot_state_pvp_fight()`, loss branch |
| Bot incapacitated (POS_STUNNED) mid-fight | `bot_state_pvp_fight()`, stunned branch |

**Effect on target selection:** `bot_find_pvp_target()` skips any player whose name is in `blacklist` in both the grudge pass and the general pass.

**Note:** The blacklist does not prevent the bot from fighting back. If a blacklisted player attacks the bot, `bot_handle_pvp_attack()` still runs the normal aggression roll. The blacklist is purely a hunt-initiation filter.

---

## Flow Diagrams

### Being attacked

```
Player hits bot
  └─ bot_handle_pvp_attack()
       ├─ bot_pvp_add_grudge(attacker)   ← always
       ├─ bot_pvp_update_nemesis()        ← recalculates nemesis
       ├─ aggression roll passes
       │    └─ pvp_bot_initiated = FALSE
       │       state = BOT_PVP_FIGHT (fight back, no blacklist risk)
       └─ aggression roll fails
            └─ state = BOT_PVP_FLEE
```

### Bot initiates a hunt

```
Bot selects target (normal PvP check or WAR MODE)
  └─ bot_find_pvp_target()
       ├─ skip blacklisted players
       ├─ prefer grudge-list players
       └─ return best candidate
  pvp_bot_initiated = TRUE
  state = BOT_PVP_HUNT
    ├─ target found in room → state = BOT_PVP_FIGHT
    │    ├─ target killed (stunned)
    │    │    ├─ bot_pvp_remove_grudge(target)
    │    │    ├─ clear nemesis if applicable
    │    │    └─ issue decap/gensteal
    │    ├─ target left room, bot HP < 50%
    │    │    ├─ bot_pvp_add_blacklist(target)   ← bot_initiated only
    │    │    └─ state = BOT_RESTING
    │    └─ bot incapacitated
    │         ├─ bot_pvp_add_blacklist(target)   ← bot_initiated only
    │         └─ state = BOT_RESTING
    └─ target lost/invalid (no kill)
         ├─ bot_pvp_add_blacklist(target)        ← bot_initiated only
         └─ state = BOT_GRINDING
```

### Nemesis enters same room

```
Central PvP tick (every pulse, pre-dispatch)
  └─ nemesis[0] != '\0'
       └─ get_char_room(nemesis) != NULL
            └─ bot_is_valid_pvp() passes
                 └─ pvp_bot_initiated = TRUE
                    pvp_target = nemesis
                    state = BOT_PVP_FIGHT
                    issue "kill <nemesis>"
```

---

## Debug Logging

All grudge/nemesis/blacklist events emit `[GRUDGE]` watch messages visible via the bot watcher:

| Message | Meaning |
|---|---|
| `[GRUDGE] Nemesis is now: <name>` | Nemesis updated after an attack |
| `[GRUDGE] <name> attack count now N` | Existing grudge entry incremented |
| `[GRUDGE] Blacklisting <name> (incapacitated)` | Added to blacklist after being stunned |
| `[GRUDGE] Blacklisting <name> (fled at low HP)` | Added to blacklist after health loss |
| `[GRUDGE] Blacklisting <name> (hunt failed, no kill)` | Added to blacklist after failed hunt |
| `[GRUDGE] Finishing <name> -- removed from grudge list` | Removed on kill |
| `[GRUDGE] Nemesis <name> is here -- attacking!` | Nemesis interrupt fired |
