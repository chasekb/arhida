#include "embedding/OnnxCudaBackend.h"

#include <stdexcept>

OnnxCudaBackend::OnnxCudaBackend(int output_dimension)
    : output_dimension_(output_dimension) {}

void OnnxCudaBackend::initialize() {
  if (output_dimension_ <= 0) {
    throw std::runtime_error(
        "OnnxCudaBackend requires positive output dimension");
  }
  initialized_ = true;
}

std::vector<std::vector<float>>
OnnxCudaBackend::embed(const BatchInput& input) {
  if (!initialized_) {
    throw std::runtime_error("OnnxCudaBackend used before initialize()");
  }

  std::vector<std::vector<float>> vectors;
  vectors.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    vectors.emplace_back(static_cast<std::size_t>(output_dimension_), 0.0f);
  }
  return vectors;
}

std::string OnnxCudaBackend::backendName() const { return "onnx-cuda"; }
