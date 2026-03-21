#pragma once

#include <string>

struct EmbeddingServiceConfig {
  std::string host = "0.0.0.0";
  int port = 8000;
  std::string model_name = "bge-small-en-v1.5";
  int model_dimension = 384;
  int max_batch_size = 64;
  std::string device = "cpu";
  std::string accelerator_backend = "onnx";
  std::string service_version = "0.1.0";
  bool model_loaded = false;
  bool tokenizer_loaded = false;

  static EmbeddingServiceConfig fromEnvironment();
};
