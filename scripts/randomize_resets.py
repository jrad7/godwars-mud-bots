import sys
import re
import random
import os

def process_file(filepath):
    if not os.path.exists(filepath):
        print(f"File {filepath} not found.")
        return

    with open(filepath, "r", encoding="latin-1") as f:
        text = f.read()

    # Find the #MOBILES section and extract all mob VNUMs.
    mobs_match = re.search(r"^#MOBILES[\r\n]+(.*?)(?:^#|\Z)", text, re.MULTILINE | re.DOTALL)
    mobs_text = mobs_match.group(1) if mobs_match else ""
    mob_vnums = []
    # Identify #VNUM lines in mobiles (e.g. #101)
    for line in mobs_text.splitlines():
        if line.startswith("#"):
            vnum_str = line[1:].strip()
            if vnum_str.isdigit():
                mob_vnums.append(vnum_str)

    # Find the #ROOMDATA or #ROOMS section to extract room VNUMs
    rooms_match = re.search(r"^#(?:ROOMS|ROOMDATA)[\r\n]+(.*?)(?:^#|\Z)", text, re.MULTILINE | re.DOTALL | re.IGNORECASE)
    rooms_text = rooms_match.group(1) if rooms_match else ""
    room_vnums = []
    for line in rooms_text.splitlines():
        if line.startswith("#"):
            vnum_str = line[1:].strip()
            if vnum_str.isdigit():
                room_vnums.append(vnum_str)

    if not mob_vnums or not room_vnums:
        print(f"Skipping {filepath}: Found {len(mob_vnums)} mobs and {len(room_vnums)} rooms.")
        return

    # Find the #RESETS section and rewrite it
    resets_match = re.search(r"(^#RESETS[\r\n]+)(.*?)(?:^#|\Z)", text, re.MULTILINE | re.DOTALL)
    if not resets_match:
        print(f"Skipping {filepath}: No #RESETS section found.")
        return
        
    resets_header = resets_match.group(1)
    resets_body = resets_match.group(2)
    next_section = text[resets_match.end(2):]
    
    # Process resets body (keep original non-mob, non-equip resets)
    new_resets = []
    for line in resets_body.splitlines():
        if not line.strip():
            continue
        if line.startswith('S'): # end of resets marker usually
            continue
        # Skip generic mobs, equip, and gives.
        if line.startswith('M') or line.startswith('E') or line.startswith('G'):
            continue
        new_resets.append(line)
        
    # Generate 5 random M resets per room
    new_resets.append("")
    for rm_vnum in room_vnums:
        for _ in range(5):
            random_mob = random.choice(mob_vnums)
            new_resets.append(f"M 0 {random_mob} 200 {rm_vnum}")
            
    new_resets.append("S")
    
    resets_str = resets_header + "\n".join(new_resets) + "\n\n"
    new_text = text[:resets_match.start(1)] + resets_str + next_section
    
    # Write back
    with open(filepath, "w", encoding="latin-1", newline='\n') as f:
        f.write(new_text)
        
    print(f"Successfully randomized resets for {filepath} ({len(mob_vnums)} mobs, {len(room_vnums)} rooms).")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python randomize_resets.py <file1.are> [file2.are ...]")
    else:
        for fname in sys.argv[1:]:
            process_file(fname)
