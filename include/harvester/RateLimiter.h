/**
 * @file RateLimiter.h
 * @brief Rate limiter for API requests
 * @author Bernard Chase
 */

#pragma once

#include <chrono>
#include <thread>

class RateLimiter {
public:
    RateLimiter(int delay_seconds);
    
    void wait_before_request();
    void wait_between_batches();
    void wait_between_set_specs();
    
private:
    int delay_ms_;
    std::chrono::steady_clock::time_point last_request_;
};
