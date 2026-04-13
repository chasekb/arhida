#!/usr/bin/env bash
set -euo pipefail

EMBEDDINGS_URL="${EMBEDDINGS_URL:-http://localhost:8000}"
ITERATIONS="${ITERATIONS:-5}"
BATCH_SIZES_CSV="${BATCH_SIZES:-1,8,32,64}"

echo "[bench] target=${EMBEDDINGS_URL} iterations=${ITERATIONS} batch_sizes=${BATCH_SIZES_CSV}"

for _ in $(seq 1 30); do
  if curl -fsS "${EMBEDDINGS_URL}/health" >/dev/null; then
    break
  fi
  sleep 1
done

if ! curl -fsS "${EMBEDDINGS_URL}/health" >/dev/null; then
  echo "[bench] embeddings service is not healthy"
  exit 1
fi

IFS=',' read -r -a BATCH_SIZES <<<"${BATCH_SIZES_CSV}"

for size in "${BATCH_SIZES[@]}"; do
  if ! [[ "${size}" =~ ^[0-9]+$ ]] || [[ "${size}" -le 0 ]]; then
    echo "[bench] invalid batch size: ${size}"
    exit 1
  fi

  echo "[bench] running batch_size=${size}"
  timings_file="/tmp/arhida-embed-bench-${size}.txt"
  : >"${timings_file}"

  for _ in $(seq 1 "${ITERATIONS}"); do
    body="$(python3 - <<'PY' "${size}"
import json
import sys

n = int(sys.argv[1])
inputs = [f"benchmark sample text {i}" for i in range(n)]
print(json.dumps({"inputs": inputs}))
PY
)"

    response_code="$(curl -sS -o /tmp/arhida-embed-bench-body.json \
      -w "%{http_code} %{time_total}" \
      -X POST "${EMBEDDINGS_URL}/embed" \
      -H "Content-Type: application/json" \
      -d "${body}")"

    code="${response_code%% *}"
    time_total="${response_code##* }"

    if [[ "${code}" != "200" ]]; then
      echo "[bench] request failed batch_size=${size} status=${code}"
      cat /tmp/arhida-embed-bench-body.json
      exit 1
    fi

    echo "${time_total}" >>"${timings_file}"
  done

  python3 - <<'PY' "${timings_file}" "${size}"
import statistics
import sys

path = sys.argv[1]
batch = sys.argv[2]

with open(path, 'r', encoding='utf-8') as fh:
    values = [float(line.strip()) for line in fh if line.strip()]

values_sorted = sorted(values)

def percentile(arr, pct):
    if not arr:
        return 0.0
    k = int(round((pct / 100.0) * (len(arr) - 1)))
    return arr[max(0, min(k, len(arr) - 1))]

print(
    f"[bench] batch_size={batch} count={len(values)} "
    f"min={min(values_sorted):.4f}s "
    f"p50={statistics.median(values_sorted):.4f}s "
    f"p95={percentile(values_sorted, 95):.4f}s "
    f"max={max(values_sorted):.4f}s"
)
PY
done

echo "[bench] completed embeddings benchmark"
