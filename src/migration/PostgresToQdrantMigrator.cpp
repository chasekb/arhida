/**
 * @file PostgresToQdrantMigrator.cpp
 * @brief Historical PostgreSQL -> Qdrant migration utility
 */

#include "migration/PostgresToQdrantMigrator.h"

#include "db/Database.h"
#include "db/QdrantStorage.h"
#include "embedding/EmbeddingClient.h"
#include "embedding/EmbeddingTextBuilder.h"
#include "utils/Logger.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <utility>

using json = nlohmann::json;

namespace {

std::string dateKey(const std::string &datestamp) {
  if (datestamp.size() >= 10) {
    return datestamp.substr(0, 10);
  }
  return datestamp;
}

} // namespace

PostgresToQdrantMigrator::PostgresToQdrantMigrator(Options options)
    : options_(std::move(options)) {}

int PostgresToQdrantMigrator::run() {
  Database postgres;
  QdrantStorage qdrant;
  EmbeddingClient embedding_client;

  postgres.connect();
  qdrant.connect();
  qdrant.initialize();

  if (!embedding_client.healthCheck()) {
    throw std::runtime_error(
        "Embeddings service health check failed before migration");
  }

  const auto total_source_records = postgres.countRecords();
  if (total_source_records == 0) {
    spdlog::info("No PostgreSQL records found. Nothing to migrate.");
    return 0;
  }

  CheckpointState state = loadCheckpoint();
  if (!options_.resume) {
    state = CheckpointState{};
  }

  if (state.completed && state.offset >= total_source_records) {
    spdlog::info("Migration checkpoint is already marked completed.");
    const bool parity_ok = runParityValidation(options_.parity_sample_size);
    return parity_ok ? 0 : 2;
  }

  const std::size_t chunk_size = std::max<std::size_t>(1, options_.chunk_size);
  const std::size_t embed_batch_size =
      std::max<std::size_t>(1, options_.embedding_batch_size);

  spdlog::info(
      "Starting PostgreSQL -> Qdrant migration (source_total={}, resume={}, "
      "offset={}, chunk_size={}, embed_batch_size={})",
      total_source_records, options_.resume, state.offset, chunk_size,
      embed_batch_size);

  while (state.offset < total_source_records) {
    auto records = postgres.fetchRecordsChunk(chunk_size, state.offset);
    if (records.empty()) {
      break;
    }

    std::vector<std::string> embedding_inputs;
    embedding_inputs.reserve(records.size());
    for (const auto &record : records) {
      embedding_inputs.push_back(EmbeddingTextBuilder::build(record));
    }

    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(records.size());
    for (std::size_t i = 0; i < embedding_inputs.size(); i += embed_batch_size) {
      const auto end = std::min(i + embed_batch_size, embedding_inputs.size());
      std::vector<std::string> batch(embedding_inputs.begin() + i,
                                     embedding_inputs.begin() + end);
      auto vectors = embedding_client.embed(batch);
      embeddings.insert(embeddings.end(), vectors.begin(), vectors.end());
    }

    if (embeddings.size() != records.size()) {
      throw std::runtime_error(
          "Embedding response size mismatch during migration chunk");
    }

    qdrant.upsertRecordsBatch(records, embeddings);

    state.offset += records.size();
    state.migrated_records += records.size();
    const std::string last_identifier = records.back().header_identifier;
    persistCheckpoint(state, last_identifier);

    const double percent =
        (static_cast<double>(state.offset) * 100.0) /
        static_cast<double>(total_source_records);
    spdlog::info(
        "Migration progress: offset={}/{} ({:.2f}%), migrated_records={}, "
        "last_identifier={}",
        state.offset, total_source_records, percent, state.migrated_records,
        last_identifier);
  }

  const bool parity_ok = runParityValidation(options_.parity_sample_size);
  if (!parity_ok) {
    state.completed = false;
    persistCheckpoint(state, "");
    spdlog::error("Parity validation failed. Cutover criteria not met.");
    return 2;
  }

  state.completed = true;
  persistCheckpoint(state, "");

  spdlog::info(
      "Cutover criteria satisfied: migration completed and parity checks passed "
      "(total/source={}, total/target={}).",
      postgres.countRecords(), qdrant.countPoints());
  return 0;
}

PostgresToQdrantMigrator::CheckpointState
PostgresToQdrantMigrator::loadCheckpoint() const {
  CheckpointState state;

  if (!options_.resume || options_.checkpoint_file.empty()) {
    return state;
  }

  std::ifstream in(options_.checkpoint_file);
  if (!in.is_open()) {
    return state;
  }

  auto payload = json::parse(in, nullptr, false);
  if (payload.is_discarded()) {
    throw std::runtime_error("Failed to parse migration checkpoint file: " +
                             options_.checkpoint_file);
  }

  state.offset = payload.value("offset", static_cast<std::size_t>(0));
  state.migrated_records =
      payload.value("migrated_records", static_cast<std::size_t>(0));
  state.completed = payload.value("completed", false);
  return state;
}

void PostgresToQdrantMigrator::persistCheckpoint(
    const CheckpointState &state, const std::string &last_identifier) const {
  if (options_.checkpoint_file.empty()) {
    return;
  }

  const std::filesystem::path checkpoint_path(options_.checkpoint_file);
  if (checkpoint_path.has_parent_path()) {
    std::filesystem::create_directories(checkpoint_path.parent_path());
  }

  const auto now = std::chrono::system_clock::now();
  const auto epoch_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count();

  json payload = {
      {"offset", state.offset},
      {"migrated_records", state.migrated_records},
      {"completed", state.completed},
      {"last_identifier", last_identifier},
      {"updated_at_epoch", epoch_seconds}};

  std::ofstream out(options_.checkpoint_file, std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to open checkpoint file for write: " +
                             options_.checkpoint_file);
  }

  out << payload.dump(2) << '\n';
}

bool PostgresToQdrantMigrator::runParityValidation(std::size_t sample_size) const {
  Database postgres;
  QdrantStorage qdrant;
  postgres.connect();
  qdrant.connect();

  const auto source_total = postgres.countRecords();
  const auto target_total = qdrant.countPoints();

  if (source_total != target_total) {
    spdlog::error("Parity failed: total count mismatch (postgres={}, qdrant={})",
                  source_total, target_total);
    return false;
  }

  const std::size_t sample_limit = std::max<std::size_t>(1, sample_size);
  auto sample = postgres.fetchRecordsChunk(sample_limit, 0);
  if (sample.empty()) {
    spdlog::warn("Parity sample set is empty after count check.");
    return true;
  }

  std::set<std::string> sampled_days;
  std::set<std::string> sampled_set_specs;

  for (const auto &record : sample) {
    if (!qdrant.identifierExists(record.header_identifier)) {
      spdlog::error("Parity failed: identifier missing in Qdrant: {}",
                    record.header_identifier);
      return false;
    }

    if (!record.header_datestamp.empty()) {
      sampled_days.insert(dateKey(record.header_datestamp));
    }

    for (const auto &set_spec : record.header_setSpecs) {
      sampled_set_specs.insert(set_spec);
    }
  }

  for (const auto &day : sampled_days) {
    const auto source_count = postgres.countRecordsForDate(day);
    const auto target_count = qdrant.countPointsForDate(day);
    if (source_count != target_count) {
      spdlog::error(
          "Parity failed: date count mismatch for {} (postgres={}, qdrant={})",
          day, source_count, target_count);
      return false;
    }
  }

  for (const auto &set_spec : sampled_set_specs) {
    const auto source_count = postgres.countRecordsForSetSpec(set_spec);
    const auto target_count = qdrant.countPointsForSetSpec(set_spec);
    if (source_count != target_count) {
      spdlog::error(
          "Parity failed: set-spec count mismatch for {} (postgres={}, "
          "qdrant={})",
          set_spec, source_count, target_count);
      return false;
    }
  }

  spdlog::info(
      "Parity validation passed (total, identifier sample, date sample, "
      "set-spec sample).");
  return true;
}
