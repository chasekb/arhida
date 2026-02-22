/**
 * @file RateLimiter.cpp
 * @brief Rate limiter implementation for API requests
 * @author Bernard Chase
 */

#include "RateLimiter.h"
#include "../utils/Logger.h"

RateLimiter::RateLimiter(int delay_seconds) : delay_ms_(delay_seconds * 1000) {}

void RateLimiter::wait_before_request() {
    auto now = std::chrono::steady_clock::now();
    
    if (last_request_.time_since_epoch().count() > 0) {
        auto elapsed = now - last_request_;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        
        if (elapsed_ms < delay_ms_) {
            auto remaining = delay_ms_ - elapsed_ms;
            spdlog::debug("Rate limiting: waiting {} ms before request", remaining);
            std::this_thread::sleep_for(std::chrono::milliseconds(remaining));
        }
    }
    
    last_request_ = std::chrono::steady_clock::now();
}

void RateLimiter::wait_between_batches() {
    spdlog::debug("Rate limiting: waiting {} ms between batches", delay_ms_);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
    last_request_ = std::chrono::steady_clock::now();
}

void RateLimiter::wait_between_set_specs() {
    spdlog::debug("Rate limiting: waiting {} ms between set_specs", delay_ms_);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
    last_request_ = std::chrono::steady_clock::now();
}
