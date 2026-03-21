#pragma once

#include "embedding/EmbeddingBackend.h"

class OnnxCpuBackend : public EmbeddingBackend {
public:
  explicit OnnxCpuBackend(int output_dimension);

  void initialize() override;
  std::vector<std::vector<float>> embed(const BatchInput& input) override;
  std::string backendName() const override;

private:
  int output_dimension_;
  bool initialized_ = false;
};
