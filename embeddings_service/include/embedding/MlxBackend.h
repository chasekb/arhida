#pragma once

#include "embedding/EmbeddingBackend.h"

class MlxBackend : public EmbeddingBackend {
public:
  explicit MlxBackend(int output_dimension);

  void initialize() override;
  std::vector<std::vector<float>> embed(const BatchInput& input) override;
  std::string backendName() const override;

private:
  int output_dimension_;
  bool initialized_ = false;
};
