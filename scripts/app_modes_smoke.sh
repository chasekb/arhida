#!/usr/bin/env bash
set -euo pipefail

# End-to-end smoke harness for app runtime modes against compose services.
# Intended for remote/CI validation where compose stack is available.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
QDRANT_URL="${QDRANT_URL:-http://localhost:6333/healthz}"
EMBEDDINGS_URL="${EMBEDDINGS_URL:-http://localhost:8000/health}"

BACKFILL_START_DATE="${BACKFILL_START_DATE:-2020-01-01}"
BACKFILL_END_DATE="${BACKFILL_END_DATE:-2020-01-01}"
SET_SPECS="${SET_SPECS:-physics}"

echo "[app-smoke] starting compose dependencies"
${COMPOSE_CMD} up -d qdrant embeddings

echo "[app-smoke] waiting for qdrant health (${QDRANT_URL})"
for _ in $(seq 1 60); do
  if curl -fsS "${QDRANT_URL}" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}" >/dev/null

echo "[app-smoke] waiting for embeddings health (${EMBEDDINGS_URL})"
for _ in $(seq 1 60); do
  if curl -fsS "${EMBEDDINGS_URL}" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${EMBEDDINGS_URL}" >/dev/null

echo "[app-smoke] running recent mode"
${COMPOSE_CMD} run --rm ${APP_SERVICE} ./arhida-cpp --mode recent

echo "[app-smoke] running backfill mode start=${BACKFILL_START_DATE} end=${BACKFILL_END_DATE} set_specs=${SET_SPECS}"
${COMPOSE_CMD} run --rm ${APP_SERVICE} \
  ./arhida-cpp --mode backfill \
  --start-date "${BACKFILL_START_DATE}" \
  --end-date "${BACKFILL_END_DATE}" \
  --set-specs "${SET_SPECS}"

echo "[app-smoke] recent/backfill mode smoke run completed"
