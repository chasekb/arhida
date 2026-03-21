/**
 * @file EmbeddingClient.cpp
 * @brief HTTP client for local embeddings service
 * @author Bernard Chase
 */

#include "embedding/EmbeddingClient.h"
#include "config/Config.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace {
size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total_size = size * nmemb;
  auto* buffer = static_cast<std::string*>(userp);
  buffer->append(static_cast<char*>(contents), total_size);
  return total_size;
}
} // namespace

EmbeddingClient::EmbeddingClient() {
  Config& config = Config::instance();
  base_url_ = config.getEmbeddingServiceUrl();
  timeout_ms_ = config.getEmbeddingRequestTimeoutMs();
  max_batch_size_ = config.getEmbeddingMaxBatchSize();
  retry_count_ = config.getEmbeddingRetryCount();
  expected_vector_size_ = config.getVectorSize();
}

EmbeddingClient::EmbeddingClient(const std::string& base_url,
                                 int timeout_ms,
                                 int max_batch_size,
                                 int retry_count,
                                 int expected_vector_size)
    : base_url_(base_url),
      timeout_ms_(timeout_ms),
      max_batch_size_(max_batch_size),
      retry_count_(retry_count),
      expected_vector_size_(expected_vector_size) {}

bool EmbeddingClient::healthCheck() const {
  long response_code = 0;
  performRequest("GET", "/health", std::string(), &response_code);
  return response_code >= 200 && response_code < 300;
}

std::vector<std::vector<float>>
EmbeddingClient::embed(const std::vector<std::string>& inputs) const {
  if (inputs.empty()) {
    return {};
  }

  if (static_cast<int>(inputs.size()) > max_batch_size_) {
    throw std::runtime_error("Embedding batch size exceeds configured maximum");
  }

  json request_body = {{"inputs", inputs}};

  long response_code = 0;
  std::string response;
  int attempts = 0;

  while (attempts < retry_count_) {
    response = performRequest("POST", "/embed", request_body.dump(),
                              &response_code);
    if (response_code >= 200 && response_code < 300) {
      break;
    }
    attempts++;
  }

  if (response_code < 200 || response_code >= 300) {
    throw std::runtime_error("Embedding request failed with HTTP status " +
                             std::to_string(response_code));
  }

  auto payload = json::parse(response);
  auto vectors = payload.at("vectors").get<std::vector<std::vector<float>>>();

  if (vectors.size() != inputs.size()) {
    throw std::runtime_error("Embedding response size mismatch: expected " +
                             std::to_string(inputs.size()) + ", got " +
                             std::to_string(vectors.size()));
  }

  for (const auto& vector : vectors) {
    if (expected_vector_size_ > 0 &&
        static_cast<int>(vector.size()) != expected_vector_size_) {
      throw std::runtime_error(
          "Embedding vector size mismatch: expected " +
          std::to_string(expected_vector_size_) + ", got " +
          std::to_string(vector.size()));
    }
  }

  return vectors;
}

std::string EmbeddingClient::performRequest(const std::string& method,
                                            const std::string& path,
                                            const std::string& body,
                                            long* response_code) const {
  CURL* curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Failed to initialize CURL for embeddings request");
  }

  std::string response;
  std::string url = base_url_ + path;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms_);

  if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  } else if (method == "GET") {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  } else {
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    throw std::runtime_error("Unsupported embeddings HTTP method: " + method);
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
    throw std::runtime_error(std::string("Embeddings request failed: ") +
                             curl_easy_strerror(result));
  }

  return response;
}