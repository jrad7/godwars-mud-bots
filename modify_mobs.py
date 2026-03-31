"""
modify_mobs.py — Mob stat editor for Diku/Merc .are files.

Two modes per area:
  - "normalize": set exact HP and damage for all mobs, with optional boss threshold
  - "scale":     multiply/divide existing HP and damage by factors

Mob stat line format (after the act/aff/align S line):
  hitroll level AC  NdS+P  NdS+P
  gold exp
  act2 aff2 sex

Boss detection: mobs with gold >= boss_gold_threshold keep their own boss_hp/boss_dam
instead of the regular grind values. Set boss_gold_threshold=None to normalize everything.

GM/immortal mobs (HP >= IMMORTAL_HP_FLOOR) are never touched.
"""

import re
import os

# ─────────────────────────────────────────────
# AREA CONFIGURATIONS
# ─────────────────────────────────────────────

AREAS = [
    {
        "file": "area/school.are",
        "mode": "normalize",

        # Regular grind mobs
        "hp":  "75d1+2925",   # avg 3000 HP (3x normalized)
        "dam": "1d2+0",

        # Mobs with gold >= this threshold are treated as bosses
        "boss_gold_threshold": 200,
        "boss_hp":  "75d1+4425",  # avg 4500 HP (3x normalized)
        "boss_dam": "1d2+0",
    },
    {
        "file": "area/smurf.are",
        "mode": "normalize",

        "hp":  "75d1+7425",  # avg 7500 HP (5x normalized)
        "dam": "1d2+0",

        "boss_gold_threshold": 40,
        "boss_hp":  "75d1+19925",  # avg 20000 HP (5x normalized)
        "boss_dam": "1d3+0",
    },
    {
        "file": "area/canyon.are",
        "mode": "normalize",

        "hp":  "75d1+24925",  # avg 25000 HP (10x normalized)
        "dam": "1d3+0",

        # Mini-bosses (50000 HP): cyclops, named elemental bosses — gold 1000-41082
        # Major bosses (60000+): guardian, HUGE, Rulers — gold 10000-100000
        # We use tiered boss thresholds via boss_tiers list (overrides boss_gold_threshold)
        "boss_gold_threshold": None,  # disabled — use boss_tiers below
        "boss_hp":  "75d1+49925",
        "boss_dam": "1d4+0",

        # Tiered bosses: list of (min_gold, hp_formula, dam_formula), checked highest first
        "boss_tiers": [
            (50000, "75d1+79925", "1d4+0"),   # avg 80000 HP — Rulers (10x)
            (10000, "75d1+59925", "1d4+0"),   # avg 60000 HP — guardian, HUGE elemental (10x)
            (1000,  "75d1+49925", "1d4+0"),   # avg 50000 HP — mini-bosses (10x)
        ],
    },
]

# Mobs with HP base >= this are immortal/GM mobs — never touched
IMMORTAL_HP_FLOOR = 100000

# ─────────────────────────────────────────────
# HELPERS
# ─────────────────────────────────────────────

HP_DAM_RE = re.compile(
    r'^(\s*\d+\s+-?\d+\s+-?\d+\s+)'   # group 1: hitroll level AC
    r'(\d+)d(\d+)\+(\d+)'              # groups 2-4: HP NdS+P
    r'(\s+)'                            # group 5: space
    r'(\d+)d(\d+)\+(\d+)'              # groups 6-8: DAM NdS+P
    r'(\s*\n)',                         # group 9: trailing newline
    re.MULTILINE
)

GOLD_RE = re.compile(r'^\s*(\d+)\s+\d+\s*$', re.MULTILINE)


def parse_formula(formula):
    """Parse 'NdS+P' into (N, S, P) ints."""
    m = re.match(r'(\d+)d(\d+)\+(\d+)', formula)
    if not m:
        raise ValueError(f"Bad formula: {formula!r}")
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def avg_hp(n, s, p):
    return n * (s + 1) / 2 + p


def normalize_file(cfg):
    path = cfg["file"]
    if not os.path.exists(path):
        print(f"  ERROR: {path} not found.")
        return 0

    with open(path, 'r') as f:
        content = f.read()

    grind_hp  = cfg["hp"]
    grind_dam = cfg["dam"]
    thresh    = cfg.get("boss_gold_threshold")
    boss_hp   = cfg.get("boss_hp", grind_hp)
    boss_dam  = cfg.get("boss_dam", grind_dam)
    tiers     = cfg.get("boss_tiers")  # list of (min_gold, hp, dam)

    # Split content into lines so we can look ahead to the gold line
    lines = content.split('\n')
    changed = 0
    i = 0
    result_lines = []

    while i < len(lines):
        line = lines[i]

        m = HP_DAM_RE.match(line + '\n')
        if m:
            hp_n, hp_s, hp_p = int(m.group(2)), int(m.group(3)), int(m.group(4))
            hp_base = hp_p  # +P is the dominant term in 75d1+X style

            # Skip immortal/GM mobs
            if hp_n * (hp_s + 1) / 2 + hp_p >= IMMORTAL_HP_FLOOR:
                result_lines.append(line)
                i += 1
                continue

            # Look ahead for the gold line (next non-blank line after this one)
            gold = 0
            for j in range(i + 1, min(i + 4, len(lines))):
                gm = re.match(r'^\s*(\d+)\s+\d+\s*$', lines[j])
                if gm:
                    gold = int(gm.group(1))
                    break

            # Determine target HP and damage
            target_hp  = grind_hp
            target_dam = grind_dam

            if tiers:
                for min_g, t_hp, t_dam in sorted(tiers, reverse=True):
                    if gold >= min_g:
                        target_hp  = t_hp
                        target_dam = t_dam
                        break
            elif thresh is not None and gold >= thresh:
                target_hp  = boss_hp
                target_dam = boss_dam

            # Rebuild the line
            prefix = m.group(1)
            space  = m.group(5)
            eol    = m.group(9)
            new_line = f"{prefix}{target_hp}{space}{target_dam}{eol}".rstrip('\n')
            result_lines.append(new_line)
            changed += 1
        else:
            result_lines.append(line)

        i += 1

    new_content = '\n'.join(result_lines)
    with open(path, 'w') as f:
        f.write(new_content)

    return changed


def scale_file(cfg):
    path = cfg["file"]
    if not os.path.exists(path):
        print(f"  ERROR: {path} not found.")
        return 0

    hp_mul_n = cfg.get("hp_mul_n", 1)
    hp_mul_p = cfg.get("hp_mul_p", 1)
    dam_div_n = cfg.get("dam_div_n", 1)
    dam_div_p = cfg.get("dam_div_p", 1)

    with open(path, 'r') as f:
        content = f.read()

    changed = [0]

    def replacer(m):
        hp_n = int(m.group(2)) * hp_mul_n
        hp_s = int(m.group(3))
        hp_p = int(m.group(4)) * hp_mul_p

        # Skip immortal mobs
        if avg_hp(int(m.group(2)), hp_s, int(m.group(4))) >= IMMORTAL_HP_FLOOR:
            return m.group(0)

        dam_n = max(1, int(m.group(6)) // dam_div_n)
        dam_s = int(m.group(7))
        dam_p = int(m.group(8)) // dam_div_p

        changed[0] += 1
        return f"{m.group(1)}{hp_n}d{hp_s}+{hp_p}{m.group(5)}{dam_n}d{dam_s}+{dam_p}{m.group(9)}"

    new_content = HP_DAM_RE.sub(replacer, content)
    with open(path, 'w') as f:
        f.write(new_content)

    return changed[0]


# ─────────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────────

def main():
    print("modify_mobs.py — Mob Stat Editor")
    print("=" * 40)

    for cfg in AREAS:
        path = cfg["file"]
        mode = cfg.get("mode", "normalize")
        print(f"\n[{mode}] {path}")

        if mode == "normalize":
            n = normalize_file(cfg)
        elif mode == "scale":
            n = scale_file(cfg)
        else:
            print(f"  Unknown mode: {mode!r}")
            continue

        if n:
            print(f"  Updated {n} mob stat lines.")
        else:
            print(f"  No changes made (check file path or mob format).")

    print("\nDone.")


if __name__ == "__main__":
    main()
