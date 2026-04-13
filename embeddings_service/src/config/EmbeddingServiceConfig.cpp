#include "config/EmbeddingServiceConfig.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace {
std::string getEnvString(const char* key, const std::string& default_value) {
  const char* value = std::getenv(key);
  return value ? std::string(value) : default_value;
}

int getEnvInt(const char* key, int default_value) {
  const char* value = std::getenv(key);
  if (!value) {
    return default_value;
  }
  try {
    return std::stoi(value);
  } catch (...) {
    return default_value;
  }
}

bool getEnvBool(const char* key, bool default_value) {
  const char* value = std::getenv(key);
  if (!value) {
    return default_value;
  }

  std::string normalized(value);
  for (char& ch : normalized) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }

  if (normalized == "1" || normalized == "true" || normalized == "yes") {
    return true;
  }
  if (normalized == "0" || normalized == "false" || normalized == "no") {
    return false;
  }
  return default_value;
}

bool fileExists(const std::string& path) {
  return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

bool directoryExists(const std::string& path) {
  return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

std::string toUpper(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return value;
}
} // namespace

EmbeddingServiceConfig EmbeddingServiceConfig::fromEnvironment() {
  EmbeddingServiceConfig cfg;
  cfg.host = getEnvString("SERVICE_HOST", cfg.host);
  cfg.port = getEnvInt("SERVICE_PORT", cfg.port);
  cfg.model_name = getEnvString("MODEL_NAME", cfg.model_name);
  cfg.model_path = getEnvString("MODEL_PATH", cfg.model_path);
  cfg.tokenizer_path = getEnvString("TOKENIZER_PATH", cfg.tokenizer_path);
  cfg.model_dimension = getEnvInt("MODEL_DIMENSION", cfg.model_dimension);
  cfg.max_batch_size = getEnvInt("MAX_BATCH_SIZE", cfg.max_batch_size);
  cfg.request_timeout_ms =
      getEnvInt("REQUEST_TIMEOUT_MS", cfg.request_timeout_ms);
  cfg.device = getEnvString("DEVICE", cfg.device);
  cfg.accelerator_backend =
      getEnvString("ACCELERATOR_BACKEND", cfg.accelerator_backend);
  cfg.ort_execution_provider =
      toUpper(getEnvString("ORT_EXECUTION_PROVIDER", cfg.ort_execution_provider));
  cfg.ort_intra_threads =
      getEnvInt("ORT_INTRA_THREADS", cfg.ort_intra_threads);
  cfg.ort_inter_threads =
      getEnvInt("ORT_INTER_THREADS", cfg.ort_inter_threads);
  cfg.ort_graph_optimization_level =
      getEnvString("ORT_GRAPH_OPT_LEVEL", cfg.ort_graph_optimization_level);
  cfg.accelerator_fallback =
      getEnvBool("ACCELERATOR_FALLBACK_TO_CPU", cfg.accelerator_fallback);
  cfg.service_version = getEnvString("SERVICE_VERSION", cfg.service_version);
  cfg.strict_model_validation =
      getEnvBool("STRICT_MODEL_VALIDATION", cfg.strict_model_validation);

  cfg.model_loaded = fileExists(cfg.model_path);
  cfg.tokenizer_loaded = directoryExists(cfg.tokenizer_path) &&
                         fileExists(cfg.tokenizer_path + "/tokenizer.json");

  if (cfg.strict_model_validation && !cfg.model_loaded) {
    throw std::runtime_error("Model artifact not found at: " + cfg.model_path);
  }

  if (cfg.strict_model_validation && !cfg.tokenizer_loaded) {
    throw std::runtime_error(
        "Tokenizer assets not found at: " + cfg.tokenizer_path +
        " (expected tokenizer.json)");
  }

  return cfg;
}

bool EmbeddingServiceConfig::shouldFallbackToCpu() const {
  return accelerator_fallback;
}
