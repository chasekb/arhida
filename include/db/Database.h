/**
 * @file Database.h
 * @brief PostgreSQL database connection and operations
 * @author Bernard Chase
 */

#pragma once

#include "db/StorageEngine.h"
#include <cstddef>
#include <memory>
#include <libpq-fe.h>
#include <string>
#include <utility>

class Database : public StorageEngine {
public:
    Database();
    ~Database() override;
    
    void connect() override;
    void disconnect() override;
    bool isConnected() const override;
    void initialize() override;
    
    PGconn* getConnection() { return conn_; }
    
    // Schema and table operations
    void createSchema(const std::string& schema_name) override;
    void createTable(const std::string& schema_name, const std::string& table_name) override;
    void createIndexes(const std::string& schema_name, const std::string& table_name) override;
    void validateStorageConfiguration() const override;
    void upsertRecord(const Record& record, const std::vector<float>& embedding) override;
    std::vector<std::string> getMissingDates(const std::string& start_date,
                                             const std::string& end_date,
                                             const std::string& set_spec) override;
    std::vector<Record> fetchRecordsChunk(std::size_t limit, std::size_t offset) const;
    std::vector<std::pair<std::size_t, Record>>
    fetchRecordsChunkAfterId(std::size_t limit, std::size_t last_row_id) const;
    std::size_t rowIdForOffset(std::size_t offset) const;
    std::size_t countRecords() const;
    std::size_t countRecordsForDate(const std::string& date) const;
    std::size_t countRecordsForSetSpec(const std::string& set_spec) const;
    bool identifierExists(const std::string& identifier) const;
    
    // Query operations
    void execute(const std::string& query);
    PGresult* query(const std::string& query);
    
private:
    PGconn* conn_;
    bool connected_;
};
