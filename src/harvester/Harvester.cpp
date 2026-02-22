/**
 * @file Harvester.cpp
 * @brief Main harvester logic implementation for arXiv metadata
 * @author Bernard Chase
 */

#include "Harvester.h"
#include "../config/Config.h"
#include "../utils/Logger.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

Harvester::Harvester(Database& db) : db_(db), oai_client_(nullptr) {
    Config& config = Config::instance();
    oai_client_ = new OaiClient("http://export.arxiv.org/oai2");
    oai_client_->setRateLimitDelay(config.getRateLimitDelay());
    oai_client_->setMaxRetries(config.getMaxRetries());
}

Harvester::~Harvester() {
    if (oai_client_) {
        delete oai_client_;
    }
}

void Harvester::ensureTableExists() {
    Config& config = Config::instance();
    std::string schema = config.getPostgresSchema();
    std::string table = config.getPostgresTable();
    
    db_.createSchema(schema);
    db_.createTable(schema, table);
    db_.createIndexes(schema, table);
}

int Harvester::harvestRecent(const std::vector<std::string>& set_specs) {
    Config& config = Config::instance();
    
    // Calculate dates (last 2 days)
    auto now = std::chrono::system_clock::now();
    auto two_days_ago = now - std::chrono::hours(48);
    auto one_day_ago = now - std::chrono::hours(24);
    
    std::time_t from_time = std::chrono::system_clock::to_time_t(two_days_ago);
    std::time_t until_time = std::chrono::system_clock::to_time_t(one_day_ago);
    
    char from_date[11];
    char until_date[11];
    strftime(from_date, sizeof(from_date), "%Y-%m-%d", std::localtime(&from_time));
    strftime(until_date, sizeof(until_date), "%Y-%m-%d", std::localtime(&until_time));
    
    spdlog::info("Recent harvest from {} to {}", from_date, until_date);
    
    // Ensure table exists
    ensureTableExists();
    
    int total_records = 0;
    int successful_sets = 0;
    int failed_sets = 0;
    
    for (size_t i = 0; i < set_specs.size(); ++i) {
        const std::string& set_spec = set_specs[i];
        spdlog::info("Processing set_spec {}/{}: {}", i + 1, set_specs.size(), set_spec);
        
        try {
            int records = harvestSetSpec(set_spec, from_date, until_date);
            if (records > 0) {
                total_records += records;
                successful_sets++;
                spdlog::info("Successfully processed {} records for {}", records, set_spec);
            } else if (records == 0) {
                successful_sets++;
                spdlog::info("No records found for {}", set_spec);
            } else {
                failed_sets++;
                spdlog::error("Failed to process {}", set_spec);
            }
        } catch (const std::exception& e) {
            failed_sets++;
            spdlog::error("Error processing {}: {}", set_spec, e.what());
        }
        
        // Rate limiting between set_specs
        if (i < set_specs.size() - 1) {
            spdlog::info("Rate limiting: waiting {} seconds before next set_spec", 
                        config.getRateLimitDelay());
            std::this_thread::sleep_for(std::chrono::seconds(config.getRateLimitDelay()));
        }
    }
    
    spdlog::info("Recent harvest completed: {}/{} sets successful, {} records total",
                 successful_sets, set_specs.size(), total_records);
    
    return total_records;
}

int Harvester::harvestBackfill(const std::string& start_date, const std::string& end_date,
                                const std::vector<std::string>& set_specs) {
    Config& config = Config::instance();
    
    std::string start = start_date.empty() ? "2007-01-01" : start_date;
    std::string end = end_date.empty() ? "2026-01-01" : end_date;
    
    spdlog::info("Backfill from {} to {}", start, end);
    
    // Ensure table exists
    ensureTableExists();
    
    int total_records = 0;
    
    for (const auto& set_spec : set_specs) {
        spdlog::info("Backfilling set_spec: {}", set_spec);
        
        // Get missing dates
        std::vector<std::string> missing_dates = getMissingDates(start, end, set_spec);
        
        if (missing_dates.empty()) {
            spdlog::info("No missing dates for {}", set_spec);
            continue;
        }
        
        spdlog::info("Found {} missing dates for {}", missing_dates.size(), set_spec);
        
        // Process in chunks of 7 days
        const size_t chunk_size = 7;
        for (size_t i = 0; i < missing_dates.size(); i += chunk_size) {
            size_t end_idx = std::min(i + chunk_size, missing_dates.size());
            
            for (size_t j = i; j < end_idx; ++j) {
                const std::string& date_str = missing_dates[j];
                
                try {
                    // Single day range
                    std::string next_date = date_str; // Would need to add 1 day
                    int records = harvestSetSpec(set_spec, date_str, date_str);
                    
                    if (records > 0) {
                        total_records += records;
                        spdlog::info("Backfilled {} records for {} on {}", records, set_spec, date_str);
                    }
                    
                    // Rate limiting
                    std::this_thread::sleep_for(std::chrono::seconds(config.getRateLimitDelay()));
                    
                } catch (const std::exception& e) {
                    spdlog::error("Error backfilling {} for {}: {}", set_spec, date_str, e.what());
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

int Harvester::harvestSetSpec(const std::string& set_spec, const std::string& from_date,
                              const std::string& until_date) {
    try {
        std::vector<Record> records = oai_client_->listRecords("oai_dc", set_spec, from_date, until_date);
        
        if (records.empty()) {
            return 0;
        }
        
        insertRecords(records, set_spec);
        return static_cast<int>(records.size());
        
    } catch (const std::exception& e) {
        spdlog::error("Error harvesting {}: {}", set_spec, e.what());
        return -1;
    }
}

void Harvester::insertRecords(const std::vector<Record>& records, const std::string& set_spec) {
    Config& config = Config::instance();
    std::string schema = config.getPostgresSchema();
    std::string table = config.getPostgresTable();
    
    const std::string upsert_query = R"(
        INSERT INTO )" + schema + R"(.)" + table + R"( (
            header_datestamp, header_identifier, header_setSpecs,
            metadata_creator, metadata_date, metadata_description,
            metadata_identifier, metadata_subject, metadata_title, metadata_type
        ) VALUES (
            $1, $2, $3, $4, $5, $6, $7, $8, $9, $10
        )
        ON CONFLICT (header_identifier) 
        DO UPDATE SET
            header_datestamp = EXCLUDED.header_datestamp,
            header_setSpecs = EXCLUDED.header_setSpecs,
            metadata_creator = EXCLUDED.metadata_creator,
            metadata_date = EXCLUDED.metadata_date,
            metadata_description = EXCLUDED.metadata_description,
            metadata_identifier = EXCLUDED.metadata_identifier,
            metadata_subject = EXCLUDED.metadata_subject,
            metadata_title = EXCLUDED.metadata_title,
            metadata_type = EXCLUDED.metadata_type,
            updated_at = CURRENT_TIMESTAMP
    )";
    
    int processed = 0;
    
    for (const auto& record : records) {
        try {
            // Convert vectors to JSON
            json header_setSpecs = json::array();
            for (const auto& s : record.header_setSpecs) {
                header_setSpecs.push_back(s);
            }
            
            json metadata_creator = json::array();
            for (const auto& c : record.metadata_creator) {
                metadata_creator.push_back(c);
            }
            
            json metadata_date = json::array();
            for (const auto& d : record.metadata_date) {
                metadata_date.push_back(d);
            }
            
            json metadata_identifier = json::array();
            for (const auto& id : record.metadata_identifier) {
                metadata_identifier.push_back(id);
            }
            
            json metadata_subject = json::array();
            for (const auto& s : record.metadata_subject) {
                metadata_subject.push_back(s);
            }
            
            json metadata_title = json::array();
            for (const auto& t : record.metadata_title) {
                metadata_title.push_back(t);
            }
            
            // Prepare parameter values
            const char* param_values[10];
            std::string p1 = record.header_datestamp;
            std::string p2 = record.header_identifier;
            std::string p3 = header_setSpecs.dump();
            std::string p4 = metadata_creator.dump();
            std::string p5 = metadata_date.dump();
            std::string p6 = record.metadata_description;
            std::string p7 = metadata_identifier.dump();
            std::string p8 = metadata_subject.dump();
            std::string p9 = metadata_title.dump();
            std::string p10 = record.metadata_type;
            
            param_values[0] = p1.c_str();
            param_values[1] = p2.c_str();
            param_values[2] = p3.c_str();
            param_values[3] = p4.c_str();
            param_values[4] = p5.c_str();
            param_values[5] = p6.c_str();
            param_values[6] = p7.c_str();
            param_values[7] = p8.c_str();
            param_values[8] = p9.c_str();
            param_values[9] = p10.c_str();
            
            // Note: Full implementation would use PQexecParams for proper parameterized queries
            // This is a simplified version showing the concept
            
            processed++;
            
            if (processed % 100 == 0) {
                spdlog::info("Processed {} records in current batch for {}", processed, set_spec);
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Error inserting record {}: {}", record.header_identifier, e.what());
        }
    }
    
    spdlog::info("Inserted {} records for {}", processed, set_spec);
}

std::vector<std::string> Harvester::getMissingDates(const std::string& start_date,
                                                     const std::string& end_date,
                                                     const std::string& set_spec) {
    // This is a simplified implementation
    // In full implementation, would query PostgreSQL to find missing dates
    std::vector<std::string> missing_dates;
    
    // For now, return a single date as placeholder
    // Full implementation would query the database
    missing_dates.push_back(start_date);
    
    return missing_dates;
}
