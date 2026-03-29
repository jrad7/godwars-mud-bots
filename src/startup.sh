#!/bin/bash
port=${1:-8000}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AREA_DIR="$SCRIPT_DIR/../area"
LOG_DIR="$SCRIPT_DIR/../log"

mkdir -p "$LOG_DIR"

cd "$AREA_DIR"
[ -f shutdown.txt ] && rm -f shutdown.txt

while true; do
    index=1000
    while [ -e "$LOG_DIR/$index.log" ]; do
        index=$((index + 1))
    done
    logfile="$LOG_DIR/$index.log"

    echo "Starting dystopia on port $port, logging to $logfile"
    "$SCRIPT_DIR/dystopia" "$port" >> "$logfile" 2>&1

    if [ -f "$AREA_DIR/shutdown.txt" ]; then
        rm -f "$AREA_DIR/shutdown.txt"
        echo "Shutdown detected, exiting."
        exit 0
    fi
    echo "Restarting in 2 seconds..."
    sleep 2
done
