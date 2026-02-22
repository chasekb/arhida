/**
 * @file Logger.cpp
 * @brief Logging implementation for arXiv Harvester
 * @author Bernard Chase
 */

#include "utils/Logger.h"
#include <vector>

std::shared_ptr<spdlog::logger> Logger::logger_;

void Logger::init() {
  // Create console sink
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::info);
  console_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

  // Create file sink with rotation
  try {
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/arhida.log", 1024 * 1024 * 10, 3); // 10MB max, 3 files
    file_sink->set_level(spdlog::level::debug);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

    // Create logger with both sinks
    logger_ = std::make_shared<spdlog::logger>(
        "arhida", spdlog::sinks_init_list({console_sink, file_sink}));
  } catch (const spdlog::spdlog_ex &ex) {
    // Fall back to console only if file logging fails
    logger_ = std::make_shared<spdlog::logger>("arhida", console_sink);
    logger_->warn("File logging failed: {}", ex.what());
  }

  logger_->set_level(spdlog::level::debug);
  spdlog::set_default_logger(logger_);
}
