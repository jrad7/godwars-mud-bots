#!/bin/bash
#
# Migrate bot player files from player/ to bot/
# Run from the repo root (where player/ and txt/ directories are).
# The MUD server must be stopped before running this script.
#

PLAYER_DIR="player"
BOT_DIR="bot"
ROSTER="txt/bots.txt"

# Real player names to keep in player/ (case-sensitive)
KEEP="Claude Gemma Jrad Kast Vict"

if [ ! -d "$PLAYER_DIR" ]; then
    echo "Error: $PLAYER_DIR directory not found. Run from repo root."
    exit 1
fi

# Create bot directory structure
mkdir -p "$BOT_DIR/backup" "$BOT_DIR/store"

is_real_player() {
    for p in $KEEP; do
        [ "$1" = "$p" ] && return 0
    done
    return 1
}

# Move everything in player/ that isn't a real player or subdirectory
moved=0
for f in "$PLAYER_DIR"/*; do
    [ ! -f "$f" ] && continue
    name=$(basename "$f")
    if is_real_player "$name"; then
        echo "Keeping real player: $name"
        continue
    fi
    mv "$f" "$BOT_DIR/$name"
    moved=$((moved + 1))
done

# Move backup (finger cache) files
for f in "$PLAYER_DIR/backup"/*; do
    [ ! -f "$f" ] && continue
    name=$(basename "$f")
    is_real_player "$name" && continue
    mv "$f" "$BOT_DIR/backup/$name"
done

# Move store (full backup) files
for f in "$PLAYER_DIR/store"/*; do
    [ ! -f "$f" ] && continue
    name=$(basename "$f")
    is_real_player "$name" && continue
    mv "$f" "$BOT_DIR/store/$name"
done

echo "Moved $moved bot file(s) to $BOT_DIR/."

# Remove retired directory
if [ -d "$PLAYER_DIR/retired" ]; then
    count=$(find "$PLAYER_DIR/retired" -maxdepth 1 -type f | wc -l)
    rm -rf "$PLAYER_DIR/retired"
    echo "Removed $PLAYER_DIR/retired/ ($count file(s))."
fi

echo "Migration complete."
