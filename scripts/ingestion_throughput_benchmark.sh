#!/usr/bin/env bash
set -euo pipefail

# Performance-validation harness: measure end-to-end ingestion throughput and
# provide a repeatable sweep for batch-size tuning.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
QDRANT_SERVICE="${QDRANT_SERVICE:-qdrant}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"
QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
QDRANT_COLLECTION="${QDRANT_COLLECTION:-arhida_ingestion_benchmark}"
SET_SPECS="${SET_SPECS:-cs}"
ITERATIONS="${ITERATIONS:-1}"
APP_BATCH_SIZES="${APP_BATCH_SIZES:-1,5,10}"
EMBED_BATCH_SIZE="${EMBED_BATCH_SIZE:-64}"

cleanup() {
  curl -fsS -X DELETE "${QDRANT_URL}/collections/${QDRANT_COLLECTION}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[ingest-bench] target collection=${QDRANT_COLLECTION} iterations=${ITERATIONS} app_batch_sizes=${APP_BATCH_SIZES} embed_batch_size=${EMBED_BATCH_SIZE}"

echo "[ingest-bench] starting dependencies"
${COMPOSE_CMD} up -d "${QDRANT_SERVICE}" "${EMBEDDINGS_SERVICE}"

echo "[ingest-bench] waiting for qdrant health"
for _ in $(seq 1 90); do
  if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}/healthz" >/dev/null

echo "[ingest-bench] waiting for embeddings health"
for _ in $(seq 1 90); do
  if curl -fsS "http://localhost:8000/health" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "http://localhost:8000/health" >/dev/null

IFS=',' read -r -a BATCH_SIZES <<<"${APP_BATCH_SIZES}"

for batch_size in "${BATCH_SIZES[@]}"; do
  if ! [[ "${batch_size}" =~ ^[0-9]+$ ]] || [[ "${batch_size}" -le 0 ]]; then
    echo "[ingest-bench] invalid batch size: ${batch_size}"
    exit 1
  fi

  for i in $(seq 1 "${ITERATIONS}"); do
    echo "[ingest-bench] run batch_size=${batch_size} iteration=${i}/${ITERATIONS}"

    curl -fsS -X DELETE "${QDRANT_URL}/collections/${QDRANT_COLLECTION}" >/dev/null 2>&1 || true

    start_time="$(python3 - <<'PY'
import time
print(f"{time.time():.6f}")
PY
)"

    ${COMPOSE_CMD} run --rm \
      -e QDRANT_COLLECTION="${QDRANT_COLLECTION}" \
      -e ARXIV_BATCH_SIZE="${batch_size}" \
      -e EMBEDDING_MAX_BATCH_SIZE="${EMBED_BATCH_SIZE}" \
      -e ARXIV_MAX_RETRIES=1 \
      -e ARXIV_RETRY_AFTER=1 \
      -e ARXIV_RATE_LIMIT_DELAY=1 \
      ${APP_SERVICE} ./arhida-cpp --mode recent --set-specs "${SET_SPECS}"

    end_time="$(python3 - <<'PY'
import time
print(f"{time.time():.6f}")
PY
)"

    COUNT_RESPONSE="$(curl -fsS -X POST "${QDRANT_URL}/collections/${QDRANT_COLLECTION}/points/count" \
      -H "Content-Type: application/json" \
      -d '{"exact":true}')"

    python3 - <<'PY' "${COUNT_RESPONSE}" "${start_time}" "${end_time}" "${batch_size}" "${EMBED_BATCH_SIZE}" "${i}"
import json
import sys

payload = json.loads(sys.argv[1])
start = float(sys.argv[2])
end = float(sys.argv[3])
batch_size = int(sys.argv[4])
embed_batch = int(sys.argv[5])
iteration = int(sys.argv[6])

count = payload.get("result", {}).get("count")
if not isinstance(count, int):
    raise SystemExit(f"invalid count payload: {payload!r}")

duration = end - start
if duration <= 0:
    raise SystemExit(f"invalid duration computed: start={start} end={end}")

throughput = count / duration
print(
    f"[ingest-bench] result iteration={iteration} app_batch_size={batch_size} "
    f"embed_batch_size={embed_batch} records={count} duration_s={duration:.3f} "
    f"records_per_sec={throughput:.3f}"
)
PY
  done
done

echo "[ingest-bench] completed end-to-end ingestion benchmark sweep"
