# arXiv Academic Paper Metadata Harvester (C++)

High-performance C++ harvester for arXiv OAI-PMH metadata with a vector-first
runtime path.

The active migration target is:

- **Qdrant** as the primary persistence backend
- **Embeddings service** (`/health`, `/embed`) for vector generation
- **PostgreSQL restricted to historical migration tooling** (not normal runtime)

## Features

- **OAI-PMH Client**: harvests metadata from arXiv.org
- **Vector persistence**: stores embeddings + payload in Qdrant
- **Embedding integration**: calls local embedding service over HTTP
- **Backfill support**: finds missing dates through backend-specific queries
- **Rate limiting**: configurable request delays to comply with arXiv policy
- **CLI interface**: `recent`, `backfill`, and `both` modes

## Runtime Topology (Docker Compose)

- `app` (`ghcr.io/chasekb/arhida:latest`)
- `qdrant` (`qdrant/qdrant:latest`)
- `embeddings` (`ghcr.io/chasekb/arhida-embeddings:latest`)

Health endpoints used in compose:

- Qdrant: `GET http://qdrant:6333/healthz`
- Embeddings: `GET http://embeddings:8000/health`

## Configuration

Primary runtime variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `VECTOR_DB_PROVIDER` | `qdrant` | Storage backend selector |
| `QDRANT_URL` | `http://qdrant:6333` | Qdrant base URL |
| `QDRANT_COLLECTION` | `arxiv_metadata` | Qdrant collection |
| `VECTOR_SIZE` | `384` | Embedding/vector dimension |
| `EMBEDDING_SERVICE_URL` | `http://embeddings:8000` | Embedding service URL |
| `EMBEDDING_MODEL_NAME` | `BAAI/bge-small-en-v1.5` | Embedding model identifier |
| `EMBEDDING_REQUEST_TIMEOUT_MS` | `30000` | Embed request timeout |
| `EMBEDDING_MAX_BATCH_SIZE` | `64` | Max embedding batch size |
| `EMBEDDING_RETRY_COUNT` | `3` | Embed retry count |
| `ARXIV_RATE_LIMIT_DELAY` | `3` | Delay between requests (seconds) |
| `ARXIV_BATCH_SIZE` | `2000` | Records per batch |
| `ARXIV_MAX_RETRIES` | `3` | Max arXiv retries |
| `ARXIV_RETRY_AFTER` | `5` | Retry delay (seconds) |

Additional accelerator/runtime variables for embeddings container:

- `DEVICE=cpu|cuda|mlx`
- `ORT_EXECUTION_PROVIDER=CPU|CUDA`
- `ORT_INTRA_THREADS`, `ORT_INTER_THREADS`, `ORT_GRAPH_OPT_LEVEL`
- `ACCELERATOR_BACKEND=onnx|mlx`
- `ACCELERATOR_FALLBACK_TO_CPU=true|false`
- `MODEL_PATH`, `TOKENIZER_PATH`, `CUDA_VISIBLE_DEVICES`

> `docker-compose.yaml` now runs normal application workflows on Qdrant +
> embeddings only.

## Usage

### CLI

```bash
./arhida-cpp --mode recent
./arhida-cpp --mode backfill --start-date 2020-01-01 --end-date 2020-01-31
./arhida-cpp --mode both --set-specs physics math cs
```

### Docker Compose

```bash
docker-compose pull
docker-compose up -d

# one-off run
docker-compose run --rm app ./arhida-cpp --mode recent
```

### Deployment mode examples

CPU-only:

```bash
DEVICE=cpu ORT_EXECUTION_PROVIDER=CPU ACCELERATOR_BACKEND=onnx docker-compose up -d
```

CUDA-enabled:

```bash
DEVICE=cuda ORT_EXECUTION_PROVIDER=CUDA ACCELERATOR_BACKEND=onnx docker-compose up -d
```

Apple Silicon / MLX development-only path (separately validated; not part of
the supported Compose deployment contract):

```bash
DEVICE=mlx ACCELERATOR_BACKEND=mlx docker-compose up -d
```

Optional accelerator fallback-to-CPU behavior (when an unsupported accelerator
configuration is requested):

```bash
ACCELERATOR_FALLBACK_TO_CPU=true docker-compose up -d embeddings
```

CUDA requires a CUDA-capable host, a compatible NVIDIA driver, and a runtime
image with the matching ONNX Runtime CUDA dependencies. Compose does not
reserve or pass through a GPU, so operators must provide GPU access to the
embeddings container separately; setting `DEVICE=cuda` alone does not make a
CPU-only host CUDA-capable. MLX is a development/validation path only and is
not an operational promise of the Compose image.

## Model Artifacts and Volume Mounting

The embeddings container expects model artifacts mounted read-only from the
local `./models` directory.

Expected layout:

```text
/models/
  bge-small-en-v1.5/
    model.onnx
    tokenizer/
      tokenizer.json
      tokenizer_config.json
      special_tokens_map.json
      vocab.txt (or equivalent vocab files)
```

Compose wiring (already present in `docker-compose.yaml`):

- `embeddings` is pulled from `ghcr.io/chasekb/arhida-embeddings:latest`
- the service still mounts local model artifacts from `./models:/models:ro`
- `MODEL_PATH=/models/bge-small-en-v1.5/model.onnx`
- `TOKENIZER_PATH=/models/bge-small-en-v1.5/tokenizer`

### Artifact Preparation Steps (Phase 7)

1. Export/pin the embedding model to ONNX format (`model.onnx`) for the selected
   model revision.
2. Collect tokenizer assets for the same model revision (must include
   `tokenizer.json`).
3. Place artifacts into the mounted layout under `/models`:

```text
/models/
  bge-small-en-v1.5/
    model.onnx
    tokenizer/
      tokenizer.json
      tokenizer_config.json
      special_tokens_map.json
      vocab.txt (or equivalent vocab files)
```

4. Start the embeddings service with strict validation enabled
   (`STRICT_MODEL_VALIDATION=true`, default behavior).
5. Verify startup health:

```bash
curl -fsS http://localhost:8000/health | jq
```

The service startup will fail fast when `MODEL_PATH` (`model.onnx`) or
`TOKENIZER_PATH/tokenizer.json` is missing, preventing partial/misconfigured
deployments. The display/model identifier is `BAAI/bge-small-en-v1.5`; the
artifact directory mounted by this repository is `bge-small-en-v1.5`.

### Model Upgrade/Rollback Workflow

Recommended model lifecycle:

1. Stage new artifacts under a new model directory (for example
   `/models/bge-small-en-v1.5-r2/`).
2. Update compose/runtime variables (`MODEL_NAME`, `MODEL_PATH`,
   `TOKENIZER_PATH`, `VECTOR_SIZE`) to the new model revision.
3. Recreate the Qdrant collection so vector dimension/schema aligns with the new
   model output.
4. Restart embeddings and app services, then run health checks and ingestion
   smoke checks.
5. Keep prior model artifacts available for rapid rollback.

Rollback strategy:

- revert `MODEL_*`/`VECTOR_SIZE` env values to the previous model revision
- recreate collection for the previous dimension if needed
- restart services and re-run health checks

## Collection Lifecycle (Rebuild/Recreate)

If you change embedding model or vector dimension, recreate the collection so
Qdrant schema matches output vectors.

```bash
# stop app writes first
docker-compose stop app

# delete old collection
curl -X DELETE "http://localhost:6333/collections/arxiv_metadata"

# restart app so it re-creates collection with configured VECTOR_SIZE
docker-compose up -d app
```

> Use the configured collection name from `QDRANT_COLLECTION` instead of
> `arxiv_metadata` when different.

## Migration Workflow (PostgreSQL -> Qdrant)

Current migration posture:

1. Run harvester in vector mode (`VECTOR_DB_PROVIDER=qdrant`).
2. Validate embeddings service health (`/health`) and Qdrant health (`/healthz`).
3. Backfill and recent runs write vectors + payloads into Qdrant.
4. Execute historical PostgreSQL migration utility when legacy data migration is needed.
5. Verify data parity/coverage before final PostgreSQL decommission steps.

### High-Impact Migration Mode (Keyset + C++ Embeddings)

`scripts/postgres_to_qdrant_migration.sh` now supports migration-optimized behavior:

- keyset pagination (`id > last_row_id`) in migrator read path
- checkpoint resume with both `offset` and `last_row_id`
- optional C++ embeddings service launched via Podman container (`USE_CPP_EMBEDDINGS_SERVICE=true`)
- stage-aware execution with `MIGRATION_STAGE=migrate|verify|all`
- Podman network override for the source database with `MIGRATION_PODMAN_NETWORK=db_prdnet`

For a two-step cutover, use the wrapper script:

```bash
MIGRATION_STAGE=migrate bash scripts/postgres_to_qdrant_migration.sh
MIGRATION_STAGE=verify bash scripts/postgres_to_qdrant_migration.sh
```

Or run both stages in sequence:

```bash
bash scripts/postgres_to_qdrant_cutover.sh
```

Podman Compose note:

`podman-compose run` cannot join the external `db_prdnet` network with the
current compose file. For network-aware migration, use
`scripts/postgres_to_qdrant_migration.sh` or a plain `podman run` invocation.

The migration script selects its connection defaults from
`MIGRATION_PODMAN_NETWORK`. With `MIGRATION_PODMAN_NETWORK=host`, it uses
`POSTGRES_HOST=host.containers.internal`, Qdrant at
`http://127.0.0.1:6333`, and embeddings at `http://127.0.0.1:8000`. With the
default Podman network (`db_prdnet`), the migration container joins that
network, uses `POSTGRES_HOST=postgres`, and reaches host-published services
through `host.containers.internal:6333` (and `:8000`/`:18000` for the other
services). Override these values explicitly when your deployment differs.

If your PostgreSQL endpoint is published on the host, the legacy host-based
form is:

```bash
POSTGRES_HOST=host.containers.internal \
POSTGRES_PORT=5432 \
POSTGRES_DB=unordered_map \
POSTGRES_USER=postgres \
POSTGRES_PASSWORD='<password>' \
POSTGRES_SCHEMA=priority_queue \
POSTGRES_TABLE=arxiv \
QDRANT_COLLECTION=arxiv_metadata_from_postgres_YYYYMMDD \
bash scripts/postgres_to_qdrant_migration.sh
```

Recommended migration invocation from the host (Qdrant is published on port
6333):

```bash
POSTGRES_SCHEMA=priority_queue \
POSTGRES_TABLE=arxiv \
QDRANT_URL=http://127.0.0.1:6333 \
QDRANT_COLLECTION=arxiv_metadata_from_postgres_YYYYMMDD \
CHECKPOINT_FILE=.migration/postgres_to_qdrant_checkpoint.json \
USE_CPP_EMBEDDINGS_SERVICE=true \
CPP_EMBEDDINGS_URL=http://127.0.0.1:18000 \
CHUNK_SIZE=400 \
EMBEDDING_BATCH_SIZE=64 \
RESUME=true \
bash scripts/postgres_to_qdrant_migration.sh
```

### Running Against a Quiesced Source PostgreSQL

For maximum throughput and deterministic runtime, migrate from a quiesced source snapshot:

1. Stop/suspend writers to `POSTGRES_SCHEMA.POSTGRES_TABLE`.
2. Validate row count stability before migration:

```bash
psql "postgresql://<user>:<pass>@<host>:<port>/<db>" \
  -c "SELECT COUNT(*) FROM priority_queue.arxiv;"
```

3. Run migration with checkpoint + keyset mode enabled (default in current migrator implementation).
4. Re-check source count and compare with the exact Qdrant point count. Then
   verify a sample of identifiers and compare the corresponding date and
   `setSpec` counts:

```bash
curl -sS http://127.0.0.1:6333/collections/<collection>/points/count \
  -H 'Content-Type: application/json' \
  --data '{"exact":true}'
```

5. Treat parity as exact total-count equality plus sampled identifier/date/set
   checks; it is not a full record-by-record comparison. If parity passes,
   re-enable normal write workflows.

## Backup and Restore (Qdrant Storage)

Qdrant data is persisted in the project-local bind mount at `./data/qdrant`.

Backup:

```bash
docker run --rm \
  -v "$PWD/data/qdrant":/source:ro \
  -v "$PWD":/backup \
  alpine tar czf /backup/qdrant-storage-backup.tgz -C /source .
```

Restore:

```bash
docker-compose stop qdrant
docker run --rm \
  -v "$PWD/data/qdrant":/target \
  -v "$PWD":/backup \
  alpine sh -c "cd /target && tar xzf /backup/qdrant-storage-backup.tgz"
docker-compose up -d qdrant
curl -fsS http://localhost:6333/healthz
curl -fsS http://localhost:6333/collections
```

Stop or otherwise quiesce every live Qdrant process before restoring the
storage directory. After restoring, start Qdrant and verify its health and
expected collections before resuming application writes.

## Operational Health Checks

Expected service endpoints:

- App container healthcheck: `./arhida-cpp --help`
- Qdrant health: `GET http://localhost:6333/healthz`
- Embeddings health: `GET http://localhost:8000/health`

Quick verification:

```bash
curl -fsS http://localhost:6333/healthz
curl -fsS http://localhost:8000/health
docker-compose ps
```

Embeddings health payload now includes accelerator/runtime metadata for
operational validation, including:

- selected backend (`backend`)
- active execution provider (`execution_provider`)
- requested ORT execution provider (`requested_ort_execution_provider`)
- ORT tuning knobs (`ort_intra_threads`, `ort_inter_threads`,
  `ort_graph_optimization_level`)
- accelerator fallback policy (`accelerator_fallback_enabled`)

Qdrant storage smoke check (collection create + upsert + verify + cleanup):

```bash
bash scripts/qdrant_smoke.sh
```

Embeddings service smoke check (`/health` + `/embed` shape + unit-norm verification + deterministic output + whitespace/tokenization behavior + oversized batch guard):

```bash
bash scripts/embeddings_smoke.sh
```

Embeddings service benchmark check (latency summary across representative batch sizes):

```bash
bash scripts/embeddings_benchmark.sh
```

App mode smoke check (runs `recent` and `backfill` via compose against qdrant/embeddings):

```bash
bash scripts/app_modes_smoke.sh
```

Embeddings unavailable failure smoke check (verifies app fails fast when embeddings health check fails at startup):

```bash
bash scripts/embeddings_unavailable_smoke.sh
```

Qdrant unavailable failure smoke check (verifies app fails fast when qdrant connectivity fails at startup/storage initialization):

```bash
bash scripts/qdrant_unavailable_smoke.sh
```

Model artifacts missing failure smoke check (verifies embeddings service fails fast when required model/tokenizer assets are missing):

```bash
bash scripts/model_artifacts_missing_smoke.sh
```

Transient embeddings retry failure smoke check (verifies app retries and recovers when the embeddings service returns an initial transient error):

```bash
bash scripts/embedding_retry_transient_smoke.sh
```

Accelerator unavailable failure smoke check (verifies embeddings service fails fast when an unavailable/unsupported accelerator mode is requested):

```bash
bash scripts/accelerator_unavailable_smoke.sh
```

Qdrant deterministic-id/idempotent-upsert smoke check (verifies duplicate upserts update existing points and deterministic point IDs remain stable):

```bash
bash scripts/qdrant_idempotent_upsert_smoke.sh
```

Qdrant dimension/payload smoke check (verifies collection dimension alignment and representative payload field serialization):

```bash
bash scripts/qdrant_dimension_payload_smoke.sh
```

Qdrant set/date filter smoke check (verifies `header_setSpecs` + `header_datestamp` filtering semantics used by backfill missing-date logic):

```bash
bash scripts/qdrant_set_date_filter_smoke.sh
```

Qdrant persistence smoke check (verifies point data survives qdrant restart with compose-backed storage):

```bash
bash scripts/qdrant_persistence_smoke.sh
```

App dependency readiness smoke check (verifies app startup fails without qdrant/embeddings and succeeds once both dependencies are healthy):

```bash
bash scripts/app_dependency_readiness_smoke.sh
```

One-record ingest smoke check (verifies at least one record is harvested, embedded, and persisted to Qdrant in `recent` mode):

```bash
bash scripts/one_record_ingest_smoke.sh
```

Batch-ingestion smoke check (verifies multi-record batch persistence via `recent` mode into an isolated Qdrant collection):

```bash
bash scripts/batch_ingestion_smoke.sh
```

Recent-mode end-to-end smoke check (verifies compose-backed `recent` mode persists data into an isolated Qdrant collection):

```bash
bash scripts/recent_mode_e2e_smoke.sh
```

Backfill-mode end-to-end smoke check (verifies compose-backed `backfill` mode persists data into an isolated Qdrant collection):

```bash
bash scripts/backfill_mode_e2e_smoke.sh
```

Embeddings CPU throughput benchmark (starts embeddings in CPU mode and runs representative batch-size latency benchmark sweep):

```bash
bash scripts/embeddings_cpu_throughput_benchmark.sh
```

Embeddings CUDA throughput benchmark (starts embeddings in CUDA mode and runs representative batch-size latency benchmark sweep):

```bash
bash scripts/embeddings_cuda_throughput_benchmark.sh
```

Embeddings MLX throughput benchmark (starts embeddings in MLX mode and runs representative batch-size latency benchmark sweep):

```bash
bash scripts/embeddings_mlx_throughput_benchmark.sh
```

End-to-end ingestion throughput benchmark (runs `recent` ingestion with configurable app/embed batch-size sweep and reports records/sec):

```bash
bash scripts/ingestion_throughput_benchmark.sh
```

## Project Structure

```
arhida/
├── CMakeLists.txt           # Build configuration
├── Dockerfile              # Docker build
├── docker-compose.yaml     # Container orchestration
├── docker-compose.build.yaml # Local build configuration overlay
├── embeddings_service/     # Embeddings service
├── tests/                  # C++ tests
├── scripts/                # Operational and smoke-check scripts
├── config/                 # Runtime configuration
├── docs/                   # Project documentation
├── models/                 # Local model artifacts (not runtime data)
├── .github/workflows/      # CI workflows
├── include/                # Header files
│   ├── config/
│   ├── db/
│   ├── harvester/
│   ├── oai/
│   └── utils/
├── src/                   # Source files
│   ├── main.cpp
│   ├── config/
│   ├── db/
│   ├── harvester/
│   ├── oai/
│   └── utils/
└── legacy_python/         # Python reference implementation
```

Runtime data such as `data/qdrant/` and local logs are deployment state and
are not part of the committed source tree.

## Persistence Model

Qdrant points contain:

- deterministic id from `header_identifier`
- embedding vector (`VECTOR_SIZE`)
- payload fields for harvested metadata:
  - `header_*` values
  - `metadata_*` values

## Rate Limiting

The harvester defaults to a 3-second delay (`ARXIV_RATE_LIMIT_DELAY=3`). It
waits before each request and between retries, and retries failed OAI-PMH
requests up to `ARXIV_MAX_RETRIES` (default `3`), with the configured delay
between attempts. Operators remain responsible for checking and complying
with arXiv.org's current rate, query-size, and usage policy; the client does
not guarantee a universal 30,000-result cap.

## License

MIT License

## Author

Bernard Chase
