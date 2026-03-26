#!/usr/bin/env bash
set -euo pipefail

# Functional-validation smoke harness: verify a single app run can harvest,
# embed, and persist at least one record into Qdrant.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
QDRANT_SERVICE="${QDRANT_SERVICE:-qdrant}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
QDRANT_COLLECTION="${QDRANT_COLLECTION:-arxiv_metadata}"
SET_SPECS="${SET_SPECS:-cs}"

echo "[one-record-smoke] starting dependencies"
${COMPOSE_CMD} up -d "${QDRANT_SERVICE}" "${EMBEDDINGS_SERVICE}"

echo "[one-record-smoke] waiting for qdrant health"
for _ in $(seq 1 90); do
  if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}/healthz" >/dev/null

echo "[one-record-smoke] waiting for embeddings health"
for _ in $(seq 1 90); do
  if curl -fsS "http://localhost:8000/health" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "http://localhost:8000/health" >/dev/null

echo "[one-record-smoke] running app recent mode with constrained harvest size"
${COMPOSE_CMD} run --rm \
  -e ARXIV_BATCH_SIZE=1 \
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
    raise SystemExit(f"one-record verification failed: invalid count payload {payload!r}")

if count < 1:
    raise SystemExit("one-record verification failed: no points found in qdrant collection")

print(f"[one-record-smoke] verified persisted point count >= 1 (count={count})")
PY

echo "[one-record-smoke] one-record harvest/embed/store smoke checks passed"
