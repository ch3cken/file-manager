# NLP Module – Technical Summary

**Owner:** Antonio  
**Assignment:** NLP (sentence embedding engine)  
**Module files:**

| File | Purpose |
|------|---------|
| `include/embedder.hpp` | Public C++ interface |
| `src/search/embedder.cpp` | C++ implementation |
| `tests/test_embedder.cpp` | Unit tests |
| `python-service/embedder.py` | Python reference / accuracy validation |
| `python-service/requirements.txt` | Python dependencies |

---

## How teammates use this module

Use the flat public header from `include/`:

```cpp
#include "embedder.hpp"

nlp::Embedder::Options options;
options.modelPath = "models/all-MiniLM-L6-v2.onnx";
options.vocabPath = "models/vocab.txt";

nlp::Embedder embedder(options);

std::vector<float> fileVector = embedder.embedFile(filePath);
std::vector<float> queryVector = embedder.embedText(userQuery);
```

Both calls return a `std::vector<float>` with exactly 384 values. The vector is
already L2-normalised, so cosine similarity can be computed as a dot product.
Store `fileVector` in Enrique's `files.embedding` column and pass `queryVector`
to Ahmad's smart/vector search layer.

---

## 1. What the NLP module does (and what it does NOT)

The NLP module is solely responsible for converting raw text into dense
numerical vectors (embeddings). It does **not** parse queries, run searches,
or manage the database — those belong to Murad and Ahmad respectively.

```
                         ┌─────────────────────────┐
  file content  ──────►  │     nlp::Embedder        │  ──►  float[384]  ──►  DB BLOB   (index time)
  search query  ──────►  │  (Antonio – NLP module)  │  ──►  float[384]  ──►  VectorSearch (query time)
                         └─────────────────────────┘
```

---

## 2. Tools and libraries used

### C++ (production)

| Library | Version | Role |
|---------|---------|------|
| **ONNX Runtime** (`onnxruntime_cxx_api.h`) | ≥ 1.16 | Runs the transformer model locally on CPU — satisfies REQ-3.3.2 (Local NLP Engine) and REQ-2.4.2 (no external server) |
| **C++17 STL** | — | Tokenisation, UTF-8 validation, file I/O, threading (`std::mutex`) |
| **Eigen3** | ≥ 3.4 | L2 normalisation (via `std::inner_product` in the embedder; Eigen dot-products used downstream by `VectorSearch`) |
| **CMake + vcpkg** | CMake ≥ 3.15 | Build system and dependency management |

**Model:** `sentence-transformers/all-MiniLM-L6-v2`  
Export command (run once, results go in `models/`):
```bash
python -m transformers.onnx \
  --model=sentence-transformers/all-MiniLM-L6-v2 \
  --feature=default \
  models/exported
```

### Python (reference / testing)

| Library | Version | Role |
|---------|---------|------|
| **sentence-transformers** | 2.2.2 | Reference embedder; produces ground-truth vectors to validate the C++ ONNX path |
| **PyTorch** | 2.1.0 | Backend for sentence-transformers |
| **Transformers (HuggingFace)** | 4.30.2 | Tokeniser and model weights |
| **NumPy** | 1.24.3 | Vector arithmetic in Python |
| **FastAPI + uvicorn** | 0.104.1 / 0.24.0 | REST wrapper for integration testing against the C++ backend |

---

## 3. NLP tasks implemented

### 3.1 Sentence embedding — `embedText()` / `embed_text()`
**What it is:** Converts an arbitrary-length text string into a fixed-size
semantic vector using a pre-trained transformer model.

**How it works (C++ pipeline):**
1. **Tokenisation** — whitespace split + BERT vocabulary look-up, produces
   `input_ids`, `attention_mask`, `token_type_ids` tensors.
2. **Transformer forward pass** — executed locally by ONNX Runtime with
   `all-MiniLM-L6-v2` (6-layer MiniLM, 384-dim output).
3. **Pooling** — if the model outputs a sequence of hidden states (shape
   `[1, seq_len, 384]`), mean-pooling across attended tokens produces a
   single vector. If it outputs a CLS vector directly (`[1, 384]`), it is
   used as-is.
4. **L2 normalisation** — the vector is scaled to unit length so that
   `cosine(a, b) == dot(a, b)`, removing the need for magnitude division
   at query time.

**SRS requirement:** REQ-4.1.3.5 — *"calculate the similarity between the
embedding vector of the file content and the search term vector"*.

---

### 3.2 File content vectorisation — `embedFile()` / `embed_file()`
**What it is:** Reads a UTF-8 text file and returns its semantic embedding.

**How it works:**  
Validates the path, reads up to `max_chars` characters (default 5 000 in
Python, full file in C++), then delegates to `embedText()`.

**SRS requirement:** REQ-2.2.1.2 — *"The software identifies some of the
contents of the file in advance, vectorises it, and stores it in the DB."*
This is the indexing-time entry point; the returned `float[384]` is stored
as a BLOB column.

---

### 3.3 Batch vectorisation — `embedTexts()` / `embed_batch()`
**What it is:** Embeds multiple texts with one loaded model instance.

**Why it matters:** Avoids repeated model-loading overhead during the initial
directory scan (REQ-4.3.2.1 step G: *"starts background … vectorisation"*).

---

### 3.4 Tokenisation — `tokenize()` (internal)
**What it is:** Converts a lowercased text string into BERT token-ID
sequences with `[CLS]`, `[SEP]`, and `[PAD]` bookmarks.

**Current implementation:** MVP whitespace tokeniser against the supplied
BERT `vocab.txt`. This is intentionally simple. Embedding quality will
improve when replaced with a proper WordPiece tokeniser that splits
sub-words (e.g. *"running"* → *"run"* + *"##ning"*).

---

### 3.5 L2 normalisation — `l2Normalize()` / `normalize_embeddings=True`
**What it is:** Scales each embedding vector to unit length.

**Why it matters (REQ-4.1.3.5):** After normalisation, cosine similarity
reduces to a dot product. `VectorSearch` (Ahmad's module) can therefore use
a single Eigen `dot()` call per candidate instead of computing magnitudes,
keeping query latency within the 0.1 s budget (REQ-5.1.1).

---

### 3.6 UTF-8 validation — `isValidUtf8()` (internal)
**What it is:** Byte-level scan that rejects malformed multi-byte sequences
before they reach the tokeniser.

**Why it matters:** REQ-5.2.1 (reliability) — the embedder must not corrupt
or crash on binary files accidentally placed in a watched directory.

---

## 4. SRS requirements mapping

| SRS Requirement | Status | How it is satisfied |
|----------------|--------|---------------------|
| REQ-4.1.3.5 Semantic-Based Search | ✅ | `embedText()` vectorises queries; `embedFile()` vectorises stored files; L2-norm enables cosine via dot-product |
| REQ-2.2.1.2 Smart search / indexing | ✅ | `embedFile()` is called at index time; return value is stored as BLOB in `files.embedding` |
| REQ-3.3.2 Local NLP Engine | ✅ | ONNX Runtime executes the model on-device; no network calls |
| REQ-2.4.2 Privacy | ✅ | No data is transmitted outside the local process |
| REQ-2.3.2 8 GB RAM minimum | ✅ | all-MiniLM-L6-v2 uses ~90 MB RAM at runtime; well within budget |
| REQ-5.2.1 Reliability | ✅ | UTF-8 validation, empty-file guard, exception hierarchy |
| REQ-5.1.2 CPU < 5% background | ✅ | Mutex-serialised inference; no background threads |

---

## 5. Integration contract

### How to call the embedder from other modules

```cpp
#include "embedder.hpp"   // flat include/ directory

nlp::Embedder::Options opts;
opts.modelPath = configuredModelPath;   // from config.json (REQ-4.3.3.7)
opts.vocabPath = configuredVocabPath;

auto embedder = std::make_shared<nlp::Embedder>(opts);

// At index time (Enrique's DB module calls this):
std::vector<float> blob = embedder->embedFile(filePath);   // store in DB

// At query time (Ahmad's SmartSearch calls this):
std::vector<float> queryVec = embedder->embedText(userQuery);
// Then pass queryVec to VectorSearch::query(queryVec, topK)
```

### DB column format (REQ-2.2.1.2)

Enrique's current schema in `src/core/database.cpp` defines the storage column
as `files.embedding BLOB`. `include/core/database.h` stores it on `FileRecord`
as `std::vector<float> embedding`, and `DatabaseManager::insertFile()` binds the
raw vector bytes with `sqlite3_bind_blob()`.

```sql
files.embedding BLOB   -- 384 contiguous IEEE-754 float32 values
                       -- byte length = 384 * sizeof(float) = 1536 bytes
```

Serialise / deserialise:
```cpp
// Write
const auto vec = embedder->embedFile(path);
// FileRecord record;
// record.embedding = vec;
// database.insertFile(record);
//
// DatabaseManager binds:
// sqlite3_bind_blob(stmt, col, vec.data(), vec.size() * sizeof(float), SQLITE_TRANSIENT);

// Read back into VectorSearch
// const float* ptr = static_cast<const float*>(sqlite3_column_blob(stmt, col));
// int dim = sqlite3_column_bytes(stmt, col) / sizeof(float);
```

### VectorSearch contract

No Ahmad `VectorSearch` header currently exists under `include/`. When it is
added, it should accept the direct output of `nlp::Embedder`:

| Property | Value |
|----------|-------|
| Type | `std::vector<float>` or equivalent contiguous `float` span |
| Dimension | `nlp::Embedder::kEmbeddingDimension` = 384 |
| Normalisation | Already L2-normalised to unit length |
| Similarity | Dot product is equivalent to cosine similarity |

---

## 6. Known limitations and next steps

| Item | Description |
|------|-------------|
| **MVP tokeniser** | Whitespace-only; does not perform WordPiece sub-word splitting. Replace with a proper WordPiece tokeniser for production-quality embeddings. |
| **Sequential batch** | `embedTexts()` calls `embedText()` in a loop. True batched ONNX inference would reduce index time for large corpora. |
| **CPU only** | No GPU/CUDA path. Acceptable for desktop background indexing (REQ-5.1.2). |
| **Fixed model** | Hard-coded to all-MiniLM-L6-v2 (384 dim). A config option for alternative models can be added later without changing the public API. |
