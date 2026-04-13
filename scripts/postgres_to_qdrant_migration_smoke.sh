#!/usr/bin/env bash
set -euo pipefail

# Migration smoke harness: provisions ephemeral PostgreSQL source data,
# runs PostgreSQL->Qdrant migration, and verifies parity/checkpoint outcomes.

CONTAINER_CMD="${CONTAINER_CMD:-podman}"

if ! command -v "${CONTAINER_CMD}" >/dev/null 2>&1; then
  echo "[migration-smoke] required container runtime not found: ${CONTAINER_CMD}"
  exit 1
fi
QDRANT_URL="${QDRANT_URL:-http://localhost:6333}"
QDRANT_COLLECTION="${QDRANT_COLLECTION:-arxiv_metadata_migration_smoke}"

PG_CONTAINER="${PG_CONTAINER:-arhida-migration-smoke-pg}"
PG_PORT="${PG_PORT:-15432}"
PG_DB="${PG_DB:-arhida_migration_smoke}"
PG_USER="${PG_USER:-arhida}"
PG_PASSWORD="${PG_PASSWORD:-arhida}"

MOCK_NAME="${MOCK_NAME:-arhida-migration-smoke-embeddings}"
MOCK_PORT="${MOCK_PORT:-18001}"
VECTOR_SIZE="${VECTOR_SIZE:-384}"

WORKDIR="$(mktemp -d)"
CHECKPOINT_FILE="${CHECKPOINT_FILE:-${WORKDIR}/postgres_to_qdrant_checkpoint.json}"

cleanup() {
  "${CONTAINER_CMD}" rm -f "${PG_CONTAINER}" >/dev/null 2>&1 || true
  "${CONTAINER_CMD}" rm -f "${MOCK_NAME}" >/dev/null 2>&1 || true
  rm -rf "${WORKDIR}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[migration-smoke] starting qdrant"
"${CONTAINER_CMD}" compose up -d qdrant

echo "[migration-smoke] waiting for qdrant health"
for _ in $(seq 1 90); do
  if curl -fsS "${QDRANT_URL}/healthz" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "${QDRANT_URL}/healthz" >/dev/null

echo "[migration-smoke] starting postgres container ${PG_CONTAINER}"
"${CONTAINER_CMD}" rm -f "${PG_CONTAINER}" >/dev/null 2>&1 || true
"${CONTAINER_CMD}" run -d --name "${PG_CONTAINER}" \
  -e POSTGRES_DB="${PG_DB}" \
  -e POSTGRES_USER="${PG_USER}" \
  -e POSTGRES_PASSWORD="${PG_PASSWORD}" \
  -p "${PG_PORT}:5432" \
  postgres:16-alpine >/dev/null

echo "[migration-smoke] waiting for postgres readiness"
for _ in $(seq 1 90); do
  if "${CONTAINER_CMD}" exec "${PG_CONTAINER}" pg_isready -U "${PG_USER}" -d "${PG_DB}" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done
"${CONTAINER_CMD}" exec "${PG_CONTAINER}" pg_isready -U "${PG_USER}" -d "${PG_DB}" >/dev/null

echo "[migration-smoke] seeding postgres source records"
"${CONTAINER_CMD}" exec -i "${PG_CONTAINER}" psql -v ON_ERROR_STOP=1 -U "${PG_USER}" -d "${PG_DB}" <<'SQL'
CREATE SCHEMA IF NOT EXISTS arxiv;

CREATE TABLE IF NOT EXISTS arxiv.metadata (
  id SERIAL PRIMARY KEY,
  header_datestamp TIMESTAMP,
  header_identifier VARCHAR(255) UNIQUE NOT NULL,
  header_setSpecs JSONB,
  metadata_creator JSONB,
  metadata_date JSONB,
  metadata_description TEXT,
  metadata_identifier JSONB,
  metadata_subject JSONB,
  metadata_title JSONB,
  metadata_type VARCHAR(100),
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

TRUNCATE arxiv.metadata;

INSERT INTO arxiv.metadata (
  header_datestamp,
  header_identifier,
  header_setSpecs,
  metadata_creator,
  metadata_date,
  metadata_description,
  metadata_identifier,
  metadata_subject,
  metadata_title,
  metadata_type
) VALUES
('2020-01-01 00:00:00', 'oai:arXiv.org:0001.00001', '["cs.AI"]'::jsonb, '["Author One"]'::jsonb, '["2020-01-01"]'::jsonb, 'Description one', '["https://arxiv.org/abs/0001.00001"]'::jsonb, '["AI"]'::jsonb, '["Title One"]'::jsonb, 'text'),
('2020-01-02 00:00:00', 'oai:arXiv.org:0001.00002', '["cs.LG"]'::jsonb, '["Author Two"]'::jsonb, '["2020-01-02"]'::jsonb, 'Description two', '["https://arxiv.org/abs/0001.00002"]'::jsonb, '["ML"]'::jsonb, '["Title Two"]'::jsonb, 'text');
SQL

cat >"${WORKDIR}/mock_embeddings_server.py" <<'PY'
#!/usr/bin/env python3
import json
import os
from http.server import BaseHTTPRequestHandler, HTTPServer

VECTOR_SIZE = int(os.environ.get("VECTOR_SIZE", "384"))

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
        if self.path != "/health":
            self._send(404, {"error": "not_found"})
            return
        self._send(200, {
            "ok": True,
            "model": "migration-smoke-mock",
            "dimension": VECTOR_SIZE,
            "backend": "mock",
            "warmup_complete": True,
        })

    def do_POST(self):
        if self.path != "/embed":
            self._send(404, {"error": "not_found"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        payload = json.loads(self.rfile.read(length).decode("utf-8") if length > 0 else "{}")
        inputs = payload.get("inputs", [])
        vectors = []
        for idx, _ in enumerate(inputs):
            vector = [0.0] * VECTOR_SIZE
            if VECTOR_SIZE > 0:
                vector[idx % VECTOR_SIZE] = 1.0
            vectors.append(vector)
        self._send(200, {
            "model": "migration-smoke-mock",
            "dimension": VECTOR_SIZE,
            "vectors": vectors,
        })

if __name__ == "__main__":
    HTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
PY

echo "[migration-smoke] starting mock embeddings service"
"${CONTAINER_CMD}" rm -f "${MOCK_NAME}" >/dev/null 2>&1 || true
"${CONTAINER_CMD}" run -d --name "${MOCK_NAME}" \
  -p "${MOCK_PORT}:8000" \
  -e VECTOR_SIZE="${VECTOR_SIZE}" \
  -v "${WORKDIR}/mock_embeddings_server.py:/mock_embeddings_server.py:ro" \
  python:3.12-alpine \
  python /mock_embeddings_server.py >/dev/null

echo "[migration-smoke] waiting for mock embeddings health"
for _ in $(seq 1 60); do
  if curl -fsS "http://localhost:${MOCK_PORT}/health" >/dev/null; then
    break
  fi
  sleep 1
done
curl -fsS "http://localhost:${MOCK_PORT}/health" >/dev/null

echo "[migration-smoke] running migration utility"
POSTGRES_HOST=127.0.0.1 \
POSTGRES_PORT="${PG_PORT}" \
POSTGRES_DB="${PG_DB}" \
POSTGRES_USER="${PG_USER}" \
POSTGRES_PASSWORD="${PG_PASSWORD}" \
POSTGRES_SCHEMA=arxiv \
POSTGRES_TABLE=metadata \
QDRANT_URL="${QDRANT_URL}" \
QDRANT_COLLECTION="${QDRANT_COLLECTION}" \
VECTOR_SIZE="${VECTOR_SIZE}" \
EMBEDDING_SERVICE_URL="http://127.0.0.1:${MOCK_PORT}" \
CHUNK_SIZE=1 \
EMBEDDING_BATCH_SIZE=1 \
PARITY_SAMPLE_SIZE=2 \
CHECKPOINT_FILE="${CHECKPOINT_FILE}" \
RESUME=false \
bash scripts/postgres_to_qdrant_migration.sh

echo "[migration-smoke] verifying checkpoint completion"
python3 - <<'PY' "${CHECKPOINT_FILE}"
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    payload = json.load(f)

if payload.get("completed") is not True:
    raise SystemExit(f"checkpoint not completed: {payload}")

print(f"[migration-smoke] checkpoint verified: completed=true, migrated_records={payload.get('migrated_records')}")
PY

echo "[migration-smoke] verifying migrated point count in qdrant"
COUNT_PAYLOAD="$(curl -fsS -X POST "${QDRANT_URL}/collections/${QDRANT_COLLECTION}/points/count" -H 'Content-Type: application/json' -d '{"exact": true}')"

python3 - <<'PY' <<<"${COUNT_PAYLOAD}"
import json
import sys

payload = json.loads(sys.stdin.read())
count = int(payload.get("result", {}).get("count", -1))
if count != 2:
    raise SystemExit(f"unexpected qdrant count: {count}, payload={payload}")

print(f"[migration-smoke] qdrant count verified: {count}")
PY

echo "[migration-smoke] PostgreSQL->Qdrant migration smoke checks passed"
