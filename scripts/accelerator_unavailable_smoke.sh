#!/usr/bin/env bash
set -euo pipefail

# Failure-mode smoke harness: verify embeddings service fails fast when an
# unavailable/unsupported accelerator mode is requested.

COMPOSE_CMD="${COMPOSE_CMD:-docker-compose}"
EMBEDDINGS_SERVICE="${EMBEDDINGS_SERVICE:-embeddings}"

# Use a deliberately unavailable accelerator mode to validate fail-fast startup.
REQUESTED_DEVICE="${REQUESTED_DEVICE:-cuda-unavailable}"
REQUESTED_ACCELERATOR_BACKEND="${REQUESTED_ACCELERATOR_BACKEND:-onnx}"
EXPECT_FALLBACK="${EXPECT_FALLBACK:-false}"

set +e
SERVICE_OUTPUT="$(${COMPOSE_CMD} run --rm --no-deps \
  -e DEVICE=${REQUESTED_DEVICE} \
  -e ACCELERATOR_BACKEND=${REQUESTED_ACCELERATOR_BACKEND} \
  -e ACCELERATOR_FALLBACK_TO_CPU=${EXPECT_FALLBACK} \
  ${EMBEDDINGS_SERVICE} 2>&1)"
SERVICE_EXIT=$?
set -e

if [[ "${EXPECT_FALLBACK}" == "true" ]]; then
  if [[ ${SERVICE_EXIT} -ne 0 ]]; then
    echo "[failure-smoke] expected startup success with ACCELERATOR_FALLBACK_TO_CPU=true"
    echo "${SERVICE_OUTPUT}"
    exit 1
  fi

  if [[ "${SERVICE_OUTPUT}" != *"falling back to CPU backend"* ]]; then
    echo "[failure-smoke] expected CPU fallback log was not observed"
    echo "${SERVICE_OUTPUT}"
    exit 1
  fi

  echo "[failure-smoke] verified fallback-to-CPU behavior for unavailable accelerator request"
  exit 0
fi

if [[ ${SERVICE_EXIT} -eq 0 ]]; then
  echo "[failure-smoke] expected embeddings startup failure for unavailable accelerator request"
  echo "${SERVICE_OUTPUT}"
  exit 1
fi

if [[ "${SERVICE_OUTPUT}" != *"Unsupported embedding backend/device combination"* ]]; then
  echo "[failure-smoke] embeddings service failed, but expected accelerator selection error was not observed"
  echo "${SERVICE_OUTPUT}"
  exit 1
fi

echo "[failure-smoke] verified fail-fast behavior for unavailable accelerator request"
