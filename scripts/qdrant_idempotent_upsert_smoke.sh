#!/usr/bin/env bash
set -euo pipefail

# Data-validation smoke harness: verify deterministic point-id generation and
# idempotent upsert/update behavior used by Qdrant storage.

QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
COLLECTION="${QDRANT_COLLECTION:-arhida_idempotent_upsert_smoke}"
VECTOR_SIZE="${VECTOR_SIZE:-384}"

echo "[data-smoke] waiting for Qdrant health endpoint at ${QDRANT_URL}/healthz"
for _ in $(seq 1 30); do
  if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}/healthz" >/dev/null

echo "[data-smoke] creating collection ${COLLECTION}"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}" \
  -H "Content-Type: application/json" \
  -d "{\"vectors\": {\"size\": ${VECTOR_SIZE}, \"distance\": \"Cosine\"}}" \
  >/dev/null

cleanup() {
  curl -fsS -X DELETE "${QDRANT_URL}/collections/${COLLECTION}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

python3 - <<'PY' > /tmp/arhida-qdrant-idempotent-smoke.json
import json
import os

vector_size = int(os.environ.get("VECTOR_SIZE", "384"))


def fnv64(identifier: str) -> int:
    fnv_offset = 1469598103934665603
    fnv_prime = 1099511628211
    value = fnv_offset
    for ch in identifier.encode("utf-8"):
        value ^= ch
        value = (value * fnv_prime) & 0xFFFFFFFFFFFFFFFF
    return value


identifier = "oai:arXiv.org:smoke-idempotent-1"
same_id = fnv64(identifier)
same_id_again = fnv64(identifier)
different_id = fnv64("oai:arXiv.org:smoke-idempotent-2")

if same_id != same_id_again:
    raise SystemExit("deterministic-id verification failed: same identifier produced different ids")

if same_id == different_id:
    raise SystemExit("deterministic-id verification failed: distinct identifiers collided in smoke input")

point_initial = {
    "id": same_id,
    "vector": [0.01] * vector_size,
    "payload": {
        "header_identifier": identifier,
        "header_datestamp": "2026-01-01T00:00:00",
        "header_setSpecs": ["cs"],
        "metadata_description": "initial-description",
    },
}

point_updated = {
    "id": same_id,
    "vector": [0.02] * vector_size,
    "payload": {
        "header_identifier": identifier,
        "header_datestamp": "2026-01-01T00:00:00",
        "header_setSpecs": ["cs"],
        "metadata_description": "updated-description",
    },
}

print(
    json.dumps(
        {
            "point_id": same_id,
            "initial": {"points": [point_initial]},
            "updated": {"points": [point_updated]},
        }
    )
)
PY

POINT_ID="$(python3 - <<'PY'
import json
with open('/tmp/arhida-qdrant-idempotent-smoke.json', 'r', encoding='utf-8') as fh:
    payload = json.load(fh)
print(payload['point_id'])
PY
)"

python3 - <<'PY'
import json

with open('/tmp/arhida-qdrant-idempotent-smoke.json', 'r', encoding='utf-8') as fh:
    payload = json.load(fh)

with open('/tmp/arhida-qdrant-idempotent-initial.json', 'w', encoding='utf-8') as fh:
    json.dump(payload['initial'], fh)

with open('/tmp/arhida-qdrant-idempotent-updated.json', 'w', encoding='utf-8') as fh:
    json.dump(payload['updated'], fh)
PY

echo "[data-smoke] upserting initial point id=${POINT_ID}"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}/points" \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/arhida-qdrant-idempotent-initial.json >/dev/null

echo "[data-smoke] upserting updated point id=${POINT_ID}"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}/points" \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/arhida-qdrant-idempotent-updated.json >/dev/null

SCROLL_RESPONSE="$(curl -fsS -X POST "${QDRANT_URL}/collections/${COLLECTION}/points/scroll" \
  -H "Content-Type: application/json" \
  -d '{"limit": 100, "with_payload": true, "with_vector": false}')"

python3 - <<'PY' <<<"${SCROLL_RESPONSE}"
import json
import sys

payload = json.loads(sys.stdin.read())
points = payload.get("result", {}).get("points", [])

if len(points) != 1:
    raise SystemExit(f"idempotent-upsert verification failed: expected 1 point, got {len(points)}")

point = points[0]
description = point.get("payload", {}).get("metadata_description")
if description != "updated-description":
    raise SystemExit(
        f"idempotent-upsert verification failed: expected updated payload, got {description!r}"
    )

print("[data-smoke] deterministic id and duplicate-update behavior verified")
PY

echo "[data-smoke] Qdrant deterministic-id/idempotent-upsert smoke checks passed"
