"""Parse area/help.are into structured help entries.

Format (matches load_helps in src/db.c):
    <level:int> <keyword...>~
    <body text...>~
    ...
    0 $~

Keywords may contain multiple space-separated aliases.
Level -1 means 'visible to all'; positive levels are immortal-only.
"""
from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass, asdict
from pathlib import Path


COLOR_CODE = re.compile(r"#[a-zA-Z0-9]")


@dataclass
class HelpEntry:
    level: int
    keywords: list[str]
    primary: str
    body: str
    body_clean: str


def strip_color(text: str) -> str:
    """Strip Dystopia color codes like #R, #n, ## (literal hash)."""
    text = text.replace("##", "\x00")
    text = COLOR_CODE.sub("", text)
    return text.replace("\x00", "#")


def _read_until_tilde(chars: list[str], i: int) -> tuple[str, int]:
    start = i
    while i < len(chars) and chars[i] != "~":
        i += 1
    return "".join(chars[start:i]), i + 1


def parse_help_file(path: Path) -> list[HelpEntry]:
    raw = path.read_text(encoding="latin-1")
    # Strip the #HELPS header and anything after the terminator.
    if "#HELPS" in raw:
        raw = raw.split("#HELPS", 1)[1]
    chars = list(raw)
    entries: list[HelpEntry] = []
    i = 0
    n = len(chars)

    while i < n:
        # Skip whitespace.
        while i < n and chars[i] in " \t\r\n":
            i += 1
        if i >= n:
            break

        # Read integer level.
        start = i
        if chars[i] in "+-":
            i += 1
        while i < n and chars[i].isdigit():
            i += 1
        level_str = "".join(chars[start:i])
        if not level_str or level_str in ("+", "-"):
            break
        level = int(level_str)

        # Skip whitespace before keyword.
        while i < n and chars[i] in " \t":
            i += 1

        keyword_raw, i = _read_until_tilde(chars, i)
        keyword_raw = keyword_raw.strip()

        if keyword_raw.startswith("$"):
            break  # end marker: "0 $~"

        # Skip whitespace/newlines before body.
        while i < n and chars[i] in " \t\r\n":
            i += 1

        body, i = _read_until_tilde(chars, i)

        if not keyword_raw:
            continue  # empty keyword entries are dropped by the server

        keywords = keyword_raw.split()
        entries.append(
            HelpEntry(
                level=level,
                keywords=keywords,
                primary=keywords[0],
                body=body,
                body_clean=strip_color(body).strip(),
            )
        )

    return entries


def main() -> None:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("area/help.are")
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("rag/help_entries.jsonl")
    entries = parse_help_file(src)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as f:
        for e in entries:
            f.write(json.dumps(asdict(e)) + "\n")
    print(f"Parsed {len(entries)} entries from {src} -> {out}")
    # Sanity: show a couple.
    for e in entries[:3]:
        preview = e.body_clean[:60].replace("\n", " ")
        print(f"  [{e.level}] {e.primary!r} ({len(e.keywords)} aliases): {preview}...")


if __name__ == "__main__":
    main()
