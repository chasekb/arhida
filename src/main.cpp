/**
 * @file main.cpp
 * @brief Main entry point for arXiv Academic Paper Metadata Harvester (C++)
 * @author Bernard Chase
 * @date 2026-02-22
 */

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <CLI/CLI.hpp>

#include "config/Config.h"
#include "utils/Logger.h"
#include "db/Database.h"
#include "oai/OaiClient.h"
#include "harvester/Harvester.h"

int main(int argc, char** argv) {
    // Initialize configuration
    Config& config = Config::instance();
    config.load();
    
    // Initialize logger
    Logger::init();
    
    // Parse command line arguments
    CLI::App app{"arXiv Academic Paper Metadata Harvester - C++ Implementation"};
    
    std::string mode = "recent";
    app.add_option("-m,--mode", mode, "Harvest mode: recent, backfill, or both")
       ->check(CLI::IsMember({"recent", "backfill", "both"}));
    
    std::string start_date, end_date;
    app.add_option("--start-date", start_date, "Start date for backfill (YYYY-MM-DD)");
    app.add_option("--end-date", end_date, "End date for backfill (YYYY-MM-DD)");
    
    std::vector<std::string> set_specs;
    app.add_option("--set-specs", set_specs, "Set specifications to process")
       ->default_val({"physics", "math", "cs", "q-bio", "q-fin", "stat", "eess", "econ"});
    
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }
    
    // Log startup
    spdlog::info("===========================================");
    spdlog::info("arXiv Harvester (C++) Starting");
    spdlog::info("Mode: {}", mode);
    spdlog::info("===========================================");
    
    // Track execution time
    auto start_time = std::chrono::steady_clock::now();
    
    int total_records = 0;
    
    try {
        // Initialize database connection
        Database db;
        db.connect();
        
        // Initialize harvester
        Harvester harvester(db);
        
        if (mode == "recent" || mode == "both") {
            spdlog::info("Starting recent harvest...");
            total_records += harvester.harvestRecent(set_specs);
        }
        
        if (mode == "backfill" || mode == "both") {
            spdlog::info("Starting backfill...");
            total_records += harvester.harvestBackfill(start_date, end_date, set_specs);
        }
        
        // Clean up
        db.disconnect();
        
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
    
    // Calculate execution time
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    spdlog::info("===========================================");
    spdlog::info("HARVEST COMPLETED");
    spdlog::info("Total records processed: {}", total_records);
    spdlog::info("Time elapsed: {} seconds", duration.count());
    if (duration.count() > 0) {
        spdlog::info("Records per minute: {:.2f}", (total_records * 60.0) / duration.count());
    }
    spdlog::info("===========================================");
    
    return 0;
}
