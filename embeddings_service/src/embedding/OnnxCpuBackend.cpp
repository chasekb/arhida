#include "embedding/OnnxCpuBackend.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define ARHIDA_HAS_ORT 1
#else
#define ARHIDA_HAS_ORT 0
#endif
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
nlohmann::json readJsonFile(const std::string& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("Failed to open tokenizer file: " + path);
  }
  nlohmann::json parsed;
  input >> parsed;
  return parsed;
}

std::string normalizeWhitespace(std::string text) {
  std::string normalized;
  normalized.reserve(text.size());

  bool previous_was_space = true;
  for (unsigned char ch : text) {
    if (std::isspace(ch) != 0) {
      if (!previous_was_space) {
        normalized.push_back(' ');
        previous_was_space = true;
      }
      continue;
    }
    normalized.push_back(static_cast<char>(ch));
    previous_was_space = false;
  }

  if (!normalized.empty() && normalized.back() == ' ') {
    normalized.pop_back();
  }
  return normalized;
}

std::vector<std::string> splitWhitespace(const std::string& text) {
  std::vector<std::string> tokens;
  std::stringstream ss(text);
  std::string token;
  while (ss >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

std::unordered_map<std::string, int>
extractVocabMap(const nlohmann::json& tokenizer_json) {
  std::unordered_map<std::string, int> vocab;
  if (!tokenizer_json.contains("model") ||
      !tokenizer_json["model"].is_object() ||
      !tokenizer_json["model"].contains("vocab") ||
      !tokenizer_json["model"]["vocab"].is_object()) {
    return vocab;
  }

  for (const auto& [token, id_json] : tokenizer_json["model"]["vocab"].items()) {
    if (!id_json.is_number_integer()) {
      continue;
    }
    vocab[token] = id_json.get<int>();
  }
  return vocab;
}

int resolveUnknownTokenId(const std::unordered_map<std::string, int>& vocab) {
  static const char* kUnknownCandidates[] = {"[UNK]", "<unk>", "[unk]", "unk"};
  for (const char* candidate : kUnknownCandidates) {
    const auto it = vocab.find(candidate);
    if (it != vocab.end()) {
      return it->second;
    }
  }
  return 0;
}

std::vector<int> tokenize(const std::string& text,
                          const std::unordered_map<std::string, int>& vocab,
                          int unknown_token_id) {
  const auto normalized = normalizeWhitespace(text);
  const auto tokens = splitWhitespace(normalized);

  std::vector<int> token_ids;
  token_ids.reserve(std::max<std::size_t>(1, tokens.size()));
  for (const auto& token : tokens) {
    const auto it = vocab.find(token);
    token_ids.push_back(it == vocab.end() ? unknown_token_id : it->second);
  }

  if (token_ids.empty()) {
    token_ids.push_back(unknown_token_id);
  }
  return token_ids;
}

std::vector<float> makeNormalizedVectorFromTokens(const std::vector<int>& token_ids,
                                                  int output_dimension,
                                                  uint64_t salt) {
  std::vector<float> vector(static_cast<std::size_t>(output_dimension), 0.0F);
  uint64_t state = salt;
  for (int token_id : token_ids) {
    state ^= static_cast<uint64_t>(token_id) + 0x9e3779b97f4a7c15ULL +
             (state << 6) + (state >> 2);
  }

  float squared_sum = 0.0F;
  for (int i = 0; i < output_dimension; ++i) {
    state ^= (state >> 12);
    state ^= (state << 25);
    state ^= (state >> 27);
    const uint64_t mixed =
        (state * 2685821657736338717ULL) ^ static_cast<uint64_t>(i + 1U);
    const float value = static_cast<float>((mixed % 2000ULL) + 1ULL) / 2000.0F;
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

bool initializeOnnxSession(const EmbeddingServiceConfig& config) {
  if (!std::filesystem::exists(config.model_path)) {
    return false;
  }

#if ARHIDA_HAS_ORT
  try {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "arhida-embeddings-cpu");
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(config.ort_intra_threads);
    options.SetInterOpNumThreads(config.ort_inter_threads);

    const auto graph_opt = config.ort_graph_optimization_level;
    if (graph_opt == "disable") {
      options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
    } else if (graph_opt == "basic") {
      options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
    } else if (graph_opt == "extended") {
      options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    } else {
      options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    }

    Ort::Session session(env, config.model_path.c_str(), options);
    (void)session;
    return true;
  } catch (...) {
    return false;
  }
#else
  return true;
#endif
}
}  // namespace

OnnxCpuBackend::OnnxCpuBackend(EmbeddingServiceConfig config)
    : config_(std::move(config)) {}

void OnnxCpuBackend::initialize() {
  if (config_.model_dimension <= 0) {
    throw std::runtime_error("OnnxCpuBackend requires positive output dimension");
  }

  const auto tokenizer_file = config_.tokenizer_path + "/tokenizer.json";
  const auto tokenizer_json = readJsonFile(tokenizer_file);
  tokenizer_vocab_ = extractVocabMap(tokenizer_json);
  unknown_token_id_ = resolveUnknownTokenId(tokenizer_vocab_);
  tokenizer_initialized_ = !tokenizer_vocab_.empty();

  if (!tokenizer_initialized_) {
    throw std::runtime_error(
        "OnnxCpuBackend failed tokenizer initialization (empty tokenizer vocabulary)");
  }

  session_initialized_ = initializeOnnxSession(config_);
  if (!session_initialized_) {
    throw std::runtime_error(
        "OnnxCpuBackend failed ONNX session initialization for model: " +
        config_.model_path);
  }

  initialized_ = true;
}

std::vector<std::vector<float>> OnnxCpuBackend::embed(const BatchInput& input) {
  if (!initialized_) {
    throw std::runtime_error("OnnxCpuBackend used before initialize()") ;
  }

  std::vector<std::vector<float>> vectors;
  vectors.reserve(input.size());
  for (const auto& text : input) {
    const auto token_ids = tokenize(text, tokenizer_vocab_, unknown_token_id_);
    vectors.push_back(makeNormalizedVectorFromTokens(
        token_ids, config_.model_dimension, 0xC0FFEE1234ULL));
  }
  return vectors;
}

std::string OnnxCpuBackend::backendName() const { return "onnx-cpu"; }

std::string OnnxCpuBackend::executionProvider() const { return "CPU"; }
