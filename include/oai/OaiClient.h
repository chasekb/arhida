/**
 * @file OaiClient.h
 * @brief OAI-PMH client for harvesting from arXiv
 * @author Bernard Chase
 */

#pragma once

#include <string>
#include <vector>
#include <curl/curl.h>
#include "Record.h"

class OaiClient {
public:
    OaiClient(const std::string& base_url);
    ~OaiClient();
    
    // Harvest records from OAI-PMH
    std::vector<Record> listRecords(
        const std::string& metadata_prefix,
        const std::string& set_spec,
        const std::string& from_date,
        const std::string& until_date);
    
    // HTTP client methods
    void setRateLimitDelay(int delay_seconds);
    void setMaxRetries(int max_retries);
    
private:
    std::string base_url_;
    CURL* curl_;
    int rate_limit_delay_;
    int max_retries_;
    
    // Internal methods
    std::string fetchUrl(const std::string& url);
    void rateLimitWait();
    std::vector<Record> parseXmlResponse(const std::string& xml);
    
    // CURL callback
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};
