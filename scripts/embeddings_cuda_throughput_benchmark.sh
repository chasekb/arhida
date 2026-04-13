#!/usr/bin/env bash
set -euo pipefail

# Performance-validation harness: measure embeddings throughput in CUDA mode.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
EMBEDDINGS_URL="${EMBEDDINGS_URL:-http://localhost:8000}"

echo "[embeddings-cuda-bench] starting embeddings service in CUDA mode"
DEVICE=cuda ACCELERATOR_BACKEND=onnx ORT_EXECUTION_PROVIDER=CUDA ACCELERATOR_FALLBACK_TO_CPU=false \
  ${COMPOSE_CMD} up -d "${EMBEDDINGS_SERVICE}"

echo "[embeddings-cuda-bench] running benchmark sweep"
EMBEDDINGS_URL="${EMBEDDINGS_URL}" bash scripts/embeddings_benchmark.sh

echo "[embeddings-cuda-bench] completed"
