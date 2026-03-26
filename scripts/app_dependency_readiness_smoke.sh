#!/usr/bin/env bash
set -euo pipefail

# Functional-validation smoke harness: verify app startup succeeds only when
# required dependencies (qdrant + embeddings) are ready.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
QDRANT_SERVICE="${QDRANT_SERVICE:-qdrant}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
QDRANT_HEALTH_URL="${QDRANT_HEALTH_URL:-http://localhost:6333/healthz}"
EMBEDDINGS_HEALTH_URL="${EMBEDDINGS_HEALTH_URL:-http://localhost:8000/health}"

echo "[app-readiness-smoke] forcing dependency-down baseline"
${COMPOSE_CMD} stop "${QDRANT_SERVICE}" "${EMBEDDINGS_SERVICE}" >/dev/null 2>&1 || true

echo "[app-readiness-smoke] asserting app fails when started without dependencies"
set +e
FAIL_OUTPUT="$(${COMPOSE_CMD} run --rm --no-deps ${APP_SERVICE} ./arhida-cpp --mode recent 2>&1)"
FAIL_EXIT=$?
set -e

if [ "${FAIL_EXIT}" -eq 0 ]; then
  echo "[app-readiness-smoke] expected failure when dependencies are unavailable" >&2
  exit 1
fi

if [[ "${FAIL_OUTPUT}" != *"Embeddings service health check failed during startup"* ]] && \
   [[ "${FAIL_OUTPUT}" != *"Qdrant request failed"* ]]; then
  echo "[app-readiness-smoke] startup-failure output missing dependency readiness signal" >&2
  echo "${FAIL_OUTPUT}" >&2
  exit 1
fi

echo "[app-readiness-smoke] bringing dependencies up"
${COMPOSE_CMD} up -d "${QDRANT_SERVICE}" "${EMBEDDINGS_SERVICE}"

echo "[app-readiness-smoke] waiting for qdrant health"
for _ in $(seq 1 90); do
  if curl -fsS "${QDRANT_HEALTH_URL}" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_HEALTH_URL}" >/dev/null

echo "[app-readiness-smoke] waiting for embeddings health"
for _ in $(seq 1 90); do
  if curl -fsS "${EMBEDDINGS_HEALTH_URL}" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${EMBEDDINGS_HEALTH_URL}" >/dev/null

echo "[app-readiness-smoke] asserting app starts once dependencies are ready"
${COMPOSE_CMD} run --rm ${APP_SERVICE} ./arhida-cpp --help >/dev/null

echo "[app-readiness-smoke] app dependency readiness smoke checks passed"
