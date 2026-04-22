"""Embed help entries and markdown docs, store them in a Chroma vector DB.

Indexes two sources into one collection:
  1. Parsed help entries from `rag/help_entries.jsonl` (see parse_help.py).
  2. All `*.md` files under `rag/doc/`, chunked by H2 section.

Run after parse_help.py. Requires:
    pip install chromadb sentence-transformers
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

import chromadb
from sentence_transformers import SentenceTransformer


EMBED_MODEL = "BAAI/bge-small-en-v1.5"  # 384-dim, fast on CPU
COLLECTION = "dystopia_help"


def render_for_embedding(entry: dict) -> str:
    """Turn a help entry into text optimized for semantic search.

    We prepend the aliases so keyword queries ('forge', 'pkill') bias
    retrieval toward their entry, and include the body for meaning.
    """
    aliases = ", ".join(entry["keywords"])
    return f"Help topic: {aliases}\n\n{entry['body_clean']}"


H2_SPLIT = re.compile(r"^(?=## )", re.MULTILINE)


def chunk_markdown(path: Path) -> list[dict]:
    """Split a markdown file into one chunk per H2 section.

    The preamble (everything before the first H2, including the H1 title) is
    emitted as its own chunk so the top-of-file context is retrievable. Each
    chunk is prefixed with the doc title so embeddings keep that context.
    """
    raw = path.read_text(encoding="utf-8")
    # Pull the H1 title if present; falls back to filename stem.
    title_match = re.search(r"^#\s+(.+)$", raw, re.MULTILINE)
    title = title_match.group(1).strip() if title_match else path.stem

    parts = [p.strip() for p in H2_SPLIT.split(raw) if p.strip()]
    chunks: list[dict] = []
    for idx, part in enumerate(parts):
        heading_match = re.match(r"^##\s+(.+)$", part, re.MULTILINE)
        section = heading_match.group(1).strip() if heading_match else "(intro)"
        text = f"Doc: {title} — {section}\n\n{part}"
        chunks.append(
            {
                "id": f"doc:{path.stem}:{idx}",
                "text": text,
                "metadata": {
                    "source": str(path.as_posix()),
                    "doc": title,
                    "section": section,
                    "kind": "doc",
                },
            }
        )
    return chunks


def load_help_chunks(src: Path) -> list[dict]:
    entries = [json.loads(line) for line in src.read_text(encoding="utf-8").splitlines() if line.strip()]
    # Only index entries players can see (level <= 0 in this codebase's convention).
    player_visible = [e for e in entries if e["level"] <= 0]
    print(f"Loaded {len(entries)} help entries; indexing {len(player_visible)} player-visible.")
    chunks = []
    for i, e in enumerate(player_visible):
        chunks.append(
            {
                "id": f"help:{i}:{e['primary']}",
                "text": render_for_embedding(e),
                "metadata": {
                    "primary": e["primary"],
                    "keywords": " ".join(e["keywords"]),
                    "level": e["level"],
                    "source": "area/help.are",
                    "kind": "help",
                },
            }
        )
    return chunks


def load_doc_chunks(doc_dir: Path) -> list[dict]:
    if not doc_dir.is_dir():
        print(f"No doc directory at {doc_dir}; skipping markdown ingest.")
        return []
    md_files = sorted(doc_dir.rglob("*.md"))
    chunks: list[dict] = []
    for md in md_files:
        chunks.extend(chunk_markdown(md))
    print(f"Loaded {len(chunks)} markdown chunks from {len(md_files)} files under {doc_dir}/.")
    return chunks


def main() -> None:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("rag/help_entries.jsonl")
    db_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("rag/chroma_db")
    doc_dir = Path(sys.argv[3]) if len(sys.argv) > 3 else Path("rag/doc")

    chunks = load_help_chunks(src) + load_doc_chunks(doc_dir)
    if not chunks:
        print("Nothing to index.")
        return

    print(f"Loading embedder: {EMBED_MODEL}")
    model = SentenceTransformer(EMBED_MODEL)

    texts = [c["text"] for c in chunks]
    print(f"Embedding {len(texts)} chunks...")
    vectors = model.encode(texts, batch_size=32, show_progress_bar=True, normalize_embeddings=True)

    client = chromadb.PersistentClient(path=str(db_dir))
    # Reset collection so re-runs are idempotent.
    try:
        client.delete_collection(COLLECTION)
    except Exception:
        pass
    coll = client.create_collection(COLLECTION, metadata={"hnsw:space": "cosine"})

    coll.add(
        ids=[c["id"] for c in chunks],
        embeddings=vectors.tolist(),
        documents=texts,
        metadatas=[c["metadata"] for c in chunks],
    )
    print(f"Stored {coll.count()} chunks in {db_dir}/ (collection: {COLLECTION})")


if __name__ == "__main__":
    main()
