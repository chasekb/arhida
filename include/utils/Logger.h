/**
 * @file Logger.h
 * @brief Logging utilities for arXiv Harvester
 * @author Bernard Chase
 */

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>

class Logger {
public:
    static void init();
    
    static std::shared_ptr<spdlog::logger> getLogger() {
        return logger_;
    }

private:
    static std::shared_ptr<spdlog::logger> logger_;
};
