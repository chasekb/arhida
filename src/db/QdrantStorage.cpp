/**
 * @file QdrantStorage.cpp
 * @brief Qdrant storage scaffold implementation
 * @author Bernard Chase
 */

#include "db/QdrantStorage.h"
#include "config/Config.h"
#include "utils/Logger.h"
#include <stdexcept>

QdrantStorage::QdrantStorage() : connected_(false) {}

void QdrantStorage::connect() {
  Config &config = Config::instance();
  spdlog::info("Initializing Qdrant storage scaffold at {} (collection: {})",
               config.getQdrantUrl(), config.getQdrantCollection());
  connected_ = true;
}

void QdrantStorage::disconnect() { connected_ = false; }

bool QdrantStorage::isConnected() const { return connected_; }

void QdrantStorage::createSchema(const std::string &schema_name) {
  spdlog::debug("QdrantStorage::createSchema called with '{}' (no-op)", schema_name);
}

void QdrantStorage::createTable(const std::string &schema_name,
                               const std::string &table_name) {
  spdlog::info("Qdrant storage scaffold would initialize collection for {}.{}",
               schema_name, table_name);
}

void QdrantStorage::createIndexes(const std::string &schema_name,
                                  const std::string &table_name) {
  spdlog::debug("QdrantStorage::createIndexes called for {}.{} (no-op scaffold)",
                schema_name, table_name);
}

std::string QdrantStorage::getCollectionName() const {
  return Config::instance().getQdrantCollection();
}