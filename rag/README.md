# Dystopia Help RAG

Retrieval-Augmented Generation over `area/help.are`. Players ask a question,
we find the most relevant help entries via semantic search, then feed them
to a local LLM as grounded context.

## Setup (run in WSL)

```bash
cd /mnt/c/Users/jared/OneDrive/Documents/dystopia/dystopia-mud
python3 -m venv rag/.venv
source rag/.venv/bin/activate
pip install -r rag/requirements.txt
```

First run of `ingest.py`/`query.py` will download the `bge-small-en-v1.5`
embedding model (~130 MB) to `~/.cache/huggingface/`.

## Configuration

Copy `rag/.env.example` to `rag/.env` and fill in values. The server and
scripts load `rag/.env` automatically. `rag/.env` is gitignored -- never
commit it, and never paste tokens into chat.

Important: `query.py` and one-off scripts each pay a multi-second cold
start to load the embedder. For interactive use, run `server.py` instead
(below) -- it loads the model once and answers requests in milliseconds.

## Pipeline

1. **Parse** — extract help entries from `area/help.are` into JSONL.
   ```bash
   python3 rag/parse_help.py
   ```
2. **Ingest** — embed entries and store in a local Chroma DB.
   ```bash
   python3 rag/ingest.py
   ```
3. **Serve** — long-lived server. Loads the embedder once.
   ```bash
   python3 rag/server.py
   # then, from anywhere:
   curl -s -X POST http://127.0.0.1:8765/ask \
        -d '{"question":"how does pkill work","llm":false}'
   ```

   Request body fields:
   - `question` (required, string)
   - `k` (optional int, default 5) — number of help entries to retrieve
   - `llm` (optional bool, default true) — if false, skip LLM and return hits only

4. **One-shot CLI** — `query.py` is still available for debugging retrieval
   without running the server:
   ```bash
   python3 rag/query.py "how do I forge weapons"
   python3 rag/query.py --llm "how do I forge weapons"
   ```

## Going offline

After the first successful `ingest.py` run, the embedding model is cached
in `~/.cache/huggingface/`. Set these in `rag/.env` to skip all Hub calls
(faster startup, no rate-limit warnings):

```
HF_HUB_OFFLINE=1
TRANSFORMERS_OFFLINE=1
```

## Local LLM endpoint

The server POSTs to an OpenAI-compatible chat endpoint. Defaults:

- URL: `http://localhost:8080/v1/chat/completions` (override `RAG_LLM_URL`)
- Model: `local-model` (override `RAG_LLM_MODEL`)

Works with llama.cpp's `./server`, LM Studio, vLLM, and Ollama's
OpenAI-compatible endpoint at `/v1/chat/completions`.

## Re-indexing

Re-run `parse_help.py` then `ingest.py` whenever `area/help.are` changes.
`ingest.py` deletes and recreates the collection, so it is idempotent.

## What's indexed

- Only entries with `level <= 0` (player-visible). Immortal-only entries
  (positive levels) are skipped; edit the filter in `ingest.py` if you
  want them included for staff use.
- Color codes (`#R`, `#n`, `##`) are stripped from the embedded text so
  they don't pollute the vectors.
