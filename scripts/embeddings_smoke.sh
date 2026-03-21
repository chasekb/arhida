#!/usr/bin/env bash
set -euo pipefail

EMBEDDINGS_URL="${EMBEDDINGS_URL:-http://localhost:8000}"
MAX_BATCH_SIZE="${MAX_BATCH_SIZE:-64}"

echo "[smoke] waiting for embeddings health endpoint at ${EMBEDDINGS_URL}/health"
for _ in $(seq 1 30); do
  if curl -fsS "${EMBEDDINGS_URL}/health" >/dev/null; then
    break
  fi
  sleep 1
done

HEALTH_RESPONSE="$(curl -fsS "${EMBEDDINGS_URL}/health")"

MODEL_NAME="$(python3 - <<'PY' <<<"${HEALTH_RESPONSE}"
import json
import sys

payload = json.loads(sys.stdin.read())
if not payload.get("ok"):
    raise SystemExit("health check failed: ok=false")
if not payload.get("warmup_complete", False):
    raise SystemExit("health check failed: warmup_complete=false")
print(payload.get("model", ""))
PY
)"

if [[ -z "${MODEL_NAME}" ]]; then
  echo "[smoke] health response missing model name"
  exit 1
fi

echo "[smoke] embeddings service healthy (model=${MODEL_NAME})"

EMBED_RESPONSE="$(curl -fsS -X POST "${EMBEDDINGS_URL}/embed" \
  -H "Content-Type: application/json" \
  -d '{"inputs": ["first smoke text", "second smoke text"]}')"

python3 - <<'PY' <<<"${EMBED_RESPONSE}"
import json
import sys

payload = json.loads(sys.stdin.read())
vectors = payload.get("vectors", [])
dimension = payload.get("dimension")

if len(vectors) != 2:
    raise SystemExit(f"embed verification failed: expected 2 vectors, got {len(vectors)}")

for i, vector in enumerate(vectors):
    if len(vector) != dimension:
        raise SystemExit(
            f"embed verification failed: vector {i} dim {len(vector)} != {dimension}"
        )

print("[smoke] embed response vector count/dim verified")
PY

OVERSIZE_BODY="$(python3 - <<'PY'
import json
import os

max_batch = int(os.environ.get("MAX_BATCH_SIZE", "64"))
inputs = [f"item-{i}" for i in range(max_batch + 1)]
print(json.dumps({"inputs": inputs}))
PY
)"

OVERSIZE_RESPONSE="$(curl -sS -o /tmp/arhida-embeddings-oversize.json -w "%{http_code}" \
  -X POST "${EMBEDDINGS_URL}/embed" \
  -H "Content-Type: application/json" \
  -d "${OVERSIZE_BODY}")"

if [[ "${OVERSIZE_RESPONSE}" != "400" ]]; then
  echo "[smoke] expected 400 for oversized batch, got ${OVERSIZE_RESPONSE}"
  cat /tmp/arhida-embeddings-oversize.json
  exit 1
fi

python3 - <<'PY'
import json

with open('/tmp/arhida-embeddings-oversize.json', 'r', encoding='utf-8') as fh:
    payload = json.load(fh)

code = payload.get("error", {}).get("code")
if code != "batch_too_large":
    raise SystemExit(f"oversize verification failed: expected batch_too_large, got {code!r}")

print("[smoke] oversize batch error verified")
PY

echo "[smoke] embeddings /health and /embed smoke checks passed"
