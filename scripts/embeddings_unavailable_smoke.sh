#!/usr/bin/env bash
set -euo pipefail

# Failure-mode smoke harness: verify app fails fast when embeddings is unavailable.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
QDRANT_URL="${QDRANT_URL:-http://localhost:6333/healthz}"

echo "[failure-smoke] starting qdrant dependency only"
${COMPOSE_CMD} up -d qdrant

echo "[failure-smoke] ensuring embeddings service is not running"
${COMPOSE_CMD} stop embeddings >/dev/null 2>&1 || true

echo "[failure-smoke] waiting for qdrant health (${QDRANT_URL})"
for _ in $(seq 1 60); do
  if curl -fsS "${QDRANT_URL}" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}" >/dev/null

set +e
APP_OUTPUT="$(${COMPOSE_CMD} run --rm --no-deps ${APP_SERVICE} ./arhida-cpp --mode recent 2>&1)"
APP_EXIT=$?
set -e

if [[ ${APP_EXIT} -eq 0 ]]; then
  echo "[failure-smoke] expected app to fail when embeddings is unavailable"
  echo "${APP_OUTPUT}"
  exit 1
fi

if [[ "${APP_OUTPUT}" != *"Embeddings service health check failed during startup"* ]]; then
  echo "[failure-smoke] app failed, but expected embeddings startup health error was not observed"
  echo "${APP_OUTPUT}"
  exit 1
fi

echo "[failure-smoke] verified fail-fast behavior when embeddings is unavailable"
