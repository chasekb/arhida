#!/usr/bin/env bash
set -euo pipefail

QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
COLLECTION="${QDRANT_COLLECTION:-arhida_smoke_collection}"
VECTOR_SIZE="${VECTOR_SIZE:-384}"

echo "[smoke] waiting for Qdrant health endpoint at ${QDRANT_URL}/healthz"
for _ in $(seq 1 30); do
  if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
    break
  fi
  sleep 1
done

curl -fsS "${QDRANT_URL}/healthz" >/dev/null
echo "[smoke] Qdrant is healthy"

echo "[smoke] creating collection: ${COLLECTION} (size=${VECTOR_SIZE})"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}" \
  -H "Content-Type: application/json" \
  -d "{\"vectors\": {\"size\": ${VECTOR_SIZE}, \"distance\": \"Cosine\"}}" \
  >/dev/null

python3 - <<'PY' > /tmp/arhida-qdrant-smoke-point.json
import json
import os

vector_size = int(os.environ.get("VECTOR_SIZE", "384"))
point = {
    "points": [
        {
            "id": 1,
            "vector": [0.01] * vector_size,
            "payload": {
                "header_identifier": "smoke:test-id",
                "header_datestamp": "2026-01-01T00:00:00",
                "header_setSpecs": ["cs"],
                "metadata_title": ["Smoke Test Record"],
                "metadata_subject": ["Software Engineering"],
                "metadata_description": "Qdrant smoke test payload"
            }
        }
    ]
}
print(json.dumps(point))
PY

echo "[smoke] upserting smoke point into ${COLLECTION}"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}/points" \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/arhida-qdrant-smoke-point.json >/dev/null

echo "[smoke] verifying upsert via scroll query"
SCROLL_RESPONSE="$(curl -fsS -X POST "${QDRANT_URL}/collections/${COLLECTION}/points/scroll" \
  -H "Content-Type: application/json" \
  -d '{"limit": 1, "with_payload": true, "with_vector": false}')"

python3 - <<'PY' <<<"${SCROLL_RESPONSE}"
import json
import sys

payload = json.loads(sys.stdin.read())
points = payload.get("result", {}).get("points", [])
if not points:
    raise SystemExit("smoke verification failed: no points returned")
identifier = points[0].get("payload", {}).get("header_identifier")
if identifier != "smoke:test-id":
    raise SystemExit(f"smoke verification failed: unexpected identifier {identifier!r}")
print("[smoke] smoke point verified")
PY

echo "[smoke] cleaning up collection ${COLLECTION}"
curl -fsS -X DELETE "${QDRANT_URL}/collections/${COLLECTION}" >/dev/null

echo "[smoke] Qdrant collection create/upsert smoke checks passed"
