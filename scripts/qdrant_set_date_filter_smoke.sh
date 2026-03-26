#!/usr/bin/env bash
set -euo pipefail

# Data-validation smoke harness: verify Qdrant payload filtering by
# header_setSpecs + header_datestamp supports backfill missing-date logic.

QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
COLLECTION="${QDRANT_COLLECTION:-arhida_set_date_filter_smoke}"
VECTOR_SIZE="${VECTOR_SIZE:-384}"
START_DATE="${START_DATE:-2026-01-01}"
END_DATE="${END_DATE:-2026-01-05}"
TARGET_SET_SPEC="${TARGET_SET_SPEC:-cs}"

echo "[filter-smoke] waiting for Qdrant health endpoint at ${QDRANT_URL}/healthz"
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

echo "[filter-smoke] creating collection ${COLLECTION} (size=${VECTOR_SIZE})"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}" \
  -H "Content-Type: application/json" \
  -d "{\"vectors\": {\"size\": ${VECTOR_SIZE}, \"distance\": \"Cosine\"}}" \
  >/dev/null

python3 - <<'PY' > /tmp/arhida-qdrant-set-date-filter-points.json
import json
import os

vector_size = int(os.environ.get("VECTOR_SIZE", "384"))

payloads = [
    {
        "id": 201,
        "header_identifier": "oai:arXiv.org:filter-smoke-1",
        "header_datestamp": "2026-01-01T00:00:00",
        "header_setSpecs": ["cs"],
    },
    {
        "id": 202,
        "header_identifier": "oai:arXiv.org:filter-smoke-2",
        "header_datestamp": "2026-01-03T00:00:00",
        "header_setSpecs": ["cs", "math"],
    },
    {
        "id": 203,
        "header_identifier": "oai:arXiv.org:filter-smoke-3",
        "header_datestamp": "2026-01-02T00:00:00",
        "header_setSpecs": ["physics"],
    },
    {
        "id": 204,
        "header_identifier": "oai:arXiv.org:filter-smoke-4",
        "header_datestamp": "2025-12-31T00:00:00",
        "header_setSpecs": ["cs"],
    },
]

points = []
for item in payloads:
    points.append(
        {
            "id": item["id"],
            "vector": [0.04] * vector_size,
            "payload": {
                "header_identifier": item["header_identifier"],
                "header_datestamp": item["header_datestamp"],
                "header_setSpecs": item["header_setSpecs"],
                "metadata_title": ["Filter Smoke"],
                "metadata_subject": ["Systems"],
                "metadata_description": "Filter smoke payload",
            },
        }
    )

print(json.dumps({"points": points}))
PY

echo "[filter-smoke] upserting representative points"
curl -fsS -X PUT "${QDRANT_URL}/collections/${COLLECTION}/points" \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/arhida-qdrant-set-date-filter-points.json >/dev/null

FILTER_RESPONSE="$(curl -fsS -X POST "${QDRANT_URL}/collections/${COLLECTION}/points/scroll" \
  -H "Content-Type: application/json" \
  -d "{\"filter\":{\"must\":[{\"key\":\"header_setSpecs\",\"match\":{\"any\":[\"${TARGET_SET_SPEC}\"]}},{\"key\":\"header_datestamp\",\"range\":{\"gte\":\"${START_DATE}T00:00:00\",\"lte\":\"${END_DATE}T23:59:59\"}}]},\"with_payload\":[\"header_identifier\",\"header_datestamp\",\"header_setSpecs\"],\"with_vector\":false,\"limit\":128}")"

python3 - <<'PY' <<<"${FILTER_RESPONSE}"
import datetime
import json
import os
import sys

payload = json.loads(sys.stdin.read())
points = payload.get("result", {}).get("points", [])

if len(points) != 2:
    raise SystemExit(
        f"filter verification failed: expected 2 matching points, got {len(points)}"
    )

dates = sorted({p["payload"]["header_datestamp"][:10] for p in points})
expected_dates = ["2026-01-01", "2026-01-03"]
if dates != expected_dates:
    raise SystemExit(
        f"filter verification failed: expected matched dates {expected_dates}, got {dates}"
    )

start_date = datetime.date.fromisoformat(os.environ.get("START_DATE", "2026-01-01"))
end_date = datetime.date.fromisoformat(os.environ.get("END_DATE", "2026-01-05"))
if end_date < start_date:
    start_date, end_date = end_date, start_date

existing = set(dates)
missing = []
cursor = start_date
while cursor <= end_date:
    key = cursor.isoformat()
    if key not in existing:
        missing.append(key)
    cursor += datetime.timedelta(days=1)

expected_missing = ["2026-01-02", "2026-01-04", "2026-01-05"]
if missing != expected_missing:
    raise SystemExit(
        f"missing-date verification failed: expected {expected_missing}, got {missing}"
    )

print(f"[filter-smoke] matched dates for set/date filter: {dates}")
print(f"[filter-smoke] missing dates for backfill semantics: {missing}")
PY

echo "[filter-smoke] Qdrant set/date filtering smoke checks passed"
