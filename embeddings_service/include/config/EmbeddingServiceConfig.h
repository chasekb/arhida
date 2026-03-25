#pragma once

#include <string>

struct EmbeddingServiceConfig {
  std::string host = "0.0.0.0";
  int port = 8000;
  std::string model_name = "bge-small-en-v1.5";
  std::string model_path = "/models/bge-small-en-v1.5/model.onnx";
  std::string tokenizer_path = "/models/bge-small-en-v1.5/tokenizer";
  int model_dimension = 384;
  int max_batch_size = 64;
  int request_timeout_ms = 30000;
  std::string device = "cpu";
  std::string accelerator_backend = "onnx";
  std::string ort_execution_provider = "CPU";
  int ort_intra_threads = 0;
  int ort_inter_threads = 0;
  std::string ort_graph_optimization_level = "all";
  bool accelerator_fallback = false;
  std::string service_version = "0.1.0";
  bool strict_model_validation = true;
  bool model_loaded = false;
  bool tokenizer_loaded = false;

  static EmbeddingServiceConfig fromEnvironment();

  bool shouldFallbackToCpu() const;
};
