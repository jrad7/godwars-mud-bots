# Adding a New Class AI

Each player-class bot has its own AI file (`bot_ai_{class}.c`) that implements a
`BOT_CLASS_AI` vtable.  The generic state machine in `bot_ai.c` dispatches through
this table, so adding a new class requires **no changes to the state handlers**.

---

## The vtable (`BOT_CLASS_AI` in `bot.h`)

```c
typedef struct {
    bool (*should_train)   (CHAR_DATA *ch); /* Class-specific train check             */
    bool (*do_train)       (CHAR_DATA *ch); /* Execute one class training step        */
    bool (*buff_check)     (CHAR_DATA *ch); /* Apply passive buffs; TRUE = cmd sent   */
    void (*combat_action)  (CHAR_DATA *ch); /* Fire one ability per combat tick       */
    bool (*between_fights) (CHAR_DATA *ch); /* Between-fight setup; TRUE = cmd sent   */
} BOT_CLASS_AI;
```

Any hook you leave `NULL` is a no-op — only implement what your class needs.

### Hook contracts

| Hook | Called when | Return value |
|---|---|---|
| `should_train` | Deciding whether to enter BOT_TRAINING | `TRUE` to trigger training |
| `do_train` | Inside BOT_TRAINING, before generic hp/mana/move spending | `TRUE` if a command was issued |
| `buff_check` | Each grinding tick between fights | `TRUE` if a command was issued (stops further processing that tick) |
| `combat_action` | Each grinding tick while `ch->position == POS_FIGHTING` | void |
| `between_fights` | Each grinding tick between fights, after `buff_check` | `TRUE` if a command was issued (stops further processing that tick) |

`do_train` runs **before** the generic stat-spending loop in `bot_do_train()`, so
class-specific rank/skill progression always takes priority over raw hp/mana/move.

---

## Step-by-step: adding CLASS_FOO

### 1. Add a BOT_CLASS constant (`bot.h`)

```c
#define BOT_CLASS_VAMPIRE   0
#define BOT_CLASS_MONK      1
#define BOT_CLASS_NINJA     2
#define BOT_CLASS_DEMON     3
#define BOT_CLASS_FOO       4   /* <-- add here */
#define BOT_CLASS_COUNT     5   /* <-- bump this */
```

### 2. Create `bot_ai_foo.c`

Minimal stub (all NULL):

```c
#include "merc.h"
#include "bot.h"

const BOT_CLASS_AI bot_foo_ai = {
    NULL,   /* should_train   */
    NULL,   /* do_train       */
    NULL,   /* buff_check     */
    NULL,   /* combat_action  */
    NULL    /* between_fights */
};
```

Fill in only the hooks your class needs.  See `bot_ai_vampire.c` for a fully
implemented example and `bot_ai_monk.c` / `bot_ai_ninja.c` for partial examples.

### 3. Register the vtable (`bot_ai.c`)

```c
/* At the top of bot_ai.c, with the other externs: */
extern const BOT_CLASS_AI bot_foo_ai;

/* In the bot_class_ai[] table: */
const BOT_CLASS_AI *bot_class_ai[BOT_CLASS_COUNT] = {
    &bot_vamp_ai,   /* BOT_CLASS_VAMPIRE */
    &bot_monk_ai,   /* BOT_CLASS_MONK    */
    &bot_ninja_ai,  /* BOT_CLASS_NINJA   */
    &bot_demon_ai,  /* BOT_CLASS_DEMON   */
    &bot_foo_ai     /* BOT_CLASS_FOO     */
};
```

### 4. Add a roster entry (`bot_mgr.c`)

```c
{ "Botname", BOT_CLASS_FOO, BOT_LIFE_PERMANENT, 60, 50, 50, 0, 0, FALSE, FALSE },
```

### 5. Map the class name (`bot_ai.c` → `bot_class_name`)

```c
case BOT_CLASS_FOO: return "foo";
```

This is the string passed to `selfclass` when the bot picks its class at level 3.

### 6. Add to the Makefile

```makefile
bot_mgr.o bot_ai.o bot_chat.o \
bot_ai_vampire.o bot_ai_monk.o bot_ai_ninja.o bot_ai_demon.o \
bot_ai_foo.o
```

---

## Implementing each hook

### `should_train` / `do_train`

These pair up.  `should_train` is the cheap check (no commands); `do_train` is the
one that actually issues a command.  Both receive the character and may inspect
`ch->pcdata->rank`, `ch->exp`, `ch->power[]`, etc.

```c
static bool bot_foo_should_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_FOO) ) return FALSE;
    /* e.g., return TRUE when the bot can afford the next rank */
    return ( ch->pcdata->rank < FOO_MAX_RANK && ch->exp >= 5000000 );
}

static bool bot_foo_do_train( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_FOO) ) return FALSE;
    if ( ch->pcdata->rank < FOO_MAX_RANK && ch->exp >= 5000000 )
    {
        bot_cmd( ch, "train nextrank" );
        return TRUE;
    }
    return FALSE;
}
```

### `buff_check`

Check each toggle buff in priority order.  Issue **one command** and return `TRUE`.
The function will be called again next tick for the next buff.

```c
static bool bot_foo_buff_check( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_FOO) ) return FALSE;

    if ( !IS_SET(ch->act, PLR_FOO_AURA) )
    { bot_cmd( ch, "fooaura" ); return TRUE; }

    return FALSE;
}
```

### `combat_action`

Pick **one** ability per call.  Use `number_range(1,100)` thresholds to spread
ability usage naturally.  Higher-priority abilities get lower thresholds.

```c
static void bot_foo_combat_action( CHAR_DATA *ch )
{
    CHAR_DATA *target = ch->fighting;
    int roll;
    char cmd[MAX_INPUT_LENGTH];

    if ( target == NULL ) return;
    roll = number_range( 1, 100 );

    if ( ch->power[DISC_FOO_BLAST] >= 3 && roll <= 40 )
    {
        sprintf( cmd, "blast %s", IS_NPC(target) ? target->short_descr : target->name );
        bot_cmd( ch, cmd );
        return;
    }
    /* fallback: normal combat continues */
}
```

### `between_fights`

Setup done once per tick between kills: entering a stance, applying a long-duration
effect, etc.  Return `TRUE` if you issued a command so mob-hunting is skipped for
that tick.

```c
static bool bot_foo_between_fights( CHAR_DATA *ch )
{
    if ( !IS_CLASS(ch, CLASS_FOO) ) return FALSE;
    /* example: enter a power stance once */
    if ( ch->stance[0] != FOO_POWERSTANCE )
    {
        bot_cmd( ch, "powerstance" );
        return TRUE;
    }
    return FALSE;
}
```

---

## Reference: existing implementations

| File | Class | Hooks implemented |
|---|---|---|
| [bot_ai_vampire.c](bot_ai_vampire.c) | Vampire | All five (fully implemented) |
| [bot_ai_monk.c](bot_ai_monk.c) | Monk | `between_fights` (autostance) |
| [bot_ai_ninja.c](bot_ai_ninja.c) | Ninja | `should_train`, `do_train` (belt progression) |
| [bot_ai_demon.c](bot_ai_demon.c) | Demon | None (all NULL stub) |
