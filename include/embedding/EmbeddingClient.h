/**
 * @file EmbeddingClient.h
 * @brief HTTP client for local embeddings service
 * @author Bernard Chase
 */

#pragma once

#include <string>
#include <vector>

class EmbeddingClient {
public:
    EmbeddingClient();

    bool healthCheck() const;
    std::vector<std::vector<float>> embed(const std::vector<std::string>& inputs) const;

private:
    std::string performRequest(const std::string& method,
                               const std::string& path,
                               const std::string& body = std::string(),
                               long* response_code = nullptr) const;

    std::string base_url_;
    int timeout_ms_;
    int max_batch_size_;
    int retry_count_;
};