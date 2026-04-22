"""Long-lived RAG server for Dystopia help.

Loads the embedder and Chroma collection once at startup, then serves
queries over HTTP. Subsequent queries are milliseconds instead of seconds.

Environment (read from rag/.env if present, otherwise process env):
    HF_TOKEN            Hugging Face token for model downloads (optional).
    HF_HUB_OFFLINE=1    Skip all Hub calls after the model is cached.
    RAG_LLM_URL         OpenAI-compatible chat endpoint for the local LLM.
    RAG_LLM_MODEL       Model name to send to the LLM endpoint.
    RAG_HOST, RAG_PORT  Bind address (default 127.0.0.1:8765).

Endpoints:
    GET  /health                  -> {"status": "ok"}
    POST /ask  {"question": ...}  -> {"hits": [...], "answer": "..."}
         Optional body fields: "k" (int, default 5), "llm" (bool, default true)
"""
from __future__ import annotations

import json
import os
import sys
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import requests


def load_dotenv(path: Path) -> None:
    """Minimal .env loader -- no external dependency."""
    if not path.exists():
        return
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        # .env does not override values already set in the process env.
        os.environ.setdefault(key, value)


# Load .env BEFORE importing libraries that read HF_* at import time.
ENV_FILE = Path(__file__).parent / ".env"
load_dotenv(ENV_FILE)

import chromadb  # noqa: E402
from sentence_transformers import SentenceTransformer  # noqa: E402


EMBED_MODEL = "BAAI/bge-small-en-v1.5"
COLLECTION = "dystopia_help"
DB_DIR = Path(__file__).parent / "chroma_db"

LLM_URL = os.environ.get("RAG_LLM_URL", "http://localhost:8003/v1/chat/completions")
LLM_MODEL = os.environ.get("RAG_LLM_MODEL", "gemma-4-e4b")
HOST = os.environ.get("RAG_HOST", "127.0.0.1")
PORT = int(os.environ.get("RAG_PORT", "8765"))

SYSTEM_PROMPT = """You are an in-game help assistant for Dystopia, a godwars-derived MUD.
Answer the player's question using ONLY the sources provided in the context. Sources
may be in-game help entries or supplementary docs -- treat both as authoritative.
If the context does not contain the answer, say so plainly -- do not invent commands,
stats, or mechanics. Cite the source title (help topic or doc section) when useful.
Be concise."""


class RagState:
    """Loaded once at startup; shared across requests."""

    def __init__(self) -> None:
        print(f"[rag] Loading embedder: {EMBED_MODEL}", flush=True)
        self.embedder = SentenceTransformer(EMBED_MODEL)
        print(f"[rag] Opening Chroma DB at {DB_DIR}", flush=True)
        client = chromadb.PersistentClient(path=str(DB_DIR))
        self.collection = client.get_collection(COLLECTION)
        print(f"[rag] Ready -- {self.collection.count()} entries indexed.", flush=True)

    def retrieve(self, question: str, k: int) -> list[dict]:
        qv = self.embedder.encode([question], normalize_embeddings=True)[0].tolist()
        res = self.collection.query(query_embeddings=[qv], n_results=k)
        return [
            {"document": doc, "metadata": meta, "distance": dist}
            for doc, meta, dist in zip(res["documents"][0], res["metadatas"][0], res["distances"][0])
        ]


def hit_label(hit: dict) -> str:
    """Human-readable label for a retrieved chunk.

    Help entries carry `primary`; doc chunks carry `doc` + `section`.
    Falls back to the source field for anything unexpected.
    """
    meta = hit.get("metadata") or {}
    kind = meta.get("kind")
    if kind == "help" or "primary" in meta:
        return f"Help: {meta['primary']}"
    if kind == "doc" or "doc" in meta:
        doc = meta.get("doc", "doc")
        section = meta.get("section")
        return f"Doc: {doc}" + (f" — {section}" if section else "")
    return meta.get("source", "chunk")


def build_messages(question: str, hits: list[dict]) -> list[dict]:
    blocks = [
        f"--- Source {i}: {hit_label(h)} ---\n{h['document']}"
        for i, h in enumerate(hits, 1)
    ]
    user = f"Context from Dystopia helpfiles and docs:\n\n" + "\n\n".join(blocks) + f"\n\nQuestion: {question}"
    return [{"role": "system", "content": SYSTEM_PROMPT}, {"role": "user", "content": user}]


def ask_llm(messages: list[dict]) -> str:
    resp = requests.post(
        LLM_URL,
        json={"model": LLM_MODEL, "messages": messages, "temperature": 0.2, "stream": False},
        timeout=120,
    )
    resp.raise_for_status()
    return resp.json()["choices"][0]["message"]["content"]


class Handler(BaseHTTPRequestHandler):
    state: RagState  # populated before serve_forever

    def log_message(self, format: str, *args) -> None:
        sys.stderr.write(f"[rag] {self.address_string()} - {format % args}\n")

    def _json(self, status: int, body: dict) -> None:
        payload = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        if self.path == "/health":
            self._json(200, {"status": "ok", "count": self.state.collection.count()})
            return
        self._json(404, {"error": "not found"})

    def do_POST(self) -> None:
        if self.path != "/ask":
            self._json(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length) or b"{}")
            question = (body.get("question") or "").strip()
            if not question:
                self._json(400, {"error": "missing 'question'"})
                return
            k = int(body.get("k", 5))
            use_llm = bool(body.get("llm", True))

            hits = self.state.retrieve(question, k)
            response: dict = {
                "hits": [
                    {
                        "label": hit_label(h),
                        "kind": (h.get("metadata") or {}).get("kind", "help"),
                        "primary": (h.get("metadata") or {}).get("primary", ""),
                        "keywords": (h.get("metadata") or {}).get("keywords", ""),
                        "doc": (h.get("metadata") or {}).get("doc", ""),
                        "section": (h.get("metadata") or {}).get("section", ""),
                        "source": (h.get("metadata") or {}).get("source", ""),
                        "distance": h["distance"],
                    }
                    for h in hits
                ]
            }
            if use_llm:
                try:
                    response["answer"] = ask_llm(build_messages(question, hits))
                except requests.exceptions.RequestException as e:
                    response["answer"] = None
                    response["llm_error"] = f"{type(e).__name__}: {e}"
            self._json(200, response)
        except Exception as e:
            traceback.print_exc()
            self._json(500, {"error": f"{type(e).__name__}: {e}"})


def main() -> None:
    Handler.state = RagState()
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"[rag] Listening on http://{HOST}:{PORT}", flush=True)
    print(f"[rag] Try: curl -s -X POST http://{HOST}:{PORT}/ask "
          f"-d '{{\"question\":\"how does pkill work\",\"llm\":false}}'", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[rag] shutting down", flush=True)


if __name__ == "__main__":
    main()
