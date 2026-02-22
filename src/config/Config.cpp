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

  // arXiv settings
  rate_limit_delay_ = std::stoi(getEnv("ARXIV_RATE_LIMIT_DELAY", "3"));
  batch_size_ = std::stoi(getEnv("ARXIV_BATCH_SIZE", "2000"));
  max_retries_ = std::stoi(getEnv("ARXIV_MAX_RETRIES", "3"));
  retry_after_ = std::stoi(getEnv("ARXIV_RETRY_AFTER", "5"));

  // Docker settings
  docker_host_ = getEnv("DOCKER_POSTGRES_HOST", "db-local");
  docker_user_file_ =
      getEnv("DOCKER_POSTGRES_USER_FILE", "/run/secrets/postgres-u");
  docker_password_file_ =
      getEnv("DOCKER_POSTGRES_PASSWORD_FILE", "/run/secrets/postgres-p");
}
