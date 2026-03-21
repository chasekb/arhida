/**
 * @file Harvester.cpp
 * @brief Main harvester logic implementation for arXiv metadata
 * @author Bernard Chase
 */

#include "harvester/Harvester.h"
#include "config/Config.h"
#include "embedding/EmbeddingTextBuilder.h"
#include "utils/Logger.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#include <utility>

Harvester::Harvester(StorageEngine &db)
    : db_(db), oai_client_(nullptr), embedding_client_(nullptr) {
  Config &config = Config::instance();
  // Use the correct arXiv OAI-PMH endpoint
  oai_client_ = new OaiClient("https://oaipmh.arxiv.org/oai");
  oai_client_->setRateLimitDelay(config.getRateLimitDelay());
  oai_client_->setMaxRetries(config.getMaxRetries());

  if (config.getVectorDbProvider() == "qdrant") {
    embedding_client_ = new EmbeddingClient();
  }
}

Harvester::~Harvester() {
  if (oai_client_) {
    delete oai_client_;
  }
  if (embedding_client_) {
    delete embedding_client_;
  }
}

void Harvester::ensureStorageInitialized() {
  db_.initialize();
}

int Harvester::harvestRecent(const std::vector<std::string> &set_specs) {
  Config &config = Config::instance();

  // Calculate dates (last 2 days) - ensure we don't use future dates
  auto now = std::chrono::system_clock::now();
  auto two_days_ago = now - std::chrono::hours(48);
  auto one_day_ago = now - std::chrono::hours(24);

  // Get current date to avoid future dates
  std::time_t current_time = std::chrono::system_clock::to_time_t(now);
  std::tm *current_tm = std::localtime(&current_time);
  std::time_t current_midnight = std::mktime(current_tm);
  
  // Ensure we don't go beyond current date
  std::time_t from_time = std::chrono::system_clock::to_time_t(two_days_ago);
  std::time_t until_time = std::chrono::system_clock::to_time_t(one_day_ago);
  
  if (from_time > current_midnight) from_time = current_midnight;
  if (until_time > current_midnight) until_time = current_midnight;

  char from_date[11];
  char until_date[11];
  strftime(from_date, sizeof(from_date), "%Y-%m-%d",
           std::localtime(&from_time));
  strftime(until_date, sizeof(until_date), "%Y-%m-%d",
           std::localtime(&until_time));

  spdlog::info("Recent harvest from {} to {}", from_date, until_date);

  // Ensure storage backend is initialized
  ensureStorageInitialized();

  int total_records = 0;
  int successful_sets = 0;
  int failed_sets = 0;

  for (size_t i = 0; i < set_specs.size(); ++i) {
    const std::string &set_spec = set_specs[i];
    spdlog::info("Processing set_spec {}/{}: {}", i + 1, set_specs.size(),
                 set_spec);

    try {
      int records = harvestSetSpec(set_spec, from_date, until_date);
      if (records > 0) {
        total_records += records;
        successful_sets++;
        spdlog::info("Successfully processed {} records for {}", records,
                     set_spec);
      } else if (records == 0) {
        successful_sets++;
        spdlog::info("No records found for {}", set_spec);
      } else {
        failed_sets++;
        spdlog::error("Failed to process {}", set_spec);
      }
    } catch (const std::exception &e) {
      failed_sets++;
      spdlog::error("Error processing {}: {}", set_spec, e.what());
    }

    // Rate limiting between set_specs
    if (i < set_specs.size() - 1) {
      spdlog::info("Rate limiting: waiting {} seconds before next set_spec",
                   config.getRateLimitDelay());
      std::this_thread::sleep_for(
          std::chrono::seconds(config.getRateLimitDelay()));
    }
  }

  spdlog::info(
      "Recent harvest completed: {}/{} sets successful, {} records total",
      successful_sets, set_specs.size(), total_records);

  return total_records;
}

int Harvester::harvestBackfill(const std::string &start_date,
                               const std::string &end_date,
                               const std::vector<std::string> &set_specs) {
  Config &config = Config::instance();

  std::string start =
      start_date.empty() ? config.getBackfillStartDate() : start_date;
  
  // Use current date as default end date instead of hardcoded 2026-01-01
  std::string end;
  if (end_date.empty()) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm *tm_ptr = std::localtime(&time_t);
    // Set to yesterday to avoid potential issues with current day
    tm_ptr->tm_mday -= 1;
    mktime(tm_ptr); // Normalize
    char date_str[11];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm_ptr);
    end = date_str;
  } else {
    end = end_date;
  }

  spdlog::info("Backfill from {} to {}", start, end);

  // Ensure storage backend is initialized
  ensureStorageInitialized();

  int total_records = 0;

  for (const auto &set_spec : set_specs) {
    spdlog::info("Backfilling set_spec: {}", set_spec);

    // Get missing dates
    std::vector<std::string> missing_dates =
        getMissingDates(start, end, set_spec);

    if (missing_dates.empty()) {
      spdlog::info("No missing dates for {}", set_spec);
      continue;
    }

    spdlog::info("Found {} missing dates for {}", missing_dates.size(),
                 set_spec);

    // Process in configurable chunks
    const size_t chunk_size =
        static_cast<size_t>(std::max(1, config.getBackfillChunkSize()));
    for (size_t i = 0; i < missing_dates.size(); i += chunk_size) {
      size_t end_idx = std::min(i + chunk_size, missing_dates.size());

      for (size_t j = i; j < end_idx; ++j) {
        const std::string &date_str = missing_dates[j];

        try {
          // Single day range
          int records = harvestSetSpec(set_spec, date_str, date_str);

          if (records > 0) {
            total_records += records;
            spdlog::info("Backfilled {} records for {} on {}", records,
                         set_spec, date_str);
          }

          // Rate limiting
          std::this_thread::sleep_for(
              std::chrono::seconds(config.getRateLimitDelay()));

        } catch (const std::exception &e) {
          spdlog::error("Error backfilling {} for {}: {}", set_spec, date_str,
                        e.what());
        }
      }

      // Rate limiting between chunks
      if (end_idx < missing_dates.size()) {
        spdlog::info("Rate limiting: waiting 5 seconds before next chunk");
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    }
  }

  spdlog::info("Backfill completed: {} records total", total_records);
  return total_records;
}

int Harvester::harvestSetSpec(const std::string &set_spec,
                              const std::string &from_date,
                              const std::string &until_date) {
  try {
    std::vector<Record> records =
        oai_client_->listRecords("oai_dc", set_spec, from_date, until_date);

    if (records.empty()) {
      return 0;
    }

    insertRecords(records, set_spec);
    return static_cast<int>(records.size());

  } catch (const std::exception &e) {
    spdlog::error("Error harvesting {}: {}", set_spec, e.what());
    return -1;
  }
}

void Harvester::insertRecords(const std::vector<Record> &records,
                              const std::string &set_spec) {
  int processed = 0;

  for (const auto &record : records) {
    std::vector<float> embedding;

    if (embedding_client_) {
      try {
        std::string embedding_text = EmbeddingTextBuilder::build(record);
        auto vectors = embedding_client_->embed({embedding_text});
        if (vectors.empty()) {
          throw std::runtime_error(
              "Embeddings service returned no vectors for harvested record");
        }
        embedding = std::move(vectors.front());
      } catch (const std::exception &e) {
        spdlog::error("Embedding generation failed for record {}: {}",
                      record.header_identifier, e.what());
        continue;
      }
    }

    try {
      db_.upsertRecord(record, embedding);
      processed++;

      if (processed % 100 == 0) {
        spdlog::info("Processed {} records in current batch for {}", processed,
                     set_spec);
      }
    } catch (const std::exception &e) {
      spdlog::error("Storage upsert failed for record {}: {}",
                    record.header_identifier, e.what());
    }
  }

  spdlog::info("Inserted {} records for {}", processed, set_spec);
}

std::vector<std::string>
Harvester::getMissingDates(const std::string &start_date,
                           const std::string &end_date,
                           const std::string &set_spec) {
  return db_.getMissingDates(start_date, end_date, set_spec);
}
