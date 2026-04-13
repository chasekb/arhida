/**
 * @file migrate_main.cpp
 * @brief CLI entry point for PostgreSQL -> Qdrant historical migration
 */

#include <CLI/CLI.hpp>
#include <exception>

#include "config/Config.h"
#include "migration/PostgresToQdrantMigrator.h"
#include "utils/Logger.h"

int main(int argc, char **argv) {
  Config &config = Config::instance();
  config.load();
  Logger::init();

  CLI::App app{"PostgreSQL -> Qdrant historical migration utility"};

  PostgresToQdrantMigrator::Options options;
  bool no_resume = false;

  app.add_option("--chunk-size", options.chunk_size,
                 "Number of PostgreSQL records fetched per migration chunk")
      ->default_val(options.chunk_size);
  app.add_option("--embedding-batch-size", options.embedding_batch_size,
                 "Embedding request batch size")
      ->default_val(options.embedding_batch_size);
  app.add_option("--parity-sample-size", options.parity_sample_size,
                 "Number of source records used for identifier/date/set parity sampling")
      ->default_val(options.parity_sample_size);
  app.add_option("--checkpoint-file", options.checkpoint_file,
                 "Checkpoint file path for migration resume state")
      ->default_val(options.checkpoint_file);
  app.add_flag("--no-resume", no_resume,
               "Ignore checkpoint state and start migration from offset 0");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &e) {
    return app.exit(e);
  }

  options.resume = !no_resume;

  try {
    PostgresToQdrantMigrator migrator(options);
    return migrator.run();
  } catch (const std::exception &e) {
    spdlog::error("Migration failed: {}", e.what());
    return 1;
  }
}
