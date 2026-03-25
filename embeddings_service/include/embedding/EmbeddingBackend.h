#pragma once

#include <string>
#include <vector>

using BatchInput = std::vector<std::string>;

class EmbeddingBackend {
public:
  virtual ~EmbeddingBackend() = default;
  virtual void initialize() = 0;
  virtual std::vector<std::vector<float>> embed(const BatchInput& input) = 0;
  virtual std::string backendName() const = 0;
  virtual std::string executionProvider() const = 0;
};
