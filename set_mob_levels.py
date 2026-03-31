#!/usr/bin/env python3
"""
set_mob_levels.py — Set mob levels in Dystopia .are files.

The mob stat line format is:
    <level> <hitroll> <ac> <NdS+bonus> <NdS+bonus>

Level is the single knob that controls HP, hitroll, damroll, and AC for
mobs in this codebase (db.c uses a level-based formula for all of these).

HP formula (db.c):
    max_hit = level^2 * 20 + rand(level^2 * 5, level^2 * 10)

Quick reference:
    level  5  →    625 –    750 HP
    level 10  →  2,500 –  3,000 HP
    level 15  →  5,625 –  6,750 HP
    level 18  →  8,100 –  9,720 HP
    level 25  → 15,625 – 18,750 HP
    level 50  → 62,500 – 75,000 HP

Usage examples:
    # Set all mobs in two files to level 5
    python set_mob_levels.py -l 5 area/school.are area/heaven.are

    # Preview without writing
    python set_mob_levels.py -l 15 --dry-run area/hell.are area/smurf.are

    # Set every .are file in the area/ directory to level 10
    python set_mob_levels.py -l 10 --dir area/
"""

import argparse
import re
import sys
from pathlib import Path

# Matches the mob stat line: level hitroll ac NdS+bonus NdS+bonus
# hitroll and ac can be negative (e.g. special mobs with -99 AC)
STAT_LINE = re.compile(
    r'^(\s*)\d+(\s+-?\d+\s+-?\d+\s+\d+d\d+[+\-]\d+\s+\d+d\d+[+\-]\d+\s*)$',
    re.MULTILINE,
)


def process_file(path: Path, level: int, dry_run: bool) -> int:
    """Replace mob levels in a single .are file. Returns count of mobs changed."""
    content = path.read_text()
    matches = STAT_LINE.findall(content)
    count = len(matches)
    if count == 0:
        return 0

    new_content = STAT_LINE.sub(
        lambda m: m.group(1) + str(level) + m.group(2),
        content,
    )

    if not dry_run:
        path.write_text(new_content)

    return count


def main():
    parser = argparse.ArgumentParser(
        description="Set mob levels in Dystopia .are files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Usage examples:")[1],
    )
    parser.add_argument(
        "-l", "--level",
        type=int,
        required=True,
        metavar="LEVEL",
        help="Target mob level (1–100)",
    )
    parser.add_argument(
        "files",
        nargs="*",
        metavar="FILE",
        help=".are file(s) to update",
    )
    parser.add_argument(
        "--dir",
        metavar="DIR",
        help="Process all .are files in this directory",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would change without writing any files",
    )

    args = parser.parse_args()

    if not args.files and not args.dir:
        parser.error("Specify at least one FILE or use --dir")

    if not 1 <= args.level <= 100:
        parser.error("Level must be between 1 and 100")

    targets: list[Path] = []
    for f in args.files:
        p = Path(f)
        if not p.exists():
            print(f"ERROR: {p} not found", file=sys.stderr)
            sys.exit(1)
        targets.append(p)

    if args.dir:
        d = Path(args.dir)
        if not d.is_dir():
            print(f"ERROR: {d} is not a directory", file=sys.stderr)
            sys.exit(1)
        targets.extend(sorted(d.glob("*.are")))

    if not targets:
        print("No .are files found.", file=sys.stderr)
        sys.exit(1)

    if args.dry_run:
        print("[dry-run] No files will be written.\n")

    total = 0
    for path in targets:
        count = process_file(path, args.level, args.dry_run)
        if count:
            tag = "(dry-run) " if args.dry_run else ""
            print(f"{tag}{path.name}: {count} mob(s) -> level {args.level}")
            total += count
        else:
            print(f"  {path.name}: no mob stat lines found, skipped")

    print(f"\nTotal: {total} mob(s) across {len(targets)} file(s)")


if __name__ == "__main__":
    main()
