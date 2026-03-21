/**
 * @file Config.cpp
 * @brief Configuration implementation for arXiv Harvester
 * @author Bernard Chase
 */

#include "config/Config.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

Config &Config::instance() {
  static Config config;
  return config;
}

std::string Config::getEnv(const char *key, const char *default_value) {
  const char *value = std::getenv(key);
  if (value) {
    return std::string(value);
  }
  return std::string(default_value ? default_value : "");
}

void Config::load() {
  // Load from .env file if exists
  std::ifstream env_file(".env");
  if (env_file.is_open()) {
    std::string line;
    while (std::getline(env_file, line)) {
      if (line.empty() || line[0] == '#')
        continue;

      size_t eq_pos = line.find('=');
      if (eq_pos != std::string::npos) {
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Remove quotes if present
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
          value = value.substr(1, value.size() - 2);
        }

        // 0 means do not overwrite existing environment variables
        setenv(key.c_str(), value.c_str(), 0);
      }
    }
    env_file.close();
  }

  // PostgreSQL settings
  host_ = getEnv("POSTGRES_HOST", "localhost");
  database_ = getEnv("POSTGRES_DB", "");
  user_ = getEnv("POSTGRES_USER", "");
  password_ = getEnv("POSTGRES_PASSWORD", "");
  port_ = std::stoi(getEnv("POSTGRES_PORT", "5432"));
  schema_ = getEnv("POSTGRES_SCHEMA", "arxiv");
  table_ = getEnv("POSTGRES_TABLE", "metadata");

  // Vector database settings
  vector_db_provider_ = getEnv("VECTOR_DB_PROVIDER", "qdrant");
  qdrant_url_ = getEnv("QDRANT_URL", "http://qdrant:6333");
  qdrant_collection_ = getEnv("QDRANT_COLLECTION", "arxiv_metadata");
  vector_size_ = std::stoi(getEnv("VECTOR_SIZE", "384"));

  // Embeddings service settings
  embedding_service_url_ =
      getEnv("EMBEDDING_SERVICE_URL", "http://embeddings:8000");
  embedding_model_name_ =
      getEnv("EMBEDDING_MODEL_NAME", "bge-small-en-v1.5");
  embedding_request_timeout_ms_ =
      std::stoi(getEnv("EMBEDDING_REQUEST_TIMEOUT_MS", "30000"));
  embedding_max_batch_size_ =
      std::stoi(getEnv("EMBEDDING_MAX_BATCH_SIZE", "64"));
  embedding_retry_count_ =
      std::stoi(getEnv("EMBEDDING_RETRY_COUNT", "3"));

  // Embeddings runtime settings
  model_path_ =
      getEnv("MODEL_PATH", "/models/bge-small-en-v1.5/model.onnx");
  tokenizer_path_ =
      getEnv("TOKENIZER_PATH", "/models/bge-small-en-v1.5/tokenizer");
  device_ = getEnv("DEVICE", "cpu");
  ort_execution_provider_ = getEnv("ORT_EXECUTION_PROVIDER", "CPU");
  cuda_visible_devices_ = getEnv("CUDA_VISIBLE_DEVICES", "0");
  accelerator_backend_ = getEnv("ACCELERATOR_BACKEND", "onnx");
  service_port_ = std::stoi(getEnv("SERVICE_PORT", "8000"));

  // arXiv settings
  rate_limit_delay_ = std::stoi(getEnv("ARXIV_RATE_LIMIT_DELAY", "3"));
  batch_size_ = std::stoi(getEnv("ARXIV_BATCH_SIZE", "2000"));
  max_retries_ = std::stoi(getEnv("ARXIV_MAX_RETRIES", "3"));
  retry_after_ = std::stoi(getEnv("ARXIV_RETRY_AFTER", "5"));
  backfill_chunk_size_ = std::stoi(getEnv("BACKFILL_CHUNK_SIZE", "7"));
  backfill_start_date_ = getEnv("BACKFILL_START_DATE", "2007-01-01");

  // Docker settings
  docker_host_ = getEnv("DOCKER_POSTGRES_HOST", "db-local");
  docker_user_file_ =
      getEnv("DOCKER_POSTGRES_USER_FILE", "/run/secrets/postgres-u");
  docker_password_file_ =
      getEnv("DOCKER_POSTGRES_PASSWORD_FILE", "/run/secrets/postgres-p");
}
