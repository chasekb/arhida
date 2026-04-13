#!/usr/bin/env python3
import os
import re

import time

from fastembed import TextEmbedding
from flask import Flask, jsonify, request


MODEL_NAME = os.getenv("MODEL_NAME", "BAAI/bge-small-en-v1.5")
MODEL_DIMENSION = int(os.getenv("MODEL_DIMENSION", "384"))
MAX_BATCH_SIZE = int(os.getenv("MAX_BATCH_SIZE", "64"))
DEVICE = os.getenv("DEVICE", "cpu")
ACCELERATOR_BACKEND = os.getenv("ACCELERATOR_BACKEND", "onnx")
ORT_EXECUTION_PROVIDER = os.getenv("ORT_EXECUTION_PROVIDER", "CPU")
ORT_INTRA_THREADS = int(os.getenv("ORT_INTRA_THREADS", "0"))
ORT_INTER_THREADS = int(os.getenv("ORT_INTER_THREADS", "0"))
ORT_GRAPH_OPT_LEVEL = os.getenv("ORT_GRAPH_OPT_LEVEL", "all")
ACCELERATOR_FALLBACK = os.getenv("ACCELERATOR_FALLBACK_TO_CPU", "false").lower() in {"1", "true", "yes"}
SERVICE_PORT = int(os.getenv("SERVICE_PORT", "8000"))
SERVICE_VERSION = os.getenv("SERVICE_VERSION", "0.1.0")
MODEL_PATH = os.getenv("MODEL_PATH", "/models/bge-small-en-v1.5/model.onnx")
TOKENIZER_PATH = os.getenv("TOKENIZER_PATH", "/models/bge-small-en-v1.5/tokenizer")
STRICT_MODEL_VALIDATION = os.getenv("STRICT_MODEL_VALIDATION", "true").lower() in {"1", "true", "yes"}


def normalize_whitespace(text: str) -> str:
    return re.sub(r"\s+", " ", text.strip())


def backend_name() -> str:
    if DEVICE == "mlx" or ACCELERATOR_BACKEND == "mlx":
        return "mlx"
    if DEVICE == "cuda":
        return "onnx-cuda"
    return "onnx-cpu"


def execution_provider() -> str:
    if DEVICE == "mlx" or ACCELERATOR_BACKEND == "mlx":
        return "MLX"
    if DEVICE == "cuda":
        return "CUDA"
    return "CPU"


def check_artifacts() -> tuple[bool, bool]:
    model_loaded = os.path.isfile(MODEL_PATH)
    tokenizer_loaded = os.path.isdir(TOKENIZER_PATH) and os.path.isfile(
        os.path.join(TOKENIZER_PATH, "tokenizer.json")
    )
    if model_loaded:
        try:
            with open(MODEL_PATH, "rb") as fh:
                head = fh.read(64)
            if b"placeholder-onnx-artifact" in head:
                model_loaded = False
        except OSError:
            model_loaded = False
    allow_remote_model = os.getenv("ALLOW_REMOTE_MODEL_DOWNLOAD", "true").lower() in {"1", "true", "yes"}
    if STRICT_MODEL_VALIDATION and tokenizer_loaded and not model_loaded and not allow_remote_model:
        raise RuntimeError(f"Model artifact not found at: {MODEL_PATH}")
    return model_loaded, tokenizer_loaded


MODEL_LOADED, TOKENIZER_LOADED = check_artifacts()
MODEL = TextEmbedding(model_name=MODEL_NAME)
WARMUP_VECTOR = list(next(MODEL.embed(["warmup"])))

if len(WARMUP_VECTOR) != MODEL_DIMENSION:
    raise RuntimeError(
        f"Embedding dimension mismatch: expected {MODEL_DIMENSION}, got {len(WARMUP_VECTOR)}"
    )

app = Flask(__name__)


def embed_with_retry(inputs: list[str], attempts: int = 3, backoff_seconds: float = 2.0):
    last_error = None
    for attempt in range(1, attempts + 1):
        try:
            return [[float(value) for value in vector] for vector in MODEL.embed(inputs)]
        except Exception as exc:  # noqa: BLE001
            last_error = exc
            if attempt == attempts:
                break
            time.sleep(backoff_seconds * attempt)
    raise last_error


@app.get("/health")
def health():
    return jsonify(
        {
            "ok": True,
            "service": "arhida-embeddings-service",
            "version": SERVICE_VERSION,
            "model": MODEL_NAME,
            "dimension": MODEL_DIMENSION,
            "max_batch_size": MAX_BATCH_SIZE,
            "device": DEVICE,
            "accelerator_backend": ACCELERATOR_BACKEND,
            "execution_provider": execution_provider(),
            "requested_ort_execution_provider": ORT_EXECUTION_PROVIDER,
            "ort_intra_threads": ORT_INTRA_THREADS,
            "ort_inter_threads": ORT_INTER_THREADS,
            "ort_graph_optimization_level": ORT_GRAPH_OPT_LEVEL,
            "accelerator_fallback_enabled": ACCELERATOR_FALLBACK,
            "backend": backend_name(),
            "model_loaded": MODEL_LOADED,
            "tokenizer_loaded": TOKENIZER_LOADED,
            "warmup_complete": True,
        }
    )


@app.post("/embed")
def embed():
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        return jsonify({"error": {"code": "invalid_json", "message": "Invalid JSON body"}}), 400

    inputs = payload.get("inputs")
    if not isinstance(inputs, list):
        return jsonify({"error": {"code": "invalid_request", "message": "Request must include an array field named 'inputs'"}}), 400
    if len(inputs) > MAX_BATCH_SIZE:
        return jsonify({"error": {"code": "batch_too_large", "message": "Input batch exceeds configured max batch size"}}), 400
    if any(not isinstance(item, str) for item in inputs):
        return jsonify({"error": {"code": "invalid_input_type", "message": "All inputs must be strings"}}), 400

    normalized_inputs = [normalize_whitespace(item) for item in inputs]
    vectors = embed_with_retry(normalized_inputs)
    if any(len(vector) != MODEL_DIMENSION for vector in vectors):
        return jsonify({"error": {"code": "backend_output_mismatch", "message": "Embedding backend returned incorrect vector dimension"}}), 500

    return jsonify(
        {
            "model": MODEL_NAME,
            "dimension": MODEL_DIMENSION,
            "backend": backend_name(),
            "vectors": vectors,
        }
    )


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=SERVICE_PORT, threaded=True)