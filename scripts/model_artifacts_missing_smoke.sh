#!/usr/bin/env bash
set -euo pipefail

# Failure-mode smoke harness: verify embeddings service fails fast when model
# artifacts are missing.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"

INVALID_MODEL_PATH="${INVALID_MODEL_PATH:-/models/nonexistent/model.onnx}"
INVALID_TOKENIZER_PATH="${INVALID_TOKENIZER_PATH:-/models/nonexistent/tokenizer}"

set +e
SERVICE_OUTPUT="$(${COMPOSE_CMD} run --rm --no-deps \
  -e STRICT_MODEL_VALIDATION=true \
  -e MODEL_PATH=${INVALID_MODEL_PATH} \
  -e TOKENIZER_PATH=${INVALID_TOKENIZER_PATH} \
  ${EMBEDDINGS_SERVICE} 2>&1)"
SERVICE_EXIT=$?
set -e

if [[ ${SERVICE_EXIT} -eq 0 ]]; then
  echo "[failure-smoke] expected embeddings service startup to fail with missing artifacts"
  echo "${SERVICE_OUTPUT}"
  exit 1
fi

if [[ "${SERVICE_OUTPUT}" != *"Model artifact not found at:"* ]]; then
  echo "[failure-smoke] embeddings service failed, but expected missing model artifact error was not observed"
  echo "${SERVICE_OUTPUT}"
  exit 1
fi

echo "[failure-smoke] verified fail-fast behavior when model artifacts are missing"
