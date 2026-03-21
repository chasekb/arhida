/**
 * @file Config.h
 * @brief Configuration management for arXiv Harvester
 * @author Bernard Chase
 */

#pragma once

#include <string>
#include <unordered_map>

class Config {
public:
    static Config& instance();
    
    void load();
    
    // PostgreSQL configuration
    std::string getPostgresHost() const { return host_; }
    std::string getPostgresDatabase() const { return database_; }
    std::string getPostgresUser() const { return user_; }
    std::string getPostgresPassword() const { return password_; }
    int getPostgresPort() const { return port_; }
    std::string getPostgresSchema() const { return schema_; }
    std::string getPostgresTable() const { return table_; }

    // Vector database configuration
    std::string getVectorDbProvider() const { return vector_db_provider_; }
    std::string getQdrantUrl() const { return qdrant_url_; }
    std::string getQdrantCollection() const { return qdrant_collection_; }
    int getVectorSize() const { return vector_size_; }

    // Embeddings service configuration
    std::string getEmbeddingServiceUrl() const { return embedding_service_url_; }
    std::string getEmbeddingModelName() const { return embedding_model_name_; }
    int getEmbeddingRequestTimeoutMs() const { return embedding_request_timeout_ms_; }
    int getEmbeddingMaxBatchSize() const { return embedding_max_batch_size_; }
    int getEmbeddingRetryCount() const { return embedding_retry_count_; }

    // Embeddings runtime configuration
    std::string getModelPath() const { return model_path_; }
    std::string getTokenizerPath() const { return tokenizer_path_; }
    std::string getDevice() const { return device_; }
    std::string getOrtExecutionProvider() const { return ort_execution_provider_; }
    std::string getCudaVisibleDevices() const { return cuda_visible_devices_; }
    std::string getAcceleratorBackend() const { return accelerator_backend_; }
    int getServicePort() const { return service_port_; }
    
    // arXiv configuration
    int getRateLimitDelay() const { return rate_limit_delay_; }
    int getBatchSize() const { return batch_size_; }
    int getMaxRetries() const { return max_retries_; }
    int getRetryAfter() const { return retry_after_; }
    int getBackfillChunkSize() const { return backfill_chunk_size_; }
    std::string getBackfillStartDate() const { return backfill_start_date_; }
    
    // Docker configuration
    std::string getDockerPostgresHost() const { return docker_host_; }
    std::string getDockerPostgresUserFile() const { return docker_user_file_; }
    std::string getDockerPostgresPasswordFile() const { return docker_password_file_; }

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    std::string getEnv(const char* key, const char* default_value);
    
    // PostgreSQL settings
    std::string host_;
    std::string database_;
    std::string user_;
    std::string password_;
    int port_;
    std::string schema_;
    std::string table_;

    // Vector database settings
    std::string vector_db_provider_;
    std::string qdrant_url_;
    std::string qdrant_collection_;
    int vector_size_;

    // Embeddings service settings
    std::string embedding_service_url_;
    std::string embedding_model_name_;
    int embedding_request_timeout_ms_;
    int embedding_max_batch_size_;
    int embedding_retry_count_;

    // Embeddings runtime settings
    std::string model_path_;
    std::string tokenizer_path_;
    std::string device_;
    std::string ort_execution_provider_;
    std::string cuda_visible_devices_;
    std::string accelerator_backend_;
    int service_port_;
    
    // arXiv settings
    int rate_limit_delay_;
    int batch_size_;
    int max_retries_;
    int retry_after_;
    int backfill_chunk_size_;
    std::string backfill_start_date_;
    
    // Docker settings
    std::string docker_host_;
    std::string docker_user_file_;
    std::string docker_password_file_;
};
