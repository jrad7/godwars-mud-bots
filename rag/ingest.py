"""Embed help entries and store them in a Chroma vector DB.

Run after parse_help.py. Requires:
    pip install chromadb sentence-transformers
"""
from __future__ import annotations

import json
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


def main() -> None:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("rag/help_entries.jsonl")
    db_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("rag/chroma_db")

    entries = [json.loads(line) for line in src.read_text(encoding="utf-8").splitlines() if line.strip()]
    # Only index entries players can see (level <= 0 in this codebase's convention).
    # Adjust the filter if you also want immortal-only help indexed.
    player_visible = [e for e in entries if e["level"] <= 0]
    print(f"Loaded {len(entries)} entries; indexing {len(player_visible)} player-visible.")

    print(f"Loading embedder: {EMBED_MODEL}")
    model = SentenceTransformer(EMBED_MODEL)

    texts = [render_for_embedding(e) for e in player_visible]
    print("Embedding...")
    vectors = model.encode(texts, batch_size=32, show_progress_bar=True, normalize_embeddings=True)

    client = chromadb.PersistentClient(path=str(db_dir))
    # Reset collection so re-runs are idempotent.
    try:
        client.delete_collection(COLLECTION)
    except Exception:
        pass
    coll = client.create_collection(COLLECTION, metadata={"hnsw:space": "cosine"})

    coll.add(
        ids=[f"help:{i}:{e['primary']}" for i, e in enumerate(player_visible)],
        embeddings=vectors.tolist(),
        documents=texts,
        metadatas=[
            {
                "primary": e["primary"],
                "keywords": " ".join(e["keywords"]),
                "level": e["level"],
                "source": "area/help.are",
            }
            for e in player_visible
        ],
    )
    print(f"Stored {coll.count()} chunks in {db_dir}/ (collection: {COLLECTION})")


if __name__ == "__main__":
    main()
