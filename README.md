# arXiv Academic Paper Metadata Harvester (C++)

High-performance C++ harvester for arXiv OAI-PMH metadata with a vector-first
runtime path.

The active migration target is:

- **Qdrant** as the primary persistence backend
- **Embeddings service** (`/health`, `/embed`) for vector generation
- **PostgreSQL kept temporarily** for migration compatibility tooling

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
- `embeddings` (`ghcr.io/chasekb/arhida-embeddings:local`)

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
| `EMBEDDING_MODEL_NAME` | `bge-small-en-v1.5` | Embedding model identifier |
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
- `ACCELERATOR_BACKEND=onnx|mlx`
- `MODEL_PATH`, `TOKENIZER_PATH`, `CUDA_VISIBLE_DEVICES`

> Transitional PostgreSQL env vars remain in compose for migration utilities.

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

Apple Silicon / MLX development path:

```bash
DEVICE=mlx ACCELERATOR_BACKEND=mlx docker-compose up -d
```

## Model Artifacts and Volume Mounting

The embeddings container expects model artifacts mounted read-only from the
`model-files` volume.

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

- `model-files:/models:ro`
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

The service startup will fail fast when `MODEL_PATH` or tokenizer assets are
missing, preventing partial/misconfigured deployments.

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

1. Keep legacy PostgreSQL env/secrets available only for migration tooling.
2. Run harvester in vector mode (`VECTOR_DB_PROVIDER=qdrant`).
3. Validate embeddings service health (`/health`) and Qdrant health (`/healthz`).
4. Backfill and recent runs write vectors + payloads into Qdrant.
5. Verify data parity/coverage before removing Postgres runtime dependencies.

## Backup and Restore (Qdrant Storage)

Qdrant data is persisted in the `qdrant-storage` Docker volume.

Backup:

```bash
docker run --rm \
  -v qdrant-storage:/source \
  -v "$PWD":/backup \
  alpine tar czf /backup/qdrant-storage-backup.tgz -C /source .
```

Restore:

```bash
docker run --rm \
  -v qdrant-storage:/target \
  -v "$PWD":/backup \
  alpine sh -c "cd /target && tar xzf /backup/qdrant-storage-backup.tgz"
```

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

Qdrant storage smoke check (collection create + upsert + verify + cleanup):

```bash
bash scripts/qdrant_smoke.sh
```

Embeddings service smoke check (`/health` + `/embed` + oversized batch guard):

```bash
bash scripts/embeddings_smoke.sh
```

## Project Structure

```
arhida/
├── CMakeLists.txt           # Build configuration
├── Dockerfile              # Docker build
├── docker-compose.yaml     # Container orchestration
├── docker-compose.build.yaml # Local build configuration overlay
├── include/               # Header files
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

## Persistence Model

Qdrant points contain:

- deterministic id from `header_identifier`
- embedding vector (`VECTOR_SIZE`)
- payload fields for harvested metadata:
  - `header_*` values
  - `metadata_*` values

## Rate Limiting

The harvester complies with arXiv.org's usage constraints:

- Maximum 1 request every 3 seconds
- Single connection at a time
- Maximum 30,000 results per query

## License

MIT License

## Author

Bernard Chase
