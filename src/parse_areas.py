import os
import random
import re

areas = ["canyon.are", "shire.are", "hell.are", "smurf.are", "heaven.are", "school.are"]

for area in areas:
    path = f"../area/{area}"
    if not os.path.exists(path):
        print(f"Skipping {area}, not found.")
        continue
    with open(path, "r", encoding="latin-1") as f:
        content = f.read()
    
    # Simple extraction of vnums for mobs
    mobs = re.findall(r"^#MOBILES(.*?)^#", content, re.MULTILINE | re.DOTALL)
    mob_vnums = []
    if mobs:
        mob_vnums = re.findall(r"^#(\d+)", mobs[0], re.MULTILINE)
        
    rooms = re.findall(r"^#ROOM(?:S|DATA|S)\b(.*?)^#", content, re.MULTILINE | re.DOTALL | re.IGNORECASE)
    room_vnums = []
    if rooms:
        room_vnums = re.findall(r"^#(\d+)", rooms[0], re.MULTILINE)
        
    print(f"{area}: {len(mob_vnums)} mobs, {len(room_vnums)} rooms")
    
