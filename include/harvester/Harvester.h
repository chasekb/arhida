/**
 * @file Harvester.h
 * @brief Main harvester logic for arXiv metadata
 * @author Bernard Chase
 */

#pragma once

#include <string>
#include <vector>
#include "../db/Database.h"
#include "../oai/OaiClient.h"

class Harvester {
public:
    Harvester(Database& db);
    ~Harvester();
    
    // Harvest operations
    int harvestRecent(const std::vector<std::string>& set_specs);
    int harvestBackfill(const std::string& start_date, const std::string& end_date, 
                       const std::vector<std::string>& set_specs);
    
private:
    Database& db_;
    OaiClient* oai_client_;
    
    // Helper methods
    void ensureTableExists();
    int harvestSetSpec(const std::string& set_spec, const std::string& from_date, 
                       const std::string& until_date);
    void insertRecords(const std::vector<Record>& records, const std::string& set_spec);
    std::vector<std::string> getMissingDates(const std::string& start_date, 
                                              const std::string& end_date,
                                              const std::string& set_spec);
};
