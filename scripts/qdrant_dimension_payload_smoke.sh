#!/usr/bin/env bash
set -euo pipefail

# Data-validation smoke harness: verify collection vector dimension alignment
# and payload field serialization completeness for a representative record.

QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
COLLECTION="${QDRANT_COLLECTION:-arhida_dimension_payload_smoke}"
VECTOR_SIZE="${VECTOR_SIZE:-384}"

echo "[data-smoke] waiting for Qdrant health endpoint at ${QDRANT_URL}/healthz"
for _ in $(seq 1 30); do
  if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}/healthz" >/dev/null

cleanup() {
  curl -fsS -X DELETE "${QDRANT_URL}/collections/${COLLECTION}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[data-smoke] creating collection ${COLLECTION} (size=${VECTOR_SIZE})"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}" \
  -H "Content-Type: application/json" \
  -d "{\"vectors\": {\"size\": ${VECTOR_SIZE}, \"distance\": \"Cosine\"}}" \
  >/dev/null

COLLECTION_INFO="$(curl -fsS "${QDRANT_URL}/collections/${COLLECTION}")"

python3 - <<'PY' <<<"${COLLECTION_INFO}"
import json
import os
import sys

payload = json.loads(sys.stdin.read())
configured = payload["result"]["config"]["params"]["vectors"]["size"]
expected = int(os.environ.get("VECTOR_SIZE", "384"))

if configured != expected:
    raise SystemExit(
        f"dimension verification failed: expected {expected}, got {configured}"
    )

print(f"[data-smoke] collection dimension verified ({configured})")
PY

python3 - <<'PY' > /tmp/arhida-qdrant-dimension-payload-point.json
import json
import os

vector_size = int(os.environ.get("VECTOR_SIZE", "384"))

point = {
    "points": [
        {
            "id": 101,
            "vector": [0.03] * vector_size,
            "payload": {
                "header_identifier": "oai:arXiv.org:smoke-payload-1",
                "header_datestamp": "2026-01-02T00:00:00",
                "header_setSpecs": ["cs", "stat"],
                "metadata_creator": ["Doe, Jane"],
                "metadata_date": ["2026-01-02"],
                "metadata_description": "Payload smoke description",
                "metadata_identifier": ["http://arxiv.org/abs/2601.00001"],
                "metadata_subject": ["Computer Science"],
                "metadata_title": ["Payload Smoke Title"],
                "metadata_type": "text",
            },
        }
    ]
}

print(json.dumps(point))
PY

echo "[data-smoke] upserting payload verification point"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}/points" \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/arhida-qdrant-dimension-payload-point.json >/dev/null

SCROLL_RESPONSE="$(curl -fsS -X POST "${QDRANT_URL}/collections/${COLLECTION}/points/scroll" \
  -H "Content-Type: application/json" \
  -d '{"limit": 1, "with_payload": true, "with_vector": false}')"

python3 - <<'PY' <<<"${SCROLL_RESPONSE}"
import json
import sys

payload = json.loads(sys.stdin.read())
points = payload.get("result", {}).get("points", [])
if not points:
    raise SystemExit("payload verification failed: no points returned")

doc = points[0].get("payload", {})

required = {
    "header_identifier": str,
    "header_datestamp": str,
    "header_setSpecs": list,
    "metadata_creator": list,
    "metadata_date": list,
    "metadata_description": str,
    "metadata_identifier": list,
    "metadata_subject": list,
    "metadata_title": list,
    "metadata_type": str,
}

for key, expected_type in required.items():
    if key not in doc:
        raise SystemExit(f"payload verification failed: missing key {key}")
    if not isinstance(doc[key], expected_type):
        raise SystemExit(
            f"payload verification failed: key {key} expected {expected_type.__name__}, got {type(doc[key]).__name__}"
        )

print("[data-smoke] payload field completeness/type verification passed")
PY

echo "[data-smoke] Qdrant dimension + payload serialization smoke checks passed"
