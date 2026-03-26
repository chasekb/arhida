#!/usr/bin/env bash
set -euo pipefail

# Functional-validation smoke harness: verify Qdrant starts and persists
# point data across a service restart when using compose volume storage.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
QDRANT_SERVICE="${QDRANT_SERVICE:-qdrant}"
QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
COLLECTION="${QDRANT_COLLECTION:-arhida_persistence_smoke}"
VECTOR_SIZE="${VECTOR_SIZE:-384}"
POINT_ID="${POINT_ID:-901}"

wait_for_health() {
  for _ in $(seq 1 60); do
    if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
      return 0
    fi
    sleep 1
  done
  curl -fsS "${QDRANT_URL}/healthz" >/dev/null
}

cleanup() {
  curl -fsS -X DELETE "${QDRANT_URL}/collections/${COLLECTION}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[persistence-smoke] starting ${QDRANT_SERVICE}"
${COMPOSE_CMD} up -d "${QDRANT_SERVICE}"
wait_for_health

echo "[persistence-smoke] creating collection ${COLLECTION}"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}" \
  -H "Content-Type: application/json" \
  -d "{\"vectors\": {\"size\": ${VECTOR_SIZE}, \"distance\": \"Cosine\"}}" \
  >/dev/null

python3 - <<'PY' > /tmp/arhida-qdrant-persistence-point.json
import json
import os

vector_size = int(os.environ.get("VECTOR_SIZE", "384"))
point_id = int(os.environ.get("POINT_ID", "901"))

doc = {
    "points": [
        {
            "id": point_id,
            "vector": [0.05] * vector_size,
            "payload": {
                "header_identifier": "oai:arXiv.org:persistence-smoke-1",
                "header_datestamp": "2026-01-06T00:00:00",
                "header_setSpecs": ["cs"],
                "metadata_title": ["Persistence Smoke"],
            },
        }
    ]
}

print(json.dumps(doc))
PY

echo "[persistence-smoke] upserting persistence probe point"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}/points" \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/arhida-qdrant-persistence-point.json >/dev/null

echo "[persistence-smoke] restarting ${QDRANT_SERVICE}"
${COMPOSE_CMD} restart "${QDRANT_SERVICE}"
wait_for_health

VERIFY_RESPONSE="$(curl -fsS -X POST "${QDRANT_URL}/collections/${COLLECTION}/points/scroll" \
  -H "Content-Type: application/json" \
  -d "{\"filter\":{\"must\":[{\"key\":\"header_identifier\",\"match\":{\"value\":\"oai:arXiv.org:persistence-smoke-1\"}}]},\"with_payload\":true,\"with_vector\":false,\"limit\":4}")"

python3 - <<'PY' <<<"${VERIFY_RESPONSE}"
import json
import sys

payload = json.loads(sys.stdin.read())
points = payload.get("result", {}).get("points", [])
if len(points) != 1:
    raise SystemExit(
        f"persistence verification failed: expected 1 point after restart, got {len(points)}"
    )

identifier = points[0].get("payload", {}).get("header_identifier")
if identifier != "oai:arXiv.org:persistence-smoke-1":
    raise SystemExit(
        f"persistence verification failed: unexpected identifier {identifier!r}"
    )

print("[persistence-smoke] point persisted across qdrant restart")
PY

echo "[persistence-smoke] Qdrant startup + persistence smoke checks passed"
