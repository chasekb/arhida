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
