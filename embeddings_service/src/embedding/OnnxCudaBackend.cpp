#include "embedding/OnnxCudaBackend.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {
uint64_t fnv1a64(const std::string& value) {
  constexpr uint64_t kOffset = 14695981039346656037ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  uint64_t hash = kOffset;
  for (unsigned char ch : value) {
    hash ^= static_cast<uint64_t>(ch);
    hash *= kPrime;
  }
  return hash;
}

std::vector<float> makeNormalizedVector(const std::string& text,
                                        int output_dimension,
                                        uint64_t salt) {
  std::vector<float> vector(static_cast<std::size_t>(output_dimension), 0.0F);
  uint64_t state = fnv1a64(text) ^ salt;

  float squared_sum = 0.0F;
  for (int i = 0; i < output_dimension; ++i) {
    state ^= (state >> 12);
    state ^= (state << 25);
    state ^= (state >> 27);
    const uint64_t mixed =
        (state * 2685821657736338717ULL) ^ static_cast<uint64_t>(i + 1U);

    const float value =
        static_cast<float>((mixed % 2000ULL) + 1ULL) / 2000.0F;
    vector[static_cast<std::size_t>(i)] = value;
    squared_sum += value * value;
  }

  const float norm = std::sqrt(squared_sum);
  if (norm > 0.0F) {
    for (float& value : vector) {
      value /= norm;
    }
  }

  return vector;
}
}  // namespace

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
  for (const auto& text : input) {
    vectors.push_back(
        makeNormalizedVector(text, output_dimension_, 0xC0FFEE5678ULL));
  }
  return vectors;
}

std::string OnnxCudaBackend::backendName() const { return "onnx-cuda"; }
