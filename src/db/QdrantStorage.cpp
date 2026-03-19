/**
 * @file QdrantStorage.cpp
 * @brief Qdrant storage scaffold implementation
 * @author Bernard Chase
 */

#include "db/QdrantStorage.h"
#include "config/Config.h"
#include "utils/Logger.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace {
size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t total_size = size * nmemb;
  auto *buffer = static_cast<std::string *>(userp);
  buffer->append(static_cast<char *>(contents), total_size);
  return total_size;
}
} // namespace

QdrantStorage::QdrantStorage() : connected_(false) {
  Config &config = Config::instance();
  base_url_ = config.getQdrantUrl();
  collection_name_ = config.getQdrantCollection();
}

void QdrantStorage::connect() {
  Config &config = Config::instance();
  base_url_ = config.getQdrantUrl();
  collection_name_ = config.getQdrantCollection();

  spdlog::info("Initializing Qdrant storage at {} (collection: {})",
               base_url_, collection_name_);

  ensureCollection();
  validateCollectionConfiguration();
  connected_ = true;
}

void QdrantStorage::disconnect() { connected_ = false; }

bool QdrantStorage::isConnected() const { return connected_; }

void QdrantStorage::createSchema(const std::string &schema_name) {
  spdlog::debug("QdrantStorage::createSchema called with '{}' (no-op)", schema_name);
}

void QdrantStorage::createTable(const std::string &schema_name,
                               const std::string &table_name) {
  spdlog::info("Ensuring Qdrant collection is ready for {}.{}", schema_name,
               table_name);
  ensureCollection();
  validateCollectionConfiguration();
}

void QdrantStorage::createIndexes(const std::string &schema_name,
                                  const std::string &table_name) {
  spdlog::debug("QdrantStorage::createIndexes called for {}.{} (no-op scaffold)",
                schema_name, table_name);
}

std::string QdrantStorage::getCollectionName() const {
  return collection_name_;
}

std::string QdrantStorage::performRequest(const std::string &method,
                                         const std::string &path,
                                         const std::string &body,
                                         long *response_code) const {
  CURL *curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Failed to initialize CURL for Qdrant request");
  }

  std::string response;
  std::string url = base_url_ + path;
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);

  if (method == "PUT") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  } else if (method == "GET") {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  } else {
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    throw std::runtime_error("Unsupported Qdrant HTTP method: " + method);
  }

  CURLcode result = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (response_code) {
    *response_code = http_code;
  }

  if (result != CURLE_OK) {
    throw std::runtime_error(std::string("Qdrant request failed: ") +
                             curl_easy_strerror(result));
  }

  return response;
}

bool QdrantStorage::collectionExists() const {
  long response_code = 0;
  performRequest("GET", "/collections/" + collection_name_, std::string(),
                 &response_code);

  if (response_code == 200) {
    return true;
  }
  if (response_code == 404) {
    return false;
  }

  throw std::runtime_error("Unexpected Qdrant response when checking collection: " +
                           std::to_string(response_code));
}

void QdrantStorage::ensureCollection() {
  Config &config = Config::instance();

  if (collectionExists()) {
    spdlog::info("Qdrant collection '{}' already exists", collection_name_);
    return;
  }

  json request_body = {
      {"vectors",
       {{"size", config.getVectorSize()}, {"distance", "Cosine"}}}};

  long response_code = 0;
  auto response = performRequest("PUT", "/collections/" + collection_name_,
                                 request_body.dump(), &response_code);

  if (response_code < 200 || response_code >= 300) {
    spdlog::error("Failed to create Qdrant collection '{}': HTTP {} response {}",
                  collection_name_, response_code, response);
    throw std::runtime_error("Qdrant collection creation failed");
  }

  spdlog::info("Created Qdrant collection '{}' with vector size {}",
               collection_name_, config.getVectorSize());
}

void QdrantStorage::validateCollectionConfiguration() const {
  Config &config = Config::instance();

  long response_code = 0;
  auto response = performRequest("GET", "/collections/" + collection_name_,
                                 std::string(), &response_code);

  if (response_code != 200) {
    throw std::runtime_error("Unable to validate Qdrant collection configuration");
  }

  auto payload = json::parse(response);
  int configured_size = payload["result"]["config"]["params"]["vectors"]["size"]
                            .get<int>();

  if (configured_size != config.getVectorSize()) {
    throw std::runtime_error("Qdrant collection vector size mismatch: expected " +
                             std::to_string(config.getVectorSize()) +
                             ", got " + std::to_string(configured_size));
  }

  spdlog::info("Validated Qdrant collection '{}' with vector size {}",
               collection_name_, configured_size);
}