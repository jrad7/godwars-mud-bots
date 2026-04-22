# Dystopia: Game Overview

Dystopia is a hack-and-slash MUD built on the Dystopia 1.2 codebase (a ROM/Merc
derivative). It is text-based, combat-focused, and centers on open PvP, deep
class specialization, and a population of autonomous bots that keep the world
populated at all hours.

## What makes Dystopia unique

### Bot-driven living world
Bots are not NPCs. They are player characters controlled by server-side AI
that logs them in through a fake descriptor, runs them through the same
commands a human would type, and persists them like any other pfile. Bots
grind, explore, train skills, shop, forge gear, rest, relevel, and hunt other
players. The world always has targets, competitors, and activity — even with
zero humans online.

Because bots are PCs, they:
- Level and gain gear the same way players do
- Can be killed, decapitated, and gensteal'd by players (and each other)
- Show up on `who`, occupy rooms, and compete for spawns
- Are **not** flagged `IS_NPC` — code that assumes NPC-ness does not apply

### Always-on, personal PvP
PvP is always enabled. Bots track every player who has attacked them during
the current session in a per-session grudge list, promote the worst offender
to a "nemesis" who is attacked on sight, and blacklist players who beat them
in bot-initiated fights. See `pvp_personality.md` for the full system. The
result is a PvP landscape where opponents remember you, hunt you, and give
up on you individually.

### Deep class roster
Classes include demon, drow, lich, mage, monk, ninja, samurai, shapeshifter,
spiderdroid, tanarri, undead knight, vampire, werewolf, and angel. Each has
distinct skills, spells, progression, and power curves. See the per-class
files under `bot_ai/` for class-specific references.

## Game loop (typical)
1. Roll a class, start in Midgaard (the central hub).
2. Grind mobs in level-appropriate zones for XP and gear.
3. Train skills, work toward upgrading your class.
4. Engage in PvP — voluntarily or because a bot's nemesis list named you.
5. Decapitate fallen enemies to confirm kills and claim trophies.

## Lineage and scope
Dystopia 1.2 descends from ROM/Merc/DikuMUD. The codebase is C, single-threaded
game loop with a threaded descriptor accept. Combat, movement, and AI all run
on the pulse tick. Zones are VNUM-ranged `.are` files loaded at boot.
