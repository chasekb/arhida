/**
 * @file PostgresToQdrantMigrator.h
 * @brief Historical PostgreSQL -> Qdrant migration utility
 */

#pragma once

#include <cstddef>
#include <string>

class PostgresToQdrantMigrator {
public:
  struct Options {
    std::size_t chunk_size = 200;
    std::size_t embedding_batch_size = 64;
    std::size_t parity_sample_size = 25;
    std::string checkpoint_file =
        ".migration/postgres_to_qdrant_checkpoint.json";
    bool resume = true;
  };

  explicit PostgresToQdrantMigrator(Options options);

  int run();

private:
  struct CheckpointState {
    std::size_t offset = 0;
    std::size_t migrated_records = 0;
    bool completed = false;
  };

  CheckpointState loadCheckpoint() const;
  void persistCheckpoint(const CheckpointState &state,
                         const std::string &last_identifier) const;

  bool runParityValidation(std::size_t sample_size) const;

  Options options_;
};
