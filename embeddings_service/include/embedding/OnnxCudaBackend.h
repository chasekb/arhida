#pragma once

#include "config/EmbeddingServiceConfig.h"
#include "embedding/EmbeddingBackend.h"

#include <unordered_map>

class OnnxCudaBackend : public EmbeddingBackend {
public:
  explicit OnnxCudaBackend(EmbeddingServiceConfig config);

  void initialize() override;
  std::vector<std::vector<float>> embed(const BatchInput& input) override;
  std::string backendName() const override;
  std::string executionProvider() const override;

private:
  EmbeddingServiceConfig config_;
  std::unordered_map<std::string, int> tokenizer_vocab_;
  int unknown_token_id_ = 0;
  bool initialized_ = false;
  bool tokenizer_initialized_ = false;
  bool session_initialized_ = false;
};
