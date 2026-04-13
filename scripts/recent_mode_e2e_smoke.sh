#!/usr/bin/env bash
set -euo pipefail

# Functional-validation smoke harness: verify recent mode runs end to end and
# persists at least one point into an isolated Qdrant collection.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
QDRANT_SERVICE="${QDRANT_SERVICE:-qdrant}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
QDRANT_COLLECTION="${QDRANT_COLLECTION:-arhida_recent_mode_e2e_smoke}"
SET_SPECS="${SET_SPECS:-cs}"

cleanup() {
  curl -fsS -X DELETE "${QDRANT_URL}/collections/${QDRANT_COLLECTION}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[recent-e2e-smoke] starting dependencies"
${COMPOSE_CMD} up -d "${QDRANT_SERVICE}" "${EMBEDDINGS_SERVICE}"

echo "[recent-e2e-smoke] waiting for qdrant health"
for _ in $(seq 1 90); do
  if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}/healthz" >/dev/null

echo "[recent-e2e-smoke] waiting for embeddings health"
for _ in $(seq 1 90); do
  if curl -fsS "http://localhost:8000/health" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "http://localhost:8000/health" >/dev/null

echo "[recent-e2e-smoke] resetting target collection ${QDRANT_COLLECTION}"
curl -fsS -X DELETE "${QDRANT_URL}/collections/${QDRANT_COLLECTION}" >/dev/null 2>&1 || true

echo "[recent-e2e-smoke] running app recent mode"
${COMPOSE_CMD} run --rm \
  -e QDRANT_COLLECTION="${QDRANT_COLLECTION}" \
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
    raise SystemExit(f"recent e2e verification failed: invalid count payload {payload!r}")

if count < 1:
    raise SystemExit(
        f"recent e2e verification failed: expected at least 1 stored point, got {count}"
    )

print(f"[recent-e2e-smoke] verified recent mode stored points (count={count})")
PY

echo "[recent-e2e-smoke] recent mode e2e smoke checks passed"
