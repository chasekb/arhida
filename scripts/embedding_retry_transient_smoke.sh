#!/usr/bin/env bash
set -euo pipefail

# Failure-mode smoke harness: verify app retries embedding requests when the
# embeddings service returns a transient failure.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
APP_SERVICE="${APP_SERVICE:-app}"
QDRANT_URL="${QDRANT_URL:-http://localhost:6333/healthz}"
MOCK_NAME="${MOCK_NAME:-arhida-embeddings-retry-mock}"
MOCK_HOST_PORT="${MOCK_HOST_PORT:-18000}"
MOCK_SERVICE_URL="${MOCK_SERVICE_URL:-http://${MOCK_NAME}:8000}"
COMPOSE_NETWORK="${COMPOSE_NETWORK:-arhida}"
VECTOR_SIZE="${VECTOR_SIZE:-384}"

WORKDIR="$(mktemp -d)"

cleanup() {
  docker rm -f "${MOCK_NAME}" >/dev/null 2>&1 || true
  rm -rf "${WORKDIR}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

cat >"${WORKDIR}/mock_embeddings_retry_server.py" <<'PY'
#!/usr/bin/env python3
import json
import os
from http.server import BaseHTTPRequestHandler, HTTPServer

VECTOR_SIZE = int(os.environ.get("VECTOR_SIZE", "384"))
STATE = {
    "embed_calls": 0,
    "transient_failures": 0,
}


class Handler(BaseHTTPRequestHandler):
    def _send(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        return

    def do_GET(self):
        if self.path == "/health":
            self._send(
                200,
                {
                    "ok": True,
                    "model": "retry-mock",
                    "dimension": VECTOR_SIZE,
                    "backend": "retry-mock",
                    "warmup_complete": True,
                },
            )
            return

        if self.path == "/stats":
            self._send(200, STATE)
            return

        self._send(404, {"error": "not_found"})

    def do_POST(self):
        if self.path != "/embed":
            self._send(404, {"error": "not_found"})
            return

        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length > 0 else b"{}"

        try:
            payload = json.loads(raw.decode("utf-8"))
            inputs = payload.get("inputs", [])
        except Exception:
            self._send(400, {"error": {"code": "invalid_json"}})
            return

        STATE["embed_calls"] += 1

        # First embed call intentionally fails to verify transient retry path.
        if STATE["embed_calls"] == 1:
            STATE["transient_failures"] += 1
            self._send(500, {"error": {"code": "temporary", "message": "transient"}})
            return

        vectors = [[0.0] * VECTOR_SIZE for _ in inputs]
        if vectors:
            vectors[0][0] = 1.0
        self._send(
            200,
            {
                "model": "retry-mock",
                "dimension": VECTOR_SIZE,
                "vectors": vectors,
            },
        )


if __name__ == "__main__":
    server = HTTPServer(("0.0.0.0", 8000), Handler)
    server.serve_forever()
PY

echo "[failure-smoke] starting qdrant dependency"
${COMPOSE_CMD} up -d qdrant

echo "[failure-smoke] ensuring compose embeddings service is not running"
${COMPOSE_CMD} stop embeddings >/dev/null 2>&1 || true

echo "[failure-smoke] starting transient-failure embeddings mock (${MOCK_NAME})"
docker rm -f "${MOCK_NAME}" >/dev/null 2>&1 || true
docker run -d --name "${MOCK_NAME}" \
  --network "${COMPOSE_NETWORK}" \
  -p "${MOCK_HOST_PORT}:8000" \
  -e VECTOR_SIZE="${VECTOR_SIZE}" \
  -v "${WORKDIR}/mock_embeddings_retry_server.py:/mock_embeddings_retry_server.py:ro" \
  python:3.12-alpine \
  python /mock_embeddings_retry_server.py >/dev/null

echo "[failure-smoke] waiting for qdrant health (${QDRANT_URL})"
for _ in $(seq 1 60); do
  if curl -fsS "${QDRANT_URL}" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}" >/dev/null

echo "[failure-smoke] waiting for mock embeddings health"
for _ in $(seq 1 30); do
  if curl -fsS "http://localhost:${MOCK_HOST_PORT}/health" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "http://localhost:${MOCK_HOST_PORT}/health" >/dev/null

set +e
APP_OUTPUT="$(${COMPOSE_CMD} run --rm --no-deps \
  -e EMBEDDING_SERVICE_URL=${MOCK_SERVICE_URL} \
  ${APP_SERVICE} ./arhida-cpp --mode recent 2>&1)"
APP_EXIT=$?
set -e

if [[ ${APP_EXIT} -ne 0 ]]; then
  echo "[failure-smoke] expected app to recover from transient embeddings error"
  echo "${APP_OUTPUT}"
  exit 1
fi

STATS_JSON="$(curl -fsS "http://localhost:${MOCK_HOST_PORT}/stats")"

EMBED_CALLS="$(python3 - <<'PY' <<<"${STATS_JSON}"
import json
import sys
payload = json.loads(sys.stdin.read())
print(int(payload.get("embed_calls", 0)))
PY
)"

TRANSIENT_FAILURES="$(python3 - <<'PY' <<<"${STATS_JSON}"
import json
import sys
payload = json.loads(sys.stdin.read())
print(int(payload.get("transient_failures", 0)))
PY
)"

if [[ "${EMBED_CALLS}" -lt 2 ]]; then
  echo "[failure-smoke] expected at least 2 embed calls (retry not observed)"
  echo "stats=${STATS_JSON}"
  exit 1
fi

if [[ "${TRANSIENT_FAILURES}" -ne 1 ]]; then
  echo "[failure-smoke] expected exactly 1 transient failure"
  echo "stats=${STATS_JSON}"
  exit 1
fi

echo "[failure-smoke] verified retry behavior under transient embeddings failures"
