#!/usr/bin/env bash
set -euo pipefail

# Performance-validation harness: measure embeddings throughput in CPU mode.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
EMBEDDINGS_URL="${EMBEDDINGS_URL:-http://localhost:8000}"

echo "[embeddings-cpu-bench] starting embeddings service in CPU mode"
DEVICE=cpu ACCELERATOR_BACKEND=onnx ORT_EXECUTION_PROVIDER=CPU \
  ${COMPOSE_CMD} up -d "${EMBEDDINGS_SERVICE}"

echo "[embeddings-cpu-bench] running benchmark sweep"
EMBEDDINGS_URL="${EMBEDDINGS_URL}" bash scripts/embeddings_benchmark.sh

echo "[embeddings-cpu-bench] completed"
