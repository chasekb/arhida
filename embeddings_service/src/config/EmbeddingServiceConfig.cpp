#include "config/EmbeddingServiceConfig.h"

#include <cstdlib>

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
} // namespace

EmbeddingServiceConfig EmbeddingServiceConfig::fromEnvironment() {
  EmbeddingServiceConfig cfg;
  cfg.host = getEnvString("SERVICE_HOST", cfg.host);
  cfg.port = getEnvInt("SERVICE_PORT", cfg.port);
  cfg.model_name = getEnvString("MODEL_NAME", cfg.model_name);
  cfg.model_dimension = getEnvInt("MODEL_DIMENSION", cfg.model_dimension);
  cfg.max_batch_size = getEnvInt("MAX_BATCH_SIZE", cfg.max_batch_size);
  cfg.device = getEnvString("DEVICE", cfg.device);
  cfg.accelerator_backend =
      getEnvString("ACCELERATOR_BACKEND", cfg.accelerator_backend);
  cfg.service_version = getEnvString("SERVICE_VERSION", cfg.service_version);

  // Phase 6 scaffolding defaults: startup contract exists, model wiring comes
  // in later phases.
  cfg.model_loaded = true;
  cfg.tokenizer_loaded = true;
  return cfg;
}
