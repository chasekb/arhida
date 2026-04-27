#!/usr/bin/env bash
set -euo pipefail

# Two-stage cutover helper:
# 1. Migrate PostgreSQL data into Qdrant.
# 2. Verify the migrated collection against the source.
# The normal app runtime already reads from Qdrant, so this script focuses on
# the source-to-target transfer and verification boundary.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MIGRATION_SCRIPT="${MIGRATION_SCRIPT:-${ROOT_DIR}/scripts/postgres_to_qdrant_migration.sh}"

echo "[cutover] stage 1/2: migrate PostgreSQL data into Qdrant"
MIGRATION_STAGE=migrate bash "${MIGRATION_SCRIPT}"

echo "[cutover] stage 2/2: verify PostgreSQL -> Qdrant parity"
MIGRATION_STAGE=verify bash "${MIGRATION_SCRIPT}"

echo "[cutover] migration and verification completed"
