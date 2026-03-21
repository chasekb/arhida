# Vector Database Migration Plan

## Objective

Spin up a vector database in `docker-compose.yaml` and transition persistence away from the current external PostgreSQL dependency.

## Executive Summary

The current application is deeply coupled to PostgreSQL across runtime configuration, Docker orchestration, build dependencies, schema management, upsert logic, and backfill queries. This means the migration is not just a compose change; it is a storage architecture change.

Recommended target: **Qdrant**.

Qdrant is the best fit for this project because it is lightweight to run in Docker Compose, offers durable local storage, supports payload filtering, and exposes a simple HTTP API that fits well with the existing C++ stack (`libcurl` + `nlohmann/json`).

---

## Current State Review

### PostgreSQL Coupling Identified

The project currently depends on PostgreSQL in the following places:

- `docker-compose.yaml`
  - Injects only `POSTGRES_*` variables
  - Assumes an external Docker network for a Postgres service
  - Uses Postgres credential secrets
- `.env.example`
  - Only defines PostgreSQL connection and schema settings
- `CMakeLists.txt`
  - Requires `libpq`
- `include/db/Database.h` / `src/db/Database.cpp`
  - Uses `PGconn`, `PGresult`, `PQconnectdb`, `PQexec`, and schema/table/index creation
- `src/harvester/Harvester.cpp`
  - Assumes relational schema creation
  - Uses Postgres-specific upsert semantics (`ON CONFLICT`)
  - Uses Postgres JSONB-style filtering assumptions
- `legacy_python/arhida.py`
  - Confirms the same persistence model in the original implementation
- `README.md` and `docs/cpp_transition.md`
  - Describe PostgreSQL as the system of record

### Important Functional Observation

The system today is **metadata harvesting with relational persistence**. It is **not yet a vector-search system** because it does not generate embeddings.

That means migration requires two changes:

1. Replacing the storage backend
2. Introducing an embedding pipeline so the vector database is useful as a vector database

---

## Recommended Vector Database

## Choice: Qdrant

### Why Qdrant

- Simple Docker Compose deployment
- Persistent local volume support
- HTTP API works well with current C++ dependencies
- Supports payload storage for raw arXiv metadata
- Supports filtered retrieval for backfill/state checks
- Supports id-based upserts, which maps well to `header_identifier`

### Why Not pgvector

`pgvector` would reduce implementation complexity, but it keeps the project on PostgreSQL. That does not satisfy the goal of transitioning away from the current external Postgres dependency to a standalone vector database.

### Why Not Milvus or Weaviate

- **Milvus**: more operational complexity than this project currently needs
- **Weaviate**: good platform, but heavier and more opinionated than necessary for a harvesting pipeline of this size

---

## Proposed Target Architecture

### Services

- `app`: existing C++ harvester
- `qdrant`: new vector database service
- optional future `embeddings` service if local embedding generation is preferred

### Persistence Model

Each arXiv record becomes a vector point in Qdrant:

- `id`: deterministic id derived from `header_identifier`
- `vector`: embedding for searchable text
- `payload`:
  - `header_datestamp`
  - `header_identifier`
  - `header_setSpecs`
  - `metadata_creator`
  - `metadata_date`
  - `metadata_description`
  - `metadata_identifier`
  - `metadata_subject`
  - `metadata_title`
  - `metadata_type`
  - `created_at`
  - `updated_at`

### Recommended Embedding Input

Use a concatenated text payload such as:

```text
title + subject + description
```

This preserves semantic retrieval quality while keeping the embedding model input simple.

---

## Docker Compose Plan

## Step 1: Add Qdrant to `docker-compose.yaml`

Planned service shape:

```yaml
services:
  app:
    image: ghcr.io/chasekb/arhida:latest
    environment:
      - VECTOR_DB_PROVIDER=qdrant
      - QDRANT_URL=http://qdrant:6333
      - QDRANT_COLLECTION=arxiv_metadata
      - VECTOR_SIZE=1024
      - EMBEDDING_PROVIDER=<to-be-decided>
      - EMBEDDING_MODEL=<to-be-decided>
      - ARXIV_RATE_LIMIT_DELAY=${ARXIV_RATE_LIMIT_DELAY:-3}
      - ARXIV_BATCH_SIZE=${ARXIV_BATCH_SIZE:-2000}
      - ARXIV_MAX_RETRIES=${ARXIV_MAX_RETRIES:-3}
      - ARXIV_RETRY_AFTER=${ARXIV_RETRY_AFTER:-5}
    depends_on:
      - qdrant
    networks:
      - app_net

  qdrant:
    image: qdrant/qdrant:latest
    ports:
      - "6333:6333"
    volumes:
      - qdrant-storage:/qdrant/storage
    networks:
      - app_net

networks:
  app_net:
    driver: bridge

volumes:
  qdrant-storage:
```

### Step 2: Remove External Postgres Assumptions

Planned changes:

- remove `POSTGRES_*` as primary runtime env vars
- remove Postgres Docker secrets from the default runtime path
- remove dependence on the external `db_prdnet` network unless needed during migration

### Step 3: Keep Temporary Dual-Write or Migration Mode Optional

For safer rollout, retain a temporary compatibility mode where the app can still read from PostgreSQL during migration validation.

---

## Application Refactor Plan

## Phase 1: Configuration Refactor

Replace Postgres-only config with storage-agnostic configuration.

### New Configuration Fields

- `VECTOR_DB_PROVIDER`
- `QDRANT_URL`
- `QDRANT_COLLECTION`
- `QDRANT_API_KEY` (optional)
- `VECTOR_SIZE`
- `EMBEDDING_PROVIDER`
- `EMBEDDING_MODEL`

### Transitional Compatibility

Keep existing `POSTGRES_*` settings only if a migration tool still needs to read from the old database.

---

## Phase 2: Storage Abstraction

The existing `Database` class should be replaced with an abstraction layer.

### Proposed Interface

```cpp
class StorageEngine {
public:
    virtual ~StorageEngine() = default;
    virtual void connect() = 0;
    virtual void initialize() = 0;
    virtual void upsertRecord(const Record& record) = 0;
    virtual std::vector<std::string> getMissingDates(
        const std::string& start_date,
        const std::string& end_date,
        const std::string& set_spec) = 0;
};
```

### Planned Implementations

- `QdrantStorage`
- optional temporary `PostgresStorage` for migration parity

This change isolates storage behavior from harvesting behavior.

---

## Phase 3: Qdrant Storage Implementation

`QdrantStorage` should handle:

- collection existence checks
- collection creation
- point upserts
- payload filtering
- date/set-spec lookup for backfill logic

### Qdrant Collection Requirements

- collection name: `arxiv_metadata`
- vector size: depends on embedding model
- distance metric: likely `Cosine`

### Deterministic Point IDs

Because `header_identifier` is a string, convert it into a deterministic point id using one of:

- UUIDv5 from `header_identifier`
- stable hash mapping

UUIDv5 is preferred for stable reproducibility.

---

## Phase 4: Embedding Pipeline

This is the most important missing capability.

Without embeddings, a vector DB is only acting as a metadata store.

This plan selects a **separate local `embeddings` service** as the target architecture. A detailed pros/cons comparison of separate-service versus in-app embeddings is preserved in an endnote at the end of this document.

### Decisions Required

Choose one:

1. **External embedding API**
   - simpler to implement initially
   - introduces API cost and secret management
2. **Local embedding model service**
   - better self-hosting story
   - more operational complexity in Compose

### Updated Implementation Direction

This migration plan now assumes a **local embedding model service** will be implemented and run alongside the application and Qdrant in Docker Compose.

That means embeddings are no longer treated as an optional future enhancement; they become a first-class part of the target runtime architecture.

### New Module Needed

- `EmbeddingClient`

Responsibilities:

- generate embeddings from metadata text
- batch requests when possible
- retry failures
- return vectors sized to the configured collection dimension

---

## Local Embedding Model Service Plan

## Target Approach

Run embeddings as a dedicated local service in Compose and have the C++ harvester call it over HTTP.

### Updated Service Topology

- `app`: arXiv harvester and orchestration
- `embeddings`: local model inference service
- `qdrant`: vector database

### High-Level Flow

1. `app` harvests metadata from arXiv
2. `app` constructs embedding input text
3. `app` sends text to `embeddings`
4. `embeddings` returns dense vectors
5. `app` upserts vectors + payload into Qdrant

---

## Compose-Level Implementation Requirements

## Required `docker-compose.yaml` Changes

Add a new `embeddings` service, for example:

```yaml
services:
  app:
    depends_on:
      - embeddings
      - qdrant
    environment:
      - VECTOR_DB_PROVIDER=qdrant
      - QDRANT_URL=http://qdrant:6333
      - QDRANT_COLLECTION=arxiv_metadata
      - EMBEDDING_SERVICE_URL=http://embeddings:8000
      - EMBEDDING_MODEL_NAME=bge-small-en-v1.5
      - VECTOR_SIZE=384

  embeddings:
    image: ghcr.io/chasekb/arhida-embeddings:local
    container_name: arhida-embeddings
    ports:
      - "8000:8000"
    environment:
      - MODEL_NAME=bge-small-en-v1.5
      - MODEL_DIMENSION=384
      - MAX_BATCH_SIZE=64
      - DEVICE=cuda
      - MODEL_PATH=/models/bge-small-en-v1.5/model.onnx
      - TOKENIZER_PATH=/models/bge-small-en-v1.5/tokenizer
      - CUDA_VISIBLE_DEVICES=0
      - ORT_EXECUTION_PROVIDER=CUDA
    volumes:
      - model-files:/models:ro
    networks:
      - app_net
    restart: unless-stopped

  qdrant:
    image: qdrant/qdrant:latest
    ports:
      - "6333:6333"
    volumes:
      - qdrant-storage:/qdrant/storage
    networks:
      - app_net

volumes:
  qdrant-storage:
  model-files:
```

### Compose Requirements Summary

- add `embeddings` container
- mount model artifacts into the container via a dedicated volume
- put `app`, `embeddings`, and `qdrant` on the same internal network
- ensure `app` waits for `embeddings` and `qdrant`
- expose model configuration through environment variables
- allow GPU-capable runtime configuration for the embeddings container

### Model Volume Mount Requirement

The embedding service should load model assets from a mounted volume rather than downloading them dynamically at startup.

Recommended layout:

```text
/models/
  bge-small-en-v1.5/
    model.onnx
    tokenizer/
      tokenizer.json
      tokenizer_config.json
      special_tokens_map.json
      vocab files...
```

Benefits:

- faster startup
- reproducible deployments
- easier model upgrades/rollbacks
- avoids repeated downloads in restricted environments

### GPU Enablement Requirement

The Compose deployment should support GPU-backed inference for the `embeddings` service.

At a minimum, the plan should account for:

- CUDA-capable ONNX Runtime build in the image
- Apple Silicon / MLX-capable deployment path for macOS-hosted local inference
- visible GPU device configuration
- execution provider selection via environment variable
- fallback to CPU mode when GPU is unavailable

### MLX Support Requirement

Because development may occur on Apple Silicon hardware, the plan should also support an **MLX-based acceleration path** in addition to CUDA.

This means the embeddings architecture should recognize two GPU-oriented runtime families:

- **CUDA** for NVIDIA-backed Linux environments
- **MLX** for Apple Silicon local environments

The migration plan should therefore treat GPU support as a broader accelerator strategy rather than CUDA-only support.

---

## Embedding Service Functional Requirements

The local embedding service should provide a narrow, stable API.

### Minimum API Requirements

#### Health Endpoint

```http
GET /health
```

Response should confirm:

- service is alive
- model is loaded
- configured vector dimension
- current device (`cpu` or `gpu`)

#### Single or Batch Embedding Endpoint

```http
POST /embed
Content-Type: application/json

{
  "inputs": [
    "first text",
    "second text"
  ]
}
```

Expected response:

```json
{
  "model": "bge-small-en-v1.5",
  "dimension": 384,
  "vectors": [
    [0.1, 0.2, 0.3],
    [0.4, 0.5, 0.6]
  ]
}
```

### API Contract Requirements

- deterministic response order matching input order
- explicit vector dimension in the response
- predictable error codes for invalid input, overload, and model-load failure
- request size limits to prevent memory blowups
- timeout behavior suitable for batch ingestion

---

## Embedding Model Requirements

### Model Selection Requirements

Choose a local model that is:

- lightweight enough for local/containerized inference
- strong enough for semantic search over titles + abstracts
- available with a clear runtime path
- stable in vector size

### Recommended Starting Model Class

Use a small sentence-transformer or BGE-family model for the first implementation.

Good starting candidates:

- `BAAI/bge-small-en-v1.5`
- `sentence-transformers/all-MiniLM-L6-v2`

### Preferred Starting Choice

`bge-small-en-v1.5`

Why:

- good quality/size tradeoff
- practical for local inference
- widely used for semantic retrieval
- manageable vector dimension for Qdrant

### Model Constraints to Lock Down

The plan must pin:

- exact model name
- exact vector dimension
- normalization behavior
- embedding input formatting rules

These must remain stable or the Qdrant collection and migration logic will drift.

---

## Embedding Service Technical Implementation Requirements

### Recommended C++ Stack

To keep the embedding service aligned with the rest of this repository, the service can be implemented in **C++** rather than Python.

Recommended stack:

- C++20
- HTTP server framework: **Drogon**, **Crow**, or **Boost.Beast**
- JSON serialization: **nlohmann/json**
- logging: **spdlog**
- model inference runtime: **ONNX Runtime C++ API**
- tokenizer/runtime assets packaged with the service image

### Preferred C++ Implementation Choice

The strongest practical option is:

- **Drogon** for the HTTP service layer
- **ONNX Runtime** for model inference
- **nlohmann/json** for request/response handling
- **spdlog** for observability

Why:

- good production-quality HTTP server support
- straightforward JSON handling
- strong C++ interoperability
- avoids introducing a second language/runtime into the deployment
- ONNX Runtime provides a realistic path for efficient local inference in C++

### Model Packaging Strategy for C++

The local model service should not depend on Python-only model loading at runtime.

Instead, the plan should assume:

1. choose a transformer embedding model that can be exported to **ONNX**
2. mount the ONNX model files and tokenizer assets into the container via a dedicated read-only model volume
3. load tokenizer assets and model artifacts directly from the filesystem at startup

This keeps inference fully local and C++-native.

### Preferred Artifact Strategy

Prefer **volume-mounted model artifacts** over baking large model files directly into the image.

Why:

- smaller and more reusable container images
- easier to swap models without rebuilding the service image
- cleaner separation between runtime binary and model assets
- better fit for GPU and multi-environment deployments

### Required Capabilities

#### 1. Model Bootstrap

- load model from mounted filesystem path at container startup
- fail fast if model cannot load
- validate that required model/tokenizer files exist on the mounted volume
- expose startup status in logs and health endpoint

#### 1a. Tokenizer Support

Because transformer embeddings require tokenization, the C++ service must also include a tokenizer strategy.

Recommended options:

- use an ONNX-compatible tokenizer pipeline if available
- use Hugging Face tokenizer assets with a compatible C++ tokenizer library
- prepackage tokenizer vocabulary/config files in the service image

Tokenizer behavior must be locked to the selected model version so embedding output remains stable.

#### 2. Batch Inference

- accept multiple texts in one request
- process batches efficiently
- configurable max batch size
- reject oversized requests cleanly

#### 3. Resource Configuration

- allow CPU-only mode by default
- support GPU execution when configured
- support accelerator-specific execution modes for both CUDA and MLX environments
- configurable worker counts and concurrency
- configurable memory limits in Compose/runtime

#### 3a. ONNX Runtime Execution Configuration

The service should expose runtime controls for:

- intra-op thread count
- inter-op thread count
- execution provider selection
- graph optimization level
- model session warmup at startup

These settings will matter for backfill throughput and developer-machine stability.

#### 3b. GPU Execution Requirements

The C++ embeddings service should support GPU inference through ONNX Runtime CUDA execution provider when available.

Required behavior:

- select CPU or CUDA provider based on configuration
- fail clearly if CUDA is requested but not available
- report active execution provider via `/health`
- keep CPU fallback mode available for non-GPU hosts

#### 3c. MLX Execution Requirements

For Apple Silicon environments, the plan should also support an **MLX-backed local inference mode**.

Required behavior:

- support a distinct runtime mode such as `DEVICE=mlx`
- expose the active accelerator mode via `/health`
- keep model/tokenizer assets mounted from the same volume-based artifact strategy
- allow local development on macOS without requiring CUDA or NVIDIA tooling

### Accelerator Abstraction Requirement

To support CPU, CUDA, and MLX cleanly, the embeddings service should abstract execution backends behind a common engine interface.

Suggested conceptual interface:

```cpp
class EmbeddingBackend {
public:
    virtual ~EmbeddingBackend() = default;
    virtual void initialize() = 0;
    virtual std::vector<std::vector<float>> embed(const BatchInput& input) = 0;
    virtual std::string backendName() const = 0;
};
```

Potential implementations:

- `OnnxCpuBackend`
- `OnnxCudaBackend`
- `MlxBackend`

This keeps the HTTP service and request contract stable while allowing environment-specific acceleration choices.

#### 4. Stable Preprocessing

The service must define consistent preprocessing for all text before embedding.

At minimum:

- trim leading/trailing whitespace
- collapse repeated internal whitespace
- handle missing title/subject/description fields consistently
- enforce UTF-8-safe input handling

#### 5. Error Handling

- return structured JSON errors
- distinguish validation errors from inference failures
- expose transient vs permanent failure semantics where possible

#### 6. Observability

- log model load time
- log batch size and latency
- expose request failure counts
- optionally expose metrics endpoint later

---

## C++ Embedding Service Implementation Plan

## Service Structure

Suggested structure for a dedicated C++ embeddings service:

```text
embeddings_service/
├── CMakeLists.txt
├── include/
│   ├── server/
│   │   └── HttpServer.h
│   ├── embedding/
│   │   ├── EmbeddingEngine.h
│   │   ├── Tokenizer.h
│   │   └── TextPreprocessor.h
│   ├── config/
│   │   └── EmbeddingConfig.h
│   └── utils/
│       └── HealthStatus.h
├── src/
│   ├── main.cpp
│   ├── server/
│   ├── embedding/
│   ├── config/
│   └── utils/
└── Dockerfile
```

## Core Components

### `EmbeddingConfig`

Responsibilities:

- load model path
- load tokenizer path
- load vector dimension
- configure batch size
- configure port
- configure ONNX runtime threading/execution settings

### `TextPreprocessor`

Responsibilities:

- normalize whitespace
- enforce UTF-8-safe handling
- apply stable formatting rules before tokenization

### `Tokenizer`

Responsibilities:

- transform normalized input strings into token ids, masks, and attention inputs expected by the model
- enforce truncation and padding rules
- expose deterministic batching behavior

### `EmbeddingEngine`

Responsibilities:

- initialize ONNX Runtime environment/session
- load model and tokenizer resources
- run inference on a batch of prepared inputs
- optionally normalize vectors before returning them
- validate output dimension

### `HttpServer`

Responsibilities:

- expose `/health`
- expose `/embed`
- validate request payloads
- marshal JSON in/out
- handle error responses and timeouts

---

## Inference Runtime Requirements for C++

### ONNX Runtime Requirements

The service should use ONNX Runtime as the primary inference engine.

Required capabilities:

- load model session once at startup
- reuse session across requests
- support batch inference
- support CPU execution first
- optionally support CUDA execution provider later

### MLX Runtime Requirements

For Apple Silicon support, the service should also define a separate MLX-capable inference path.

Practical implication:

- the C++ service may need a backend abstraction where ONNX Runtime remains the Linux/CPU/CUDA default
- MLX support may be implemented through a dedicated adapter or companion runtime path for macOS development environments

Because MLX is not an ONNX Runtime execution provider, it should be treated as a separate backend rather than a variant of CUDA support.

### Output Normalization Requirement

If cosine similarity is used in Qdrant, the service should either:

- return normalized vectors directly, or
- return raw vectors and document that normalization occurs in the caller

The plan should prefer **service-side normalization** so all downstream consumers receive consistent vectors.

---

## C++ HTTP Layer Options

### Option 1: Drogon

Best overall choice for a production-style C++ service.

Pros:

- mature HTTP framework
- routing, middleware, and JSON support
- good performance and async model

Cons:

- heavier framework footprint than minimal alternatives

### Option 2: Crow

Lighter and simpler than Drogon.

Pros:

- fast to stand up
- minimal API surface

Cons:

- less full-featured for long-term service growth

### Option 3: Boost.Beast

Lowest-level option.

Pros:

- maximum control
- strong C++ ecosystem alignment

Cons:

- most implementation effort
- more boilerplate for routing and JSON APIs

### Recommendation

Use **Drogon** unless there is a strong reason to minimize dependencies. It provides the best balance of maintainability and production readiness.

---

## C++ Build and Packaging Requirements

### CMake Requirements

The embedding service will need a dedicated `CMakeLists.txt` that includes:

- HTTP framework dependency
- ONNX Runtime headers/libraries
- `nlohmann/json`
- `spdlog`
- any tokenizer library used

### Docker Requirements

The service image should:

- install ONNX Runtime dependencies
- support loading model and tokenizer assets from a mounted read-only volume
- include a GPU-capable variant or base image when CUDA support is enabled
- expose the HTTP port
- warm up the model at startup if possible

### Suggested Runtime Environment Variables

- `MODEL_PATH`
- `TOKENIZER_PATH`
- `MODEL_NAME`
- `MODEL_DIMENSION`
- `MAX_BATCH_SIZE`
- `SERVICE_PORT`
- `ORT_INTRA_THREADS`
- `ORT_INTER_THREADS`
- `DEVICE=cpu|cuda|mlx`
- `ORT_EXECUTION_PROVIDER=CPU|CUDA`
- `CUDA_VISIBLE_DEVICES`
- `ACCELERATOR_BACKEND=onnx|mlx`

---

## C++ Service Operational Requirements

### Startup Sequence

At startup the service should:

1. load config
2. initialize logger
3. verify mounted model/tokenizer assets are present
4. load tokenizer assets
5. initialize the selected inference backend
6. configure CPU, CUDA, or MLX execution path
7. run a warmup inference
8. expose healthy status only after successful warmup

### Health Check Contract

`GET /health` should return at least:

- model loaded: true/false
- tokenizer loaded: true/false
- vector dimension
- runtime device
- execution provider
- accelerator backend
- service version

### Failure Handling Requirements

- if tokenizer load fails, service should not start
- if model session load fails, service should not start
- if CUDA is requested but unavailable, service should either fail fast or explicitly fall back according to configuration
- if MLX is requested but unavailable, service should either fail fast or explicitly fall back according to configuration
- if batch inference fails, request should return structured error JSON
- if output dimension mismatches configured dimension, request should fail hard

### Apple Silicon Development Recommendation

For local macOS development on Apple Silicon:

- prefer `DEVICE=mlx`
- continue to mount model artifacts from `/models`
- keep the same `/embed` and `/health` API contract as Linux deployments
- document any model-format conversion requirements needed for the MLX backend

---

## Revised Recommendation for the Embedding Service Stack

If you want the embedding service to stay consistent with the rest of this repository’s technology direction, the plan should use:

- **C++20**
- **Drogon**
- **ONNX Runtime C++ API**
- **nlohmann/json**
- **spdlog**

with backend flexibility for:

- **ONNX Runtime CPU/CUDA** in Linux/NVIDIA environments
- **MLX-backed execution** in Apple Silicon local environments

That gives you a fully local, C++-native embedding service that integrates cleanly with the existing C++ application while avoiding the operational split of a Python-based inference stack.

---

## Application-Side Requirements for Local Embeddings

The C++ app will need a dedicated embedding client layer.

### New Config Fields Required

- `EMBEDDING_SERVICE_URL`
- `EMBEDDING_MODEL_NAME`
- `VECTOR_SIZE`
- `EMBEDDING_REQUEST_TIMEOUT_MS`
- `EMBEDDING_MAX_BATCH_SIZE`
- `EMBEDDING_RETRY_COUNT`

### New C++ Components Required

#### `EmbeddingClient`

Responsibilities:

- call `/health` on startup or during initialization
- send batched text payloads to `/embed`
- validate returned vector dimension
- retry transient failures
- surface hard failures clearly to harvesting logic

#### `EmbeddingTextBuilder`

Responsibilities:

- build stable input text from record fields
- normalize whitespace
- guarantee consistent field ordering

Suggested format:

```text
Title: <title>
Subjects: <subject1>; <subject2>
Description: <abstract>
```

This format should be versioned conceptually so future changes do not silently alter retrieval behavior.

Current decision for additional embedding metadata (Phase 5):

- **Defer additional metadata fields for now** and keep the canonical embedding input
  limited to `title + subject + description`.
- Revisit potential inclusion of `creator`, `date`, `type`, or identifier-derived context
  under Phase 13 once expanded metadata harvesting is finalized.

---

## Qdrant Requirements for Local Embeddings

Qdrant collection settings must match the local model.

### Required Alignment

- collection vector dimension must equal model output dimension
- distance metric should be chosen consistently with normalization strategy
- if vectors are normalized, cosine similarity is the preferred default

### Operational Requirement

The app should validate at startup that:

- the embedding service reports dimension `N`
- Qdrant collection expects dimension `N`

If they do not match, startup should fail fast rather than corrupting the collection with incompatible writes.

---

## Backfill and Migration Requirements with Local Embeddings

Local inference introduces throughput constraints that affect migration and backfill.

### Requirements

- historical migration utility must support batching for embedding requests
- migration utility must support checkpoint/resume
- backfill should rate-limit both arXiv requests and embedding requests independently
- large historical loads may need a separate migration mode from normal daily harvesting

Current decision for runtime backfill state tracking:

- **Normal harvester backfill state is derived from Qdrant payloads**
  (`header_datestamp` + `header_setSpecs`) via `getMissingDates()` filters.
- **A separate checkpoint mechanism is deferred to the dedicated historical
  migration utility** (Phase 14) where long-running resume semantics matter most.

### Recommended Migration Behavior

- read historical records from PostgreSQL in chunks
- build embedding text in chunks
- call embedding service in batches
- upsert into Qdrant in batches
- persist checkpoint state after each successful chunk

---

## Local Embedding Service Non-Functional Requirements

### Performance Requirements

- acceptable latency for small batches
- predictable throughput under backfill loads
- graceful degradation if request volume spikes

### Reliability Requirements

- startup health check must verify model is loaded
- service should restart cleanly without data loss
- temporary failures must be recoverable by client retries

### Security Requirements

- internal-only network exposure by default
- no public exposure unless explicitly needed
- request body size limits
- dependency/image pinning for reproducible builds

### Portability Requirements

- CPU-first deployment must work on ordinary developer machines
- optional GPU support should be additive, not required

---

## Documentation Requirements for Local Embeddings

The migration docs should explicitly document:

- chosen model name and dimension
- service API contract
- compose configuration
- model cache volume behavior
- CPU vs GPU runtime options
- failure and retry behavior
- how to rebuild the collection if model/dimension changes

---

## Revised Recommendation

The target implementation should now be:

1. add `qdrant` service to Compose
2. add **local `embeddings` service** to Compose
3. refactor app config to use `EMBEDDING_SERVICE_URL` and Qdrant settings
4. build `EmbeddingClient` in C++
5. implement stable embedding text formatting
6. create Qdrant collection using the local model's fixed dimension
7. migrate historical PostgreSQL records through the local embedding service

This gives the project a fully local vector pipeline with no dependency on an external embedding provider.

---

## Detailed Implementation Checklist

This section breaks the migration into checkable implementation phases.

## Phase 0: Design and Readiness

- [ ] Confirm Qdrant as the target vector database
- [ ] Confirm the local embeddings-service architecture is the intended target
- [ ] Confirm the initial embedding model name and vector dimension
- [ ] Confirm the initial embedding text format for each arXiv record
- [ ] Confirm whether additional arXiv metadata formats (`arXiv`, `arXivRaw`) will be harvested in phase 1 or deferred
- [ ] Confirm whether historical PostgreSQL migration is in scope for the first release
- [ ] Confirm target deployment environments:
  - [ ] Linux CPU
  - [ ] Linux NVIDIA GPU / CUDA
  - [ ] macOS Apple Silicon / MLX

## Phase 1: Docker Compose and Runtime Topology

- [x] Update `docker-compose.yaml` to add `qdrant`
- [x] Add persistent `qdrant-storage` volume
- [x] Add `embeddings` service to `docker-compose.yaml`
- [x] Add read-only model artifact volume mount for embeddings service
- [x] Add environment variables for Qdrant connectivity
- [x] Add environment variables for embeddings-service connectivity
- [x] Add environment variables for model path, tokenizer path, dimension, and batch size
- [x] Add environment variables for accelerator selection (`cpu`, `cuda`, `mlx`)
- [x] Add startup ordering / dependency configuration for `app -> embeddings -> qdrant`
- [x] Ensure services share the correct internal network
- [x] Decide whether to remove or temporarily keep the external PostgreSQL network dependency
- [ ] Document example compose overrides for:
  - [x] CPU-only deployment
  - [x] CUDA-enabled deployment
  - [x] Apple Silicon / MLX development

## Phase 2: Configuration Refactor in the C++ App

- [x] Extend `Config` to support vector database settings
- [x] Extend `Config` to support embeddings-service settings
- [x] Add config for `EMBEDDING_SERVICE_URL`
- [x] Add config for `EMBEDDING_MODEL_NAME`
- [x] Add config for `VECTOR_SIZE`
- [x] Add config for embedding timeout / retry controls
- [x] Add config for accelerator/backend awareness if needed by the app
- [x] Preserve temporary PostgreSQL settings for migration tooling if still needed
- [x] Update `.env.example` with new vector/embedding configuration
- [x] Remove or de-emphasize Postgres-only runtime guidance in docs/examples

## Phase 3: Storage Abstraction in the C++ App

- [x] Define a storage abstraction interface (for example `StorageEngine`)
- [x] Refactor harvester code to depend on the abstraction rather than `Database`
- [x] Add initial runtime backend selection between PostgreSQL and Qdrant
- [ ] Decide whether the existing Postgres implementation becomes `PostgresStorage` or remains migration-only
- [x] Separate schema/table responsibilities from general storage orchestration
- [ ] Define abstraction methods for:
  - [x] connect / initialize
  - [x] upsert record
  - [x] collection/index setup
  - [x] missing-date lookup / checkpoint queries
  - [x] any required stats or validation helpers

## Phase 4: Qdrant Storage Implementation

- [x] Create a `QdrantStorage` implementation
- [x] Implement Qdrant connection/bootstrap logic
- [x] Implement collection existence checks
- [x] Implement collection creation with correct dimension and distance metric
- [x] Implement id generation from `header_identifier`
- [x] Implement point upsert logic
- [x] Implement payload serialization for all persisted metadata fields
- [x] Implement filtering/query support needed by backfill logic
- [x] Add startup validation that collection dimension matches embedding dimension
- [x] Add logging and error handling for Qdrant request failures
- [ ] Add tests or smoke checks for collection creation and upsert behavior

## Phase 5: Embedding Text and Record Preparation

- [x] Implement `EmbeddingTextBuilder`
- [x] Define the canonical text layout for embeddings
- [x] Normalize whitespace and field ordering deterministically
- [x] Handle missing title/subject/abstract values consistently
- [x] Decide whether additional metadata should be included in the embedding text now or later
- [x] Version the embedding text format conceptually in docs/code comments
- [ ] Add tests for text-building behavior

## Phase 6: C++ Embeddings Service Foundation

- [ ] Create a dedicated embeddings-service project structure
- [ ] Add `CMakeLists.txt` for the embeddings service
- [ ] Add HTTP server dependency (recommended: Drogon)
- [ ] Add ONNX Runtime dependency
- [ ] Add `nlohmann/json` and `spdlog`
- [ ] Define service config loading
- [ ] Define `/health` endpoint contract
- [ ] Define `/embed` endpoint contract
- [ ] Define structured error response format
- [ ] Add startup logging and service versioning

## Phase 7: Model Artifact and Tokenizer Management

- [ ] Choose the initial exported model artifact format
- [ ] Prepare ONNX model files for the selected embedding model
- [ ] Prepare tokenizer assets for the selected embedding model
- [ ] Define mounted model directory layout under `/models`
- [ ] Implement startup validation for required model/tokenizer files
- [ ] Document artifact preparation and placement steps
- [ ] Decide how model upgrades/rollbacks will be handled operationally

## Phase 8: C++ Inference Engine Implementation

- [ ] Implement `EmbeddingBackend` interface
- [ ] Implement `OnnxCpuBackend`
- [ ] Implement `OnnxCudaBackend`
- [ ] Define plan for `MlxBackend`
- [ ] Implement ONNX Runtime session initialization
- [ ] Implement batch inference
- [ ] Implement output-dimension validation
- [ ] Implement vector normalization strategy
- [ ] Implement runtime warmup inference
- [ ] Implement tokenizer integration
- [ ] Add tests or verification for returned embedding dimension and stability

## Phase 9: Accelerator Support

### CUDA
- [ ] Add CUDA-capable ONNX Runtime deployment support
- [ ] Support runtime selection of CUDA execution provider
- [ ] Add health reporting for active CUDA execution provider
- [ ] Add failure handling if CUDA is requested but unavailable
- [ ] Validate throughput on NVIDIA-backed environment

### MLX
- [ ] Finalize technical approach for MLX backend implementation
- [ ] Define whether MLX is native C++, bridged, or adapter-backed
- [ ] Implement or stub `MlxBackend`
- [ ] Support runtime selection of `DEVICE=mlx`
- [ ] Add health reporting for active MLX backend
- [ ] Add failure handling if MLX is requested but unavailable
- [ ] Validate local Apple Silicon development path

### Fallback Logic
- [ ] Decide whether accelerator failure should hard fail or fall back automatically
- [ ] Implement explicit fallback policy
- [ ] Document fallback behavior clearly

## Phase 10: Embeddings-Service HTTP and Operational Behavior

- [ ] Implement request validation for `/embed`
- [ ] Implement batch-size limits
- [ ] Implement timeout behavior
- [ ] Implement structured JSON error responses
- [ ] Implement latency and batch-size logging
- [ ] Implement health state reporting only after successful warmup
- [ ] Add smoke tests for `/health`
- [ ] Add smoke tests for `/embed`
- [ ] Add load testing or benchmark pass for representative batch sizes

## Phase 11: Application-Side Embedding Client

- [x] Implement `EmbeddingClient` in the main C++ app
- [x] Implement `/health` check on startup
- [x] Implement batched `/embed` calls
- [x] Implement retry logic for transient failures
- [x] Implement timeout handling
- [x] Validate embedding dimension against configuration
- [x] Surface clear errors back to harvester flow
- [ ] Add tests for success, retry, timeout, and invalid-dimension scenarios

## Phase 12: Harvester Refactor

- [x] Replace relational table initialization with vector-collection initialization
- [x] Replace SQL upsert assumptions with Qdrant upsert flow
- [x] Insert embedding-generation step into record ingestion flow
- [x] Persist vectors plus payload for harvested records
- [x] Refactor `getMissingDates()` behavior to work with Qdrant filters or checkpoints
- [x] Decide whether backfill state lives entirely in Qdrant payloads or in a separate checkpoint mechanism
- [ ] Ensure recent harvest mode still works end to end
- [ ] Ensure backfill mode still works end to end
- [x] Add logging around embedding failures vs storage failures

## Phase 13: Expanded Metadata Harvesting (Optional but Recommended)

- [ ] Decide whether to keep only `oai_dc` or add `arXiv` metadata format
- [ ] If adding `arXiv` format:
  - [ ] extend record model for richer metadata fields
  - [ ] update OAI parsing logic
  - [ ] persist new fields in Qdrant payloads
- [ ] Decide whether to add `arXivRaw` for version history
- [ ] If adding `arXivRaw`:
  - [ ] extend record model for version history
  - [ ] update parsing and persistence logic
- [ ] Decide whether any of these additional fields should influence embedding text

## Phase 14: Historical PostgreSQL Migration Utility

- [ ] Design migration utility entry point
- [ ] Reuse or implement PostgreSQL read path for historical records
- [ ] Read historical records in chunks
- [ ] Build embedding text for migrated records
- [ ] Batch requests to embeddings service
- [ ] Batch upserts to Qdrant
- [ ] Add checkpoint/resume support
- [ ] Add migration progress logging
- [ ] Add parity validation for counts by day, set, and identifier
- [ ] Define cutover criteria after migration completes

## Phase 15: Validation and Testing

### Functional Validation
- [ ] Verify Qdrant starts and persists data
- [ ] Verify embeddings service starts and reports healthy
- [ ] Verify app starts only when dependencies are ready
- [ ] Verify one record can be harvested, embedded, and stored successfully
- [ ] Verify batch ingestion works
- [ ] Verify recent mode works end to end
- [ ] Verify backfill mode works end to end

### Data Validation
- [ ] Verify vector dimension matches configured collection dimension
- [ ] Verify payload fields are complete and correctly serialized
- [ ] Verify deterministic id generation
- [ ] Verify duplicate records update correctly
- [ ] Verify filtering by set/date still works for backfill purposes

### Performance Validation
- [ ] Measure embedding throughput on CPU
- [ ] Measure embedding throughput on CUDA
- [ ] Measure embedding throughput on MLX if implemented
- [ ] Measure end-to-end ingestion throughput
- [ ] Tune batch sizes for app, embeddings service, and Qdrant writes

### Failure Validation
- [ ] Verify behavior when embeddings service is unavailable
- [ ] Verify behavior when Qdrant is unavailable
- [ ] Verify behavior when model artifacts are missing
- [ ] Verify behavior when accelerator is requested but unavailable
- [ ] Verify retry behavior under transient failures

## Phase 16: Documentation and Operations

- [x] Update `README.md` for the new architecture
- [x] Update `.env.example`
- [x] Update Docker usage instructions
- [x] Document model artifact preparation and volume mounting
- [x] Document CPU, CUDA, and MLX deployment modes
- [x] Document how to rebuild or recreate a Qdrant collection
- [x] Document migration workflow from PostgreSQL to Qdrant
- [x] Document backup/restore strategy for Qdrant storage
- [x] Document operational health checks and expected endpoints

## Phase 17: Cutover and Cleanup

- [ ] Define go-live criteria
- [ ] Run historical migration if in scope
- [ ] Validate parity against PostgreSQL
- [ ] Switch primary runtime persistence to Qdrant
- [ ] Disable or remove PostgreSQL dependency from normal runtime path
- [ ] Remove `libpq` from the main application build if no longer needed
- [ ] Remove obsolete Postgres-only documentation
- [ ] Tag/release the migrated architecture

---

## Phase 5: Harvester Changes

`Harvester` must stop depending on relational database behavior.

### Current Behavior to Replace

- `ensureTableExists()`
  - replace with collection initialization
- SQL upsert string construction
  - replace with Qdrant point upsert requests
- SQL missing-date query
  - replace with payload filter queries

### Updated Flow

1. harvest record from arXiv
2. normalize metadata
3. build embedding input text
4. request embedding
5. upsert vector point into Qdrant
6. store raw metadata as payload

---

## Phase 6: Existing Data Migration

The existing PostgreSQL dataset should be migrated with a one-time utility.

### Migration Utility Responsibilities

- read all existing rows from PostgreSQL
- convert each row into the new payload format
- generate embeddings
- upsert into Qdrant
- log failures and resume safely

### Validation Checks

- total record counts match
- counts by `set_spec` match
- counts by date match
- random sampling confirms metadata parity
- duplicate `header_identifier` handling is stable

### Recommended Rollout Strategy

1. migrate historical data
2. run parity validation
3. cut app writes to Qdrant
4. decommission Postgres dependency after verification

---

## Phase 7: Build System Changes

### Current Build Dependency to Remove Later

- `libpq`

### Dependencies to Keep Using

- `libcurl`
- `libxml2`
- `nlohmann/json`
- `spdlog`

### Build Transition Strategy

- keep `libpq` only while migration tooling requires it
- remove `libpq` from `CMakeLists.txt` once Qdrant is fully adopted

---

## Phase 8: Documentation Updates

Update all user-facing and operational docs:

- `README.md`
- `.env.example`
- `docs/cpp_transition.md`
- Docker usage examples

### New Documentation Topics

- how to run Qdrant locally
- how to configure vector collection settings
- how embedding configuration works
- how to perform migration from PostgreSQL
- how to back up and restore Qdrant storage

---

## Risks and Open Questions

## Risk 1: No Embedding Strategy Yet

This is the biggest architectural gap.

If embeddings are not generated, Qdrant does not provide the intended vector-search capability.

## Risk 2: Backfill Logic Is Harder in a Pure Vector Store

The current backfill logic relies on querying existing dates efficiently. This must be reimplemented using Qdrant payload filters or a separate checkpoint/state mechanism.

## Risk 3: Vector DB Alone May Not Replace All Relational Use Cases

If the system needs strong tabular reporting, analytics, or administrative querying, a hybrid design may still be better.

## Risk 4: Migration Complexity

The current code is hard-coded to PostgreSQL semantics. Migration will touch config, storage, batching, data validation, and documentation.

---

## Recommended Delivery Sequence

### Milestone 1: Compose and Config

- add Qdrant service
- add vector env vars
- stop assuming external Postgres at runtime

### Milestone 2: Storage Abstraction

- introduce `StorageEngine`
- isolate harvester from SQL concepts

### Milestone 3: Embeddings + Qdrant Writes

- implement `EmbeddingClient`
- implement `QdrantStorage`
- upsert harvested records into Qdrant

### Milestone 4: Backfill Compatibility

- rebuild missing-date detection using payload filters or checkpoints

### Milestone 5: Historical Migration

- migrate PostgreSQL records
- validate parity

### Milestone 6: Cleanup

- remove `libpq`
- remove old Postgres env vars and docs
- simplify compose and deployment docs

---

## Suggested Success Criteria

Migration is complete when all of the following are true:

- Qdrant starts from `docker-compose.yaml`
- data persists in a Docker volume
- new harvests write vectors and payload successfully
- backfill logic still works
- historical PostgreSQL data is migrated
- PostgreSQL is no longer required for normal runtime operation
- docs reflect the new architecture clearly

---

## Recommended Next Implementation Step

Start with these three concrete changes:

1. update `docker-compose.yaml` to add a Qdrant service and persistent volume
2. refactor `Config` to support vector-database settings
3. scaffold a `QdrantStorage` class behind a storage abstraction while keeping Postgres only for migration validation

---

## Endnote: Embeddings Deployment Tradeoffs

There are two viable ways to introduce embeddings into this system:

1. build embedding generation directly into the existing harvester application
2. run embedding generation as a separate service and call it from the harvester

### Separate `embeddings` Service

**Pros**

- clear separation of responsibilities between harvesting and inference
- easier model swaps and experimentation
- better runtime isolation for CPU/GPU-heavy inference
- more operational flexibility for restarts and scaling
- allows the embedding stack to use Python/inference-native tooling
- better long-term fit for larger ingestion pipelines

**Cons**

- more operational complexity
- more service-to-service failure modes
- added request/serialization latency
- more moving parts in local development
- extra API/version coordination between app and embeddings service

### Embeddings Built Into the Existing Application

**Pros**

- simpler deployment shape
- lower integration overhead
- easier end-to-end debugging in early development
- potentially faster initial delivery

**Cons**

- tighter coupling in the main application
- harder model/provider changes later
- more resource contention inside the harvester process
- more friction if local inference must be implemented in or tightly coordinated with C++
- harder reuse by other components later

### Why This Plan Chooses a Separate Service

This migration plan chooses a **separate local embeddings service** because the target architecture now explicitly includes local model hosting. Once the project commits to local inference, the separation benefits outweigh the added operational overhead:

- model lifecycle and inference tuning stay isolated from harvesting logic
- CPU/GPU concerns are separated from the main C++ ingestion runtime
- the app can remain focused on arXiv harvesting and Qdrant persistence
- future reuse and scaling paths remain open
