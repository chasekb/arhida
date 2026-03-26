#!/usr/bin/env bash
set -euo pipefail

# Performance-validation harness: measure embeddings throughput in MLX mode.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
EMBEDDINGS_URL="${EMBEDDINGS_URL:-http://localhost:8000}"

echo "[embeddings-mlx-bench] starting embeddings service in MLX mode"
DEVICE=mlx ACCELERATOR_BACKEND=mlx ACCELERATOR_FALLBACK_TO_CPU=false \
  ${COMPOSE_CMD} up -d "${EMBEDDINGS_SERVICE}"

echo "[embeddings-mlx-bench] running benchmark sweep"
EMBEDDINGS_URL="${EMBEDDINGS_URL}" bash scripts/embeddings_benchmark.sh

echo "[embeddings-mlx-bench] completed"
