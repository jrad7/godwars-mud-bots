"""Query the help RAG index and (optionally) ask a local LLM to answer.

Usage:
    python3 rag/query.py "how does the forge work"              # retrieval only
    python3 rag/query.py --llm "how does the forge work"        # retrieve + generate

The --llm path expects an OpenAI-compatible server at RAG_LLM_URL
(defaults to http://localhost:8080/v1/chat/completions -- works for
llama.cpp server, LM Studio, vLLM, and Ollama with openai compat).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

import chromadb
import requests
from sentence_transformers import SentenceTransformer


EMBED_MODEL = "BAAI/bge-small-en-v1.5"
COLLECTION = "dystopia_help"
LLM_URL = os.environ.get("RAG_LLM_URL", "http://localhost:8003/v1/chat/completions")
LLM_MODEL = os.environ.get("RAG_LLM_MODEL", "gemma-4-e4b")

SYSTEM_PROMPT = """You are an in-game help assistant for Dystopia, a godwars-derived MUD.
Answer the player's question using ONLY the help entries provided in the context.
If the context does not contain the answer, say so plainly -- do not invent commands,
stats, or mechanics. Quote the relevant help topic names when useful. Be concise."""


def retrieve(question: str, db_dir: Path, k: int = 5) -> list[dict]:
    model = SentenceTransformer(EMBED_MODEL)
    qv = model.encode([question], normalize_embeddings=True)[0].tolist()
    client = chromadb.PersistentClient(path=str(db_dir))
    coll = client.get_collection(COLLECTION)
    res = coll.query(query_embeddings=[qv], n_results=k)
    hits = []
    for doc, meta, dist in zip(res["documents"][0], res["metadatas"][0], res["distances"][0]):
        hits.append({"document": doc, "metadata": meta, "distance": dist})
    return hits


def build_prompt(question: str, hits: list[dict]) -> list[dict]:
    context_blocks = []
    for i, h in enumerate(hits, 1):
        context_blocks.append(f"--- Help entry {i}: {h['metadata']['primary']} ---\n{h['document']}")
    context = "\n\n".join(context_blocks)
    user = f"Context from Dystopia helpfiles:\n\n{context}\n\nQuestion: {question}"
    return [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user},
    ]


def ask_llm(messages: list[dict]) -> str:
    resp = requests.post(
        LLM_URL,
        json={"model": LLM_MODEL, "messages": messages, "temperature": 0.2, "stream": False},
        timeout=120,
    )
    resp.raise_for_status()
    data = resp.json()
    return data["choices"][0]["message"]["content"]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("question", nargs="+")
    ap.add_argument("--llm", action="store_true", help="Send to local LLM for a synthesized answer.")
    ap.add_argument("--k", type=int, default=5)
    ap.add_argument("--db", type=Path, default=Path("rag/chroma_db"))
    args = ap.parse_args()

    q = " ".join(args.question)
    hits = retrieve(q, args.db, args.k)

    print(f"\n=== Top {len(hits)} retrieved entries for: {q!r} ===")
    for i, h in enumerate(hits, 1):
        preview = h["document"].split("\n\n", 1)[-1][:120].replace("\n", " ")
        print(f"  {i}. [{h['distance']:.3f}] {h['metadata']['primary']}: {preview}...")

    if args.llm:
        print("\n=== LLM answer ===")
        messages = build_prompt(q, hits)
        try:
            print(ask_llm(messages))
        except requests.exceptions.RequestException as e:
            print(f"[LLM call failed: {e}]", file=sys.stderr)
            print(f"Is your local LLM running at {LLM_URL}?", file=sys.stderr)
            sys.exit(1)


if __name__ == "__main__":
    main()
