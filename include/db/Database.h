/**
 * @file Database.h
 * @brief PostgreSQL database connection and operations
 * @author Bernard Chase
 */

#pragma once

#include "db/StorageEngine.h"
#include <string>
#include <memory>
#include <libpq-fe.h>

class Database : public StorageEngine {
public:
    Database();
    ~Database() override;
    
    void connect() override;
    void disconnect() override;
    bool isConnected() const override;
    
    PGconn* getConnection() { return conn_; }
    
    // Schema and table operations
    void createSchema(const std::string& schema_name) override;
    void createTable(const std::string& schema_name, const std::string& table_name) override;
    void createIndexes(const std::string& schema_name, const std::string& table_name) override;
    void upsertRecord(const Record& record, const std::vector<float>& embedding) override;
    std::vector<std::string> getMissingDates(const std::string& start_date,
                                             const std::string& end_date,
                                             const std::string& set_spec) override;
    
    // Query operations
    void execute(const std::string& query);
    PGresult* query(const std::string& query);
    
private:
    PGconn* conn_;
    bool connected_;
};
