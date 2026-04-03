#!/usr/bin/env python3
"""
modify_resets.py — Bulk-edit mob resets in Dystopia .are files.

Two operations (each optional, combinable):

  --scale N        Multiply all existing M reset counts by N
  --fill-mob VNUM  Add M resets for mob VNUM to every room that has no
                   mob resets, with count set by --fill-count (default 5)

Usage examples:
    # Scale existing resets 5x and fill empty rooms with bumpkins (vnum 1128)
    python modify_resets.py --scale 5 --fill-mob 1128 area/shire.are

    # Just fill empty rooms with 3 of mob 1103
    python modify_resets.py --fill-mob 1103 --fill-count 3 area/shire.are

    # Just scale resets 2x (no fill)
    python modify_resets.py --scale 2 area/canyon.are

    # Preview without writing
    python modify_resets.py --scale 5 --fill-mob 1128 --dry-run area/shire.are
"""

import argparse
import re
import sys
from pathlib import Path


def process_file(path: Path, scale: int | None, fill_mob: int | None,
                 fill_count: int, dry_run: bool):
    content = path.read_text()

    if "#RESETS" not in content:
        print(f"  {path.name}: no #RESETS section found, skipped")
        return

    pre, rest = content.split("#RESETS\n", 1)
    resets_body, post = rest.split("\nS\n", 1)

    lines = resets_body.split("\n")

    scaled = 0
    if scale is not None:
        new_lines = []
        for line in lines:
            m = re.match(r'^(M \d+ \d+ )(\d+)( \d+)$', line)
            if m:
                new_count = int(m.group(2)) * scale
                new_lines.append(m.group(1) + str(new_count) + m.group(3))
                scaled += 1
            else:
                new_lines.append(line)
        lines = new_lines

    filled = 0
    if fill_mob is not None:
        rooms_with_mobs = set()
        for line in lines:
            m = re.match(r'^M \d+ \d+ \d+ (\d+)$', line)
            if m:
                rooms_with_mobs.add(int(m.group(1)))

        # Detect area VNUM range from #AREADATA
        vnum_m = re.search(r'VNUMs\s+(\d+)\s+(\d+)', pre)
        if not vnum_m:
            print(f"  {path.name}: could not detect VNUM range, skipped fill")
        else:
            vmin, vmax = int(vnum_m.group(1)), int(vnum_m.group(2))
            all_rooms = set()
            for m in re.finditer(r'^#(\d+)$', resets_body + pre + "\n" + post,
                                  re.MULTILINE):
                r = int(m.group(1))
                if vmin <= r <= vmax:
                    all_rooms.add(r)
            # Also scan ROOMDATA section
            room_section = re.search(r'#ROOMDATA(.*?)(?=#\w|\Z)', content, re.DOTALL)
            if room_section:
                for m in re.finditer(r'^#(\d+)$', room_section.group(1), re.MULTILINE):
                    r = int(m.group(1))
                    if vmin <= r <= vmax:
                        all_rooms.add(r)

            empty_rooms = sorted(all_rooms - rooms_with_mobs)
            new_lines = [f"M 0 {fill_mob} {fill_count} {room}"
                         for room in empty_rooms]
            lines.extend(new_lines)
            filled = len(empty_rooms)

    new_resets = "\n".join(lines)
    new_content = pre + "#RESETS\n" + new_resets + "\nS\n" + post

    tag = "[dry-run] " if dry_run else ""
    if scale is not None:
        print(f"{tag}{path.name}: scaled {scaled} M reset(s) x{scale}")
    if fill_mob is not None:
        print(f"{tag}{path.name}: added mob {fill_mob} x{fill_count} to {filled} empty room(s)")

    if not dry_run:
        path.write_text(new_content)


def main():
    parser = argparse.ArgumentParser(
        description="Bulk-edit mob resets in Dystopia .are files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Usage examples:")[1],
    )
    parser.add_argument("files", nargs="+", metavar="FILE")
    parser.add_argument("--scale", type=int, metavar="N",
                        help="Multiply all existing M reset counts by N")
    parser.add_argument("--fill-mob", type=int, metavar="VNUM",
                        help="Add this mob to every room with no mob resets")
    parser.add_argument("--fill-count", type=int, default=5, metavar="N",
                        help="Count for --fill-mob resets (default: 5)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would change without writing")

    args = parser.parse_args()

    if args.scale is None and args.fill_mob is None:
        parser.error("Specify at least one of --scale or --fill-mob")

    for f in args.files:
        p = Path(f)
        if not p.exists():
            print(f"ERROR: {p} not found", file=sys.stderr)
            sys.exit(1)
        process_file(p, args.scale, args.fill_mob, args.fill_count, args.dry_run)


if __name__ == "__main__":
    main()
