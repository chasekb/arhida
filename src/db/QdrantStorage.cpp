/**
 * @file QdrantStorage.cpp
 * @brief Qdrant storage scaffold implementation
 * @author Bernard Chase
 */

#include "db/QdrantStorage.h"
#include "config/Config.h"
#include "utils/Logger.h"
#include <curl/curl.h>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

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

void QdrantStorage::upsertRecord(const Record &record,
                                 const std::vector<float> &embedding) {
  json payload = {
      {"header_identifier", record.header_identifier},
      {"header_datestamp", record.header_datestamp},
      {"header_setSpecs", record.header_setSpecs},
      {"metadata_creator", record.metadata_creator},
      {"metadata_date", record.metadata_date},
      {"metadata_description", record.metadata_description},
      {"metadata_identifier", record.metadata_identifier},
      {"metadata_subject", record.metadata_subject},
      {"metadata_title", record.metadata_title},
      {"metadata_type", record.metadata_type}};

  json request_body = {
      {"points",
       json::array({{{"id", makePointId(record.header_identifier)},
                     {"vector", embedding},
                     {"payload", payload}}})}};

  long response_code = 0;
  auto response = performRequest("PUT",
                                 "/collections/" + collection_name_ + "/points",
                                 request_body.dump(), &response_code);

  if (response_code < 200 || response_code >= 300) {
    spdlog::error("Failed to upsert Qdrant point for {}: HTTP {} response {}",
                  record.header_identifier, response_code, response);
    throw std::runtime_error("Qdrant point upsert failed");
  }

  spdlog::info("Upserted Qdrant point for {}", record.header_identifier);
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
  } else if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
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

std::uint64_t QdrantStorage::makePointId(const std::string &identifier) const {
  constexpr std::uint64_t fnv_offset = 1469598103934665603ULL;
  constexpr std::uint64_t fnv_prime = 1099511628211ULL;

  std::uint64_t hash = fnv_offset;
  for (unsigned char ch : identifier) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= fnv_prime;
  }

  return hash;
}

std::vector<std::string>
QdrantStorage::getMissingDates(const std::string &start_date,
                               const std::string &end_date,
                               const std::string &set_spec) {
  std::set<std::string> existing_dates;

  json filter = {
      {"must",
       json::array({{{"key", "header_setSpecs"},
                     {"match", {{"any", json::array({set_spec})}}}},
                    {{"key", "header_datestamp"},
                     {"range",
                      {{"gte", start_date + "T00:00:00"},
                       {"lte", end_date + "T23:59:59"}}}}})}};

  json request_body = {
      {"filter", filter},
      {"with_payload", json::array({"header_datestamp"})},
      {"with_vector", false},
      {"limit", 256}};

  json offset = nullptr;
  do {
    if (!offset.is_null()) {
      request_body["offset"] = offset;
    } else {
      request_body.erase("offset");
    }

    long response_code = 0;
    auto response = performRequest(
        "POST", "/collections/" + collection_name_ + "/points/scroll",
        request_body.dump(), &response_code);

    if (response_code < 200 || response_code >= 300) {
      throw std::runtime_error("Qdrant scroll request failed with HTTP status " +
                               std::to_string(response_code));
    }

    auto payload = json::parse(response);
    if (payload.contains("result") && payload["result"].contains("points")) {
      for (const auto &point : payload["result"]["points"]) {
        if (!point.contains("payload") ||
            !point["payload"].contains("header_datestamp")) {
          continue;
        }

        std::string datestamp =
            point["payload"]["header_datestamp"].get<std::string>();
        if (datestamp.size() >= 10) {
          existing_dates.insert(datestamp.substr(0, 10));
        }
      }
    }

    offset = nullptr;
    if (payload.contains("result") &&
        payload["result"].contains("next_page_offset") &&
        !payload["result"]["next_page_offset"].is_null()) {
      offset = payload["result"]["next_page_offset"];
    }
  } while (!offset.is_null());

  std::tm start_tm = {};
  std::tm end_tm = {};
  std::istringstream start_ss(start_date);
  std::istringstream end_ss(end_date);
  start_ss >> std::get_time(&start_tm, "%Y-%m-%d");
  end_ss >> std::get_time(&end_tm, "%Y-%m-%d");

  if (start_ss.fail() || end_ss.fail()) {
    throw std::runtime_error("Invalid date format for missing date lookup");
  }

  std::time_t start_time = std::mktime(&start_tm);
  std::time_t end_time = std::mktime(&end_tm);
  if (end_time < start_time) {
    std::swap(start_time, end_time);
  }

  std::vector<std::string> missing_dates;
  for (std::time_t current = start_time; current <= end_time;
       current += 24 * 3600) {
    std::tm *current_tm = std::localtime(&current);
    char date_str[11];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", current_tm);

    if (existing_dates.find(date_str) == existing_dates.end()) {
      missing_dates.emplace_back(date_str);
    }
  }

  return missing_dates;
}