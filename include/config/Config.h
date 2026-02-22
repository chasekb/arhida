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
    
    // arXiv configuration
    int getRateLimitDelay() const { return rate_limit_delay_; }
    int getBatchSize() const { return batch_size_; }
    int getMaxRetries() const { return max_retries_; }
    int getRetryAfter() const { return retry_after_; }
    
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
    
    // arXiv settings
    int rate_limit_delay_;
    int batch_size_;
    int max_retries_;
    int retry_after_;
    
    // Docker settings
    std::string docker_host_;
    std::string docker_user_file_;
    std::string docker_password_file_;
};
