# Bot Takeover Implementation Plan

Allowing a player or admin to temporarily assume control of a generated bot requires intercepting the input descriptor, safely transferring context between characters, and temporarily pausing the bot's AI logic so the user isn't fighting the automated pulses.

This is a **Moderate** difficulty implementation that requires changes predominantly in `act_wiz.c`, `comm.c`, and `bot_ai.c`.

## 1. The Takeover Mechanism (Descriptor Swapping)

The architecture already handles taking over bodies via the O-switch (`switch`) command. Rather than requiring users to authenticate through the normal login flow with a bot's dynamically generated password, creating a dedicated `possess` or `takeover` command is much safer and cleaner.

**Tasks:**
- Create a new command (e.g., `do_possess` or `do_takeover`).
- Save the player's current character state in their original body.
- Detach the player's `d->character` pointer and attach it to the bot's `CHAR_DATA` memory location.
- The bot's original fake heartbeat descriptor (the one with `BOT_DESCRIPTOR_SENTINEL`) should be temporarily discarded or overwritten by the live player socket.

## 2. Pausing the Bot AI

`bot_ai_tick()` checks every pulse and applies automated commands to any character with `ch->pcdata->is_bot == TRUE`. Because the possessed bot remains technically a bot, the player will find themselves fighting the AI for control of the body.

**Tasks:**
- Modify `bot_ai_tick()` in `bot_mgr.c` or `bot_ai.c` to look for a real, live descriptor.
- Add an intercept condition that skips the standard bot logic if the character's descriptor isn't a fake bot Sentinel:

```c
/* Skip if the bot has been taken over by a real player socket */
if (ch->desc != NULL && ch->desc->descriptor != BOT_DESCRIPTOR_SENTINEL)
    continue;
```

## 3. Graceful Return & Logout

When the admin/player finishes examining or piloting the bot, control must be yielded seamlessly back to the AI without destroying the character or causing descriptor memory leaks.

**Tasks:**
- Modify `do_return` in `act_wiz.c` (or wherever `possess` exits) to catch when the user is returning from a bot body specifically.
- Dynamically recreate the fake bot descriptor and assign it back to the bot's data so the AI heartbeat can pick it back up:

```c
DESCRIPTOR_DATA *d_bot = alloc_perm( sizeof(*d_bot) );
init_descriptor( d_bot, BOT_DESCRIPTOR_SENTINEL );
d_bot->character = bot_char;
bot_char->desc = d_bot;
```

- **Safety Net:** Intercept `do_quit` inside `act_comm.c`. If a player types `quit` while possessing a bot, you must hook it to automatically execute `do_return` instead, safely depositing them back into their original body. Otherwise, the bot's dedicated `BOT_DATA` memory will leak, and `do_quit` will inappropriately tear down the bot instead of cleanly dropping the player form.

## Summary

The existing architecture handles standard descriptor swapping and the framework is solidly in place to support taking over entities. By creating a custom clone of `do_switch` purposefully designed for bots that manages the `BOT_DESCRIPTOR_SENTINEL` handover, and adding a simple one-line guard to the AI heartbeat routine, you establish safe parameters for piloting server-side bots.
