import re
import os

with open("../area/smurf.are", "r", encoding="latin-1", errors="replace") as f:
    text = f.read()

# find mobiles block
mobs_match = re.search(r"^#MOBILES[\r\n]+(.*?)(?:^#|\Z)", text, re.MULTILINE | re.DOTALL)
mobs = re.findall(r"^#(\d+)", mobs_match.group(1), re.MULTILINE) if mobs_match else []

# find rooms block
rooms_match = re.search(r"^#(?:ROOMS|ROOMDATA)[\r\n]+(.*?)(?:^#|\Z)", text, re.MULTILINE | re.DOTALL | re.IGNORECASE)
rooms_text = rooms_match.group(1) if rooms_match else ""
rooms = re.findall(r"^#(\d+)", rooms_text, re.MULTILINE) if rooms_text else []

# find resets block
resets_match = re.search(r"^#RESETS[\r\n]+(.*?)(?:^#|\Z)", text, re.MULTILINE | re.DOTALL)
resets_text = resets_match.group(1) if resets_match else ""

print(f"Smurfs: {len(mobs)} mobs, {len(rooms)} rooms, resets length: {len(resets_text)}")
