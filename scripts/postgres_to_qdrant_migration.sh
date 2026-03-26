#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
CHECKPOINT_FILE="${CHECKPOINT_FILE:-${ROOT_DIR}/.migration/postgres_to_qdrant_checkpoint.json}"
CHUNK_SIZE="${CHUNK_SIZE:-200}"
EMBEDDING_BATCH_SIZE="${EMBEDDING_BATCH_SIZE:-64}"
PARITY_SAMPLE_SIZE="${PARITY_SAMPLE_SIZE:-25}"
RESUME="${RESUME:-true}"

echo "[migration] building arhida-migrate target in ${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" >/dev/null
cmake --build "${BUILD_DIR}" --target arhida-migrate >/dev/null

ARGS=(
  --chunk-size "${CHUNK_SIZE}"
  --embedding-batch-size "${EMBEDDING_BATCH_SIZE}"
  --parity-sample-size "${PARITY_SAMPLE_SIZE}"
  --checkpoint-file "${CHECKPOINT_FILE}"
)

if [[ "${RESUME}" != "true" ]]; then
  ARGS+=(--no-resume)
fi

echo "[migration] running PostgreSQL -> Qdrant migration"
"${BUILD_DIR}/arhida-migrate" "${ARGS[@]}"

echo "[migration] completed"
