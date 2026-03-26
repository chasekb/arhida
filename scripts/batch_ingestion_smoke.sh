#!/usr/bin/env bash
set -euo pipefail

# Functional-validation smoke harness: verify app batch ingestion persists
# multiple records into Qdrant during a constrained recent-mode run.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
QDRANT_SERVICE="${QDRANT_SERVICE:-qdrant}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
QDRANT_COLLECTION="${QDRANT_COLLECTION:-arhida_batch_ingestion_smoke}"
SET_SPECS="${SET_SPECS:-cs}"
BATCH_SIZE="${BATCH_SIZE:-5}"

cleanup() {
  curl -fsS -X DELETE "${QDRANT_URL}/collections/${QDRANT_COLLECTION}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[batch-smoke] starting dependencies"
${COMPOSE_CMD} up -d "${QDRANT_SERVICE}" "${EMBEDDINGS_SERVICE}"

echo "[batch-smoke] waiting for qdrant health"
for _ in $(seq 1 90); do
  if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}/healthz" >/dev/null

echo "[batch-smoke] waiting for embeddings health"
for _ in $(seq 1 90); do
  if curl -fsS "http://localhost:8000/health" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "http://localhost:8000/health" >/dev/null

echo "[batch-smoke] resetting target collection ${QDRANT_COLLECTION}"
curl -fsS -X DELETE "${QDRANT_URL}/collections/${QDRANT_COLLECTION}" >/dev/null 2>&1 || true

echo "[batch-smoke] running app recent mode with ARXIV_BATCH_SIZE=${BATCH_SIZE}"
${COMPOSE_CMD} run --rm \
  -e QDRANT_COLLECTION="${QDRANT_COLLECTION}" \
  -e ARXIV_BATCH_SIZE="${BATCH_SIZE}" \
  -e ARXIV_MAX_RETRIES=1 \
  -e ARXIV_RETRY_AFTER=1 \
  -e ARXIV_RATE_LIMIT_DELAY=1 \
  ${APP_SERVICE} ./arhida-cpp --mode recent --set-specs "${SET_SPECS}"

COUNT_RESPONSE="$(curl -fsS -X POST "${QDRANT_URL}/collections/${QDRANT_COLLECTION}/points/count" \
  -H "Content-Type: application/json" \
  -d '{"exact":true}')"

python3 - <<'PY' <<<"${COUNT_RESPONSE}"
import json
import sys

payload = json.loads(sys.stdin.read())
count = payload.get("result", {}).get("count")
if not isinstance(count, int):
    raise SystemExit(f"batch verification failed: invalid count payload {payload!r}")

if count < 2:
    raise SystemExit(
        f"batch verification failed: expected at least 2 stored points, got {count}"
    )

print(f"[batch-smoke] verified batch ingestion stored multiple points (count={count})")
PY

echo "[batch-smoke] batch ingestion smoke checks passed"
