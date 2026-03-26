#!/usr/bin/env bash
set -euo pipefail

# Functional-validation smoke harness: verify embeddings service starts and
# reports healthy readiness metadata.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
EMBEDDINGS_URL="${EMBEDDINGS_URL:-http://localhost:8000/health}"

echo "[embeddings-startup-smoke] starting ${EMBEDDINGS_SERVICE}"
${COMPOSE_CMD} up -d "${EMBEDDINGS_SERVICE}"

echo "[embeddings-startup-smoke] waiting for health endpoint ${EMBEDDINGS_URL}"
for _ in $(seq 1 90); do
  if curl -fsS "${EMBEDDINGS_URL}" >/dev/null; then
    break
  fi
  sleep 1
done

HEALTH_PAYLOAD="$(curl -fsS "${EMBEDDINGS_URL}")"

python3 - <<'PY' <<<"${HEALTH_PAYLOAD}"
import json
import sys

payload = json.loads(sys.stdin.read())

if payload.get("ok") is not True:
    raise SystemExit("health verification failed: ok != true")

if payload.get("warmup_complete") is not True:
    raise SystemExit("health verification failed: warmup_complete != true")

dimension = payload.get("dimension")
if not isinstance(dimension, int) or dimension <= 0:
    raise SystemExit(
        f"health verification failed: expected positive integer dimension, got {dimension!r}"
    )

backend = payload.get("backend")
if not isinstance(backend, str) or not backend:
    raise SystemExit(
        f"health verification failed: expected non-empty backend string, got {backend!r}"
    )

print(
    f"[embeddings-startup-smoke] health verified: backend={backend}, dimension={dimension}"
)
PY

echo "[embeddings-startup-smoke] embeddings startup + health smoke checks passed"
