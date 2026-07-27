#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
DEFAULT_CHECKPOINT_FILE="${ROOT_DIR}/.migration/postgres_to_qdrant_checkpoint.json"
CHECKPOINT_FILE="${CHECKPOINT_FILE:-${DEFAULT_CHECKPOINT_FILE}}"
CHUNK_SIZE="${CHUNK_SIZE:-200}"
EMBEDDING_BATCH_SIZE="${EMBEDDING_BATCH_SIZE:-64}"
PARITY_SAMPLE_SIZE="${PARITY_SAMPLE_SIZE:-25}"
RESUME="${RESUME:-true}"
AUTO_CHECKPOINT_RESUME="${AUTO_CHECKPOINT_RESUME:-true}"
USE_CPP_EMBEDDINGS_SERVICE="${USE_CPP_EMBEDDINGS_SERVICE:-true}"
MIGRATION_STAGE="${MIGRATION_STAGE:-all}"
MIGRATION_PODMAN_NETWORK="${MIGRATION_PODMAN_NETWORK:-db_prdnet}"
CONTAINER_BRIDGE_HOST="${CONTAINER_BRIDGE_HOST:-host.containers.internal}"
CPP_EMBEDDINGS_IMAGE="${CPP_EMBEDDINGS_IMAGE:-localhost/arhida-embeddings-cpp:local}"
CPP_EMBEDDINGS_CONTAINER="${CPP_EMBEDDINGS_CONTAINER:-arhida-embeddings-migration-cpp}"
MIGRATION_IMAGE="${MIGRATION_IMAGE:-localhost/arhida-migrate:local}"

if [[ "${MIGRATION_PODMAN_NETWORK}" == "host" ]]; then
  POSTGRES_HOST="${POSTGRES_HOST:-host.containers.internal}"
  QDRANT_URL="${QDRANT_URL:-http://127.0.0.1:6333}"
  EMBEDDING_SERVICE_URL="${EMBEDDING_SERVICE_URL:-http://127.0.0.1:8000}"
  CPP_EMBEDDINGS_URL="${CPP_EMBEDDINGS_URL:-http://127.0.0.1:18000}"
  PODMAN_NETWORK_ARGS=(--network=host)
else
  POSTGRES_HOST="${POSTGRES_HOST:-postgres}"
  QDRANT_URL="${QDRANT_URL:-http://${CONTAINER_BRIDGE_HOST}:6333}"
  EMBEDDING_SERVICE_URL="${EMBEDDING_SERVICE_URL:-http://${CONTAINER_BRIDGE_HOST}:8000}"
  CPP_EMBEDDINGS_URL="${CPP_EMBEDDINGS_URL:-http://${CONTAINER_BRIDGE_HOST}:18000}"
  PODMAN_NETWORK_ARGS=(--network="${MIGRATION_PODMAN_NETWORK}")
fi

STARTED_CPP_EMBED_CONTAINER="false"
mkdir -p "${ROOT_DIR}/logs"

if [[ "${RESUME}" == "true" && "${AUTO_CHECKPOINT_RESUME}" == "true" ]]; then
  if [[ ! -f "${CHECKPOINT_FILE}" ]]; then
    CHECKPOINT_CANDIDATE="$(python3 - <<'PY' "${ROOT_DIR}"
import glob
import json
import os
import sys

root = sys.argv[1]
pattern = os.path.join(root, '.migration', 'postgres_to_qdrant_checkpoint*.json')
candidates = []
for path in glob.glob(pattern):
    try:
        with open(path, 'r', encoding='utf-8') as fh:
            payload = json.load(fh)
        if bool(payload.get('completed', False)):
            continue
        offset = int(payload.get('offset', 0))
        updated = int(payload.get('updated_at_epoch', 0))
        candidates.append((offset, updated, path))
    except Exception:
        continue

if not candidates:
    print('')
else:
    candidates.sort(key=lambda item: (item[0], item[1], item[2]), reverse=True)
    print(candidates[0][2])
PY
)"
    if [[ -n "${CHECKPOINT_CANDIDATE}" ]]; then
      CHECKPOINT_FILE="${CHECKPOINT_CANDIDATE}"
      echo "[migration] auto-selected checkpoint ${CHECKPOINT_FILE}"
    fi
  fi
fi

CHECKPOINT_DIR="$(dirname "${CHECKPOINT_FILE}")"
CHECKPOINT_BASENAME="$(basename "${CHECKPOINT_FILE}")"

cleanup() {
  if [[ "${STARTED_CPP_EMBED_CONTAINER}" == "true" ]]; then
    podman rm -f "${CPP_EMBEDDINGS_CONTAINER}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

if [[ "${USE_CPP_EMBEDDINGS_SERVICE}" == "true" ]]; then
  if ! curl -fsS "${EMBEDDING_SERVICE_URL}/health" >/dev/null 2>&1; then
    echo "[migration] building cpp embeddings container ${CPP_EMBEDDINGS_IMAGE}"
    podman build --pull=missing -t "${CPP_EMBEDDINGS_IMAGE}" \
      -f "${ROOT_DIR}/embeddings_service/Dockerfile" "${ROOT_DIR}" >/dev/null

    echo "[migration] starting cpp embeddings container ${CPP_EMBEDDINGS_CONTAINER}"
    podman rm -f "${CPP_EMBEDDINGS_CONTAINER}" >/dev/null 2>&1 || true
    podman run -d --name "${CPP_EMBEDDINGS_CONTAINER}" \
      --pull=never \
      -p 18000:8000 \
      -e MODEL_NAME="${EMBEDDING_MODEL_NAME:-BAAI/bge-small-en-v1.5}" \
      -e MODEL_DIMENSION="${VECTOR_SIZE:-384}" \
      -e MAX_BATCH_SIZE="${EMBEDDING_MAX_BATCH_SIZE:-64}" \
      -e MODEL_PATH="${MODEL_PATH:-/models/bge-small-en-v1.5/model.onnx}" \
      -e TOKENIZER_PATH="${TOKENIZER_PATH:-/models/bge-small-en-v1.5/tokenizer}" \
      -e DEVICE="${DEVICE:-cpu}" \
      -e ORT_EXECUTION_PROVIDER="${ORT_EXECUTION_PROVIDER:-CPU}" \
      -e ORT_INTRA_THREADS="${ORT_INTRA_THREADS:-0}" \
      -e ORT_INTER_THREADS="${ORT_INTER_THREADS:-0}" \
      -e ORT_GRAPH_OPT_LEVEL="${ORT_GRAPH_OPT_LEVEL:-all}" \
      -e ACCELERATOR_BACKEND="${ACCELERATOR_BACKEND:-onnx}" \
      -e ACCELERATOR_FALLBACK_TO_CPU="${ACCELERATOR_FALLBACK_TO_CPU:-false}" \
      -e REQUEST_TIMEOUT_MS="${EMBEDDING_REQUEST_TIMEOUT_MS:-30000}" \
      -e SERVICE_PORT=8000 \
      -e STRICT_MODEL_VALIDATION="${STRICT_MODEL_VALIDATION:-true}" \
      -v "${ROOT_DIR}/models:/models:ro" \
      "${CPP_EMBEDDINGS_IMAGE}" >/dev/null
    STARTED_CPP_EMBED_CONTAINER="true"

    for _ in $(seq 1 60); do
      if curl -fsS "${EMBEDDING_SERVICE_URL}/health" >/dev/null 2>&1; then
        break
      fi
      sleep 1
    done

    if ! curl -fsS "${EMBEDDING_SERVICE_URL}/health" >/dev/null 2>&1; then
      podman logs --tail 120 "${CPP_EMBEDDINGS_CONTAINER}" || true
      echo "[migration] cpp embeddings container failed health check"
      exit 1
    fi

    curl -fsS "${EMBEDDING_SERVICE_URL}/health" >/dev/null
  else
    echo "[migration] reusing existing embeddings service at ${EMBEDDING_SERVICE_URL}"
  fi
fi

mkdir -p "${CHECKPOINT_DIR}"

case "${MIGRATION_STAGE}" in
  all|migrate|verify)
    ;;
  *)
    echo "[migration] invalid MIGRATION_STAGE=${MIGRATION_STAGE}. Expected all, migrate, or verify."
    exit 1
    ;;
esac

echo "[migration] building migration image ${MIGRATION_IMAGE}"
podman build --pull=missing \
  --build-arg BUILD_MIGRATION_TOOL=ON \
  -t "${MIGRATION_IMAGE}" \
  -f "${ROOT_DIR}/Dockerfile" "${ROOT_DIR}" >/dev/null

ARGS=(
  --chunk-size "${CHUNK_SIZE}"
  --embedding-batch-size "${EMBEDDING_BATCH_SIZE}"
  --parity-sample-size "${PARITY_SAMPLE_SIZE}"
  --checkpoint-file "/checkpoint/${CHECKPOINT_BASENAME}"
)

if [[ "${RESUME}" != "true" ]]; then
  ARGS+=(--no-resume)
fi

case "${MIGRATION_STAGE}" in
  migrate)
    ARGS+=(--migrate-only)
    echo "[migration] running PostgreSQL -> Qdrant migration stage"
    ;;
  verify)
    ARGS+=(--verify-only)
    echo "[migration] running PostgreSQL -> Qdrant verification stage"
    ;;
  all)
    echo "[migration] running PostgreSQL -> Qdrant migration and verification"
    ;;
esac

podman run --rm \
  --pull=never \
  "${PODMAN_NETWORK_ARGS[@]}" \
  -e VECTOR_DB_PROVIDER=qdrant \
  -e POSTGRES_HOST="${POSTGRES_HOST}" \
  -e POSTGRES_PORT="${POSTGRES_PORT:-5432}" \
  -e POSTGRES_DB="${POSTGRES_DB:-}" \
  -e POSTGRES_USER="${POSTGRES_USER:-}" \
  -e POSTGRES_PASSWORD="${POSTGRES_PASSWORD:-}" \
  -e POSTGRES_SCHEMA="${POSTGRES_SCHEMA:-arxiv}" \
  -e POSTGRES_TABLE="${POSTGRES_TABLE:-metadata}" \
  -e QDRANT_URL="${QDRANT_URL}" \
  -e QDRANT_COLLECTION="${QDRANT_COLLECTION:-arxiv_metadata}" \
  -e VECTOR_SIZE="${VECTOR_SIZE:-384}" \
  -e EMBEDDING_SERVICE_URL="${EMBEDDING_SERVICE_URL}" \
  -v "${CHECKPOINT_DIR}:/checkpoint:Z" \
  "${MIGRATION_IMAGE}" \
  /app/arhida-migrate \
  "${ARGS[@]}"

echo "[migration] completed"
