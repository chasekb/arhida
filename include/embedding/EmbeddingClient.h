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
    EmbeddingClient(const std::string& base_url,
                    int timeout_ms,
                    int max_batch_size,
                    int retry_count,
                    int expected_vector_size = -1);

    bool healthCheck() const;
    std::vector<std::vector<float>> embed(const std::vector<std::string>& inputs) const;

protected:
    virtual std::string performRequest(const std::string& method,
                                       const std::string& path,
                                       const std::string& body = std::string(),
                                       long* response_code = nullptr) const;

private:
    std::string base_url_;
    int timeout_ms_;
    int max_batch_size_;
    int retry_count_;
    int expected_vector_size_;
};