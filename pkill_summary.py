#!/usr/bin/env python3
"""
pkill_summary.py
Parses pkill.log and prints a PvP class performance report.
Only DECAP lines are counted (GENSTEAL lines are ignored).
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

LOG_FILE = Path(__file__).parent / "txt" / "pkill.log"

# Match: [DATE TIME] DECAP: Killer (Class) decapitated Victim (Class) (room N)
DECAP_RE = re.compile(
    r'\[.*?\]\s+DECAP:\s+(\w+)\s+\(([^)]+)\)\s+decapitated\s+(\w+)\s+\(([^)]+)\)'
)

def load_decaps(path: Path):
    kills = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = DECAP_RE.search(line)
            if m:
                killer, killer_class, victim, victim_class = m.groups()
                kills.append({
                    "killer": killer,
                    "killer_class": killer_class,
                    "victim": victim,
                    "victim_class": victim_class,
                })
    return kills


def analyse(kills):
    """
    Build per-class stats:
      kills   – how many times that class killed someone
      deaths  – how many times that class died
      k_d     – kills / deaths ratio
      victims – Counter of victim classes
      killers – Counter of who killed this class
    """
    classes = defaultdict(lambda: {"kills": 0, "deaths": 0,
                                   "victims": defaultdict(int),
                                   "killers": defaultdict(int)})

    for k in kills:
        kc = k["killer_class"]
        vc = k["victim_class"]
        classes[kc]["kills"] += 1
        classes[kc]["victims"][vc] += 1
        classes[vc]["deaths"] += 1
        classes[vc]["killers"][kc] += 1

    return classes


def kd(stats):
    d = stats["deaths"]
    return stats["kills"] / d if d else float("inf")


def top(counter, n=3):
    return sorted(counter.items(), key=lambda x: -x[1])[:n]


def print_report(classes, kills):
    print("=" * 66)
    print(f"  DYSTOPIA PvP CLASS PERFORMANCE REPORT")
    print(f"  Total DECAP kills analysed: {len(kills)}")
    print("=" * 66)

    # Sort by most kills
    sorted_classes = sorted(classes.items(), key=lambda x: -x[1]["kills"])

    header = f"{'Class':<18} {'Kills':>6} {'Deaths':>7} {'K/D':>7}"
    print(f"\n{header}")
    print("-" * 42)
    for cls, s in sorted_classes:
        ratio = kd(s)
        ratio_str = f"{ratio:.2f}" if ratio != float("inf") else "∞"
        print(f"{cls:<18} {s['kills']:>6} {s['deaths']:>7} {ratio_str:>7}")

    print("\n" + "=" * 66)
    print("  DETAILED BREAKDOWN PER CLASS")
    print("=" * 66)

    for cls, s in sorted_classes:
        ratio = kd(s)
        ratio_str = f"{ratio:.2f}" if ratio != float("inf") else "∞"
        print(f"\n  [{cls}]  Kills: {s['kills']}  Deaths: {s['deaths']}  K/D: {ratio_str}")

        fav = top(s["victims"])
        if fav:
            print(f"    Top victims   : " +
                  ", ".join(f"{c} ({n})" for c, n in fav))

        nemesis = top(s["killers"])
        if nemesis:
            print(f"    Killed most by: " +
                  ", ".join(f"{c} ({n})" for c, n in nemesis))

    print("\n" + "=" * 66)
    print("  CLASS VS CLASS MATRIX  (rows = killer class, cols = victim class)")
    print("=" * 66)

    all_classes = sorted(classes.keys())
    col_w = 6
    name_w = 18

    # Header row
    header_row = f"{'':>{name_w}}"
    for c in all_classes:
        abbr = c[:col_w]
        header_row += f"{abbr:>{col_w}}"
    print(header_row)
    print("-" * (name_w + col_w * len(all_classes)))

    for kc in all_classes:
        row = f"{kc:<{name_w}}"
        for vc in all_classes:
            n = classes[kc]["victims"].get(vc, 0)
            row += f"{n if n else '.':>{col_w}}"
        print(row)

    print()


def main():
    path = LOG_FILE
    if len(sys.argv) > 1:
        path = Path(sys.argv[1])

    if not path.exists():
        print(f"ERROR: log file not found: {path}", file=sys.stderr)
        sys.exit(1)

    kills = load_decaps(path)
    if not kills:
        print("No DECAP entries found in log.", file=sys.stderr)
        sys.exit(0)

    classes = analyse(kills)
    print_report(classes, kills)


if __name__ == "__main__":
    main()
