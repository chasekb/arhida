#!/usr/bin/env bash
set -euo pipefail

# Failure-mode smoke harness: verify app fails fast when Qdrant is unavailable.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
EMBEDDINGS_URL="${EMBEDDINGS_URL:-http://localhost:8000/health}"

echo "[failure-smoke] starting embeddings dependency only"
${COMPOSE_CMD} up -d embeddings

echo "[failure-smoke] ensuring qdrant service is not running"
${COMPOSE_CMD} stop qdrant >/dev/null 2>&1 || true

echo "[failure-smoke] waiting for embeddings health (${EMBEDDINGS_URL})"
for _ in $(seq 1 60); do
  if curl -fsS "${EMBEDDINGS_URL}" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${EMBEDDINGS_URL}" >/dev/null

set +e
APP_OUTPUT="$(${COMPOSE_CMD} run --rm --no-deps ${APP_SERVICE} ./arhida-cpp --mode recent 2>&1)"
APP_EXIT=$?
set -e

if [[ ${APP_EXIT} -eq 0 ]]; then
  echo "[failure-smoke] expected app to fail when qdrant is unavailable"
  echo "${APP_OUTPUT}"
  exit 1
fi

if [[ "${APP_OUTPUT}" != *"Qdrant request failed"* ]]; then
  echo "[failure-smoke] app failed, but expected qdrant connectivity error was not observed"
  echo "${APP_OUTPUT}"
  exit 1
fi

echo "[failure-smoke] verified fail-fast behavior when qdrant is unavailable"
