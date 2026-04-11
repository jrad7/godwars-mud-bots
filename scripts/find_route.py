#!/usr/bin/env python3
"""
Find shortest paths from room 3001 to target zone entrances.
Parses all .are files, builds a room graph, runs BFS.
"""

import os
import re
from collections import deque

AREA_DIR = os.path.join(os.path.dirname(__file__), '..', 'dystopia-mud', 'area')

DIRS = ['north', 'east', 'south', 'west', 'up', 'down']

def parse_areas(area_dir):
    """Parse all .are files and return a dict of {vnum: {dir_idx: dest_vnum}}."""
    graph = {}

    for fname in os.listdir(area_dir):
        if not fname.endswith('.are'):
            continue
        path = os.path.join(area_dir, fname)
        with open(path, 'r', errors='replace') as f:
            lines = f.readlines()

        in_rooms = False
        current_room = None
        i = 0
        while i < len(lines):
            line = lines[i].rstrip()

            if line == '#ROOMDATA':
                in_rooms = True
                i += 1
                continue

            # Stop at next section header
            if in_rooms and line.startswith('#') and line != '#ROOMDATA':
                m = re.match(r'^#(\d+)$', line)
                if m:
                    vnum = int(m.group(1))
                    if vnum == 0:
                        in_rooms = False
                    else:
                        current_room = vnum
                        if current_room not in graph:
                            graph[current_room] = {}
                elif line in ('#MOBILES','#OBJECTS','#SPECIALS','#RESETS',
                              '#SHOPS','#END','#$','#HELPS'):
                    in_rooms = False
                i += 1
                continue

            if not in_rooms or current_room is None:
                i += 1
                continue

            # Detect door: "D0" through "D5"
            dm = re.match(r'^D([0-5])$', line)
            if dm:
                dir_idx = int(dm.group(1))
                # Skip desc~ line(s) until we find the keyword~ line
                # then the exit flags line: "lock_flags exit_to key_vnum"
                # Format: skip lines until we find a line matching 3 integers
                i += 1
                # skip desc (may span multiple lines ending with ~)
                while i < len(lines) and '~' not in lines[i]:
                    i += 1
                i += 1  # skip the ~ line itself
                # skip keyword line (ends with ~)
                while i < len(lines) and '~' not in lines[i]:
                    i += 1
                i += 1  # skip keyword ~ line
                # Now read the exit flags line
                if i < len(lines):
                    flag_line = lines[i].strip()
                    parts = flag_line.split()
                    if len(parts) >= 3:
                        try:
                            dest = int(parts[2])  # format: exit_flags key_vnum to_room
                            if dest > 0:
                                graph[current_room][dir_idx] = dest
                        except ValueError:
                            pass
                i += 1
                continue

            i += 1

    return graph


def bfs(graph, start, target_vnums):
    """BFS from start to each target vnum. Returns dict {vnum: [commands]}."""
    results = {}
    visited = {start: None}  # vnum -> (prev_vnum, dir_name)
    queue = deque([start])

    remaining = set(target_vnums)

    while queue and remaining:
        current = queue.popleft()

        if current in remaining:
            # Reconstruct path
            path = []
            node = current
            while visited[node] is not None:
                prev, direction = visited[node]
                path.append(direction)
                node = prev
            path.reverse()
            results[current] = path
            remaining.discard(current)

        for dir_idx, dest in graph.get(current, {}).items():
            if dest not in visited:
                visited[dest] = (current, DIRS[dir_idx])
                queue.append(dest)

    return results


def first_room_in_zone(graph, vnum_range):
    """Find the lowest vnum in the graph within the given range."""
    lo, hi = vnum_range
    rooms = [v for v in graph if lo <= v <= hi]
    return min(rooms) if rooms else None


if __name__ == '__main__':
    print("Parsing area files...")
    graph = parse_areas(AREA_DIR)
    print(f"Loaded {len(graph)} rooms.")

    # Use specific entry-point rooms (closest to midgaard) rather than lowest VNUMs
    zone_rooms = {
        'MORIA':    3900,   # West trail, reachable via west gate
        'SEWER':    7030,   # The Dump (3030) -> down
        'ARACHNOS': 6200,   # Via HAON
        'THALOS':   5200,   # Via MIDENNIR
        'PLAINS':   300,    # Via MORIA
        'JUARGAN':  4700,   # Via MORIA east
        'MARSH':    8301,   # Via HAON
        'HITOWER':  1300,   # Via HAON
    }
    for name, vnum in zone_rooms.items():
        if vnum in graph:
            print(f"  {name}: target room = {vnum}")
        else:
            print(f"  {name}: room {vnum} NOT IN GRAPH")

    print("\nRunning BFS from room 3001...")
    target_vnums = list(zone_rooms.values())
    results = bfs(graph, 3001, target_vnums)

    # Reverse lookup vnum->name
    vnum_to_name = {v: k for k, v in zone_rooms.items()}

    print("\n=== ROUTES FROM RECALL (3001) ===\n")
    for vnum, cmds in results.items():
        name = vnum_to_name.get(vnum, str(vnum))
        cmd_str = ', '.join(f'"{c}"' for c in cmds)
        c_array = '{ "recall", ' + ', '.join(f'"{c}"' for c in cmds) + ', NULL }'
        print(f"{name} (room {vnum}):")
        print(f"  Steps: {cmds}")
        print(f"  C array: {c_array}")
        print()

    # Report any zones not reachable
    found_vnums = set(results.keys())
    for name, vnum in zone_rooms.items():
        if vnum not in found_vnums:
            print(f"WARNING: {name} (room {vnum}) NOT REACHABLE from 3001")
