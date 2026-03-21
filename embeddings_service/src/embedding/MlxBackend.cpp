#include "embedding/MlxBackend.h"

#include <stdexcept>

MlxBackend::MlxBackend(int output_dimension)
    : output_dimension_(output_dimension) {}

void MlxBackend::initialize() {
  if (output_dimension_ <= 0) {
    throw std::runtime_error("MlxBackend requires positive output dimension");
  }
  initialized_ = true;
}

std::vector<std::vector<float>> MlxBackend::embed(const BatchInput& input) {
  if (!initialized_) {
    throw std::runtime_error("MlxBackend used before initialize()");
  }

  std::vector<std::vector<float>> vectors;
  vectors.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    vectors.emplace_back(static_cast<std::size_t>(output_dimension_), 0.0f);
  }
  return vectors;
}

std::string MlxBackend::backendName() const { return "mlx"; }
