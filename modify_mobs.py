import re
import os

# --- CONFIGURATION START ---

# List of area files to modify
# You can add or remove areas from this list easily.
AREA_FILES = [
    "area/school.are",
    "area/smurf.are",
    "area/canyon.are"
]

# Modifiers for Mob Health (Hit points)
# NdS+P will become (N * HP_MULTI_N)d(S)+(P * HP_MULTI_P)
HP_MULTI_N = 5
HP_MULTI_P = 5

# Modifiers for Mob Damage
# NdS+P will become (N // DAM_DIV_N)d(S)+(P // DAM_DIV_P)
# Minimum dice number is 1, so 1d4 halved becomes 1d4.
DAM_DIV_N = 2
DAM_DIV_P = 2

# --- CONFIGURATION END ---

def process_file(filepath):
    print(f"Processing: {filepath}")
    
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found.")
        return False
        
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Regex to find the hit dice and dam dice lines in a standard Merc mob format
    # Example format:
    # 8 16 8 10d5+15 1d6+1
    # 100 0
    # Group matched:
    # (1: prefix) (2: N)d(3: S)+(4: P) (5: space) (6: N)d(7: S)+(8: P)\n
    # (9: gold)(10: space+0\n)
    
    pattern = re.compile(
        r'^(\d+\s+-?\d+\s+-?\d+\s+)(\d+)d(\d+)\+(\d+)(\s+)(\d+)d(\d+)\+(\d+)\n'
        r'(\d+)(\s+\d+\n)',
        re.MULTILINE
    )
    
    modifications = 0
    
    def replacer(match):
        nonlocal modifications
        modifications += 1
        prefix = match.group(1)
        
        # HP Calculation
        h_nodice = int(match.group(2)) * HP_MULTI_N
        h_sizedice = int(match.group(3))
        h_plus = int(match.group(4)) * HP_MULTI_P
        space1 = match.group(5)
        
        # Damage Calculation
        d_nodice = max(1, int(match.group(6)) // DAM_DIV_N)
        d_sizedice = int(match.group(7))
        d_plus = int(match.group(8)) // DAM_DIV_P
        
        gold = match.group(9) # Gold remains unaffected
        suffix = match.group(10)
        
        return f"{prefix}{h_nodice}d{h_sizedice}+{h_plus}{space1}{d_nodice}d{d_sizedice}+{d_plus}\n{gold}{suffix}"

    new_content = pattern.sub(replacer, content)
    
    if modifications > 0:
        with open(filepath, 'w') as f:
            f.write(new_content)
        print(f"Successfully updated {modifications} mobs in {filepath}.")
        return True
    else:
        print(f"No match found or no modifications made in {filepath}.")
        return False

def main():
    print("Starting Mob Stat Modification Script...")
    print(f"HP Multiplier: {HP_MULTI_N}x")
    print(f"Damage Divisor: / {DAM_DIV_N}")
    success_count = 0
    
    for area in AREA_FILES:
        if process_file(area):
            success_count += 1
            
    print(f"Finished. Modified {success_count}/{len(AREA_FILES)} files.")

if __name__ == "__main__":
    main()
