#pragma once

#include "embedding/EmbeddingBackend.h"

class OnnxCudaBackend : public EmbeddingBackend {
public:
  explicit OnnxCudaBackend(int output_dimension);

  void initialize() override;
  std::vector<std::vector<float>> embed(const BatchInput& input) override;
  std::string backendName() const override;
  std::string executionProvider() const override;

private:
  int output_dimension_;
  bool initialized_ = false;
};
