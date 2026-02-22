/**
 * @file Database.h
 * @brief PostgreSQL database connection and operations
 * @author Bernard Chase
 */

#pragma once

#include <string>
#include <memory>
#include <libpq-fe.h>

class Database {
public:
    Database();
    ~Database();
    
    void connect();
    void disconnect();
    bool isConnected() const;
    
    PGconn* getConnection() { return conn_; }
    
    // Schema and table operations
    void createSchema(const std::string& schema_name);
    void createTable(const std::string& schema_name, const std::string& table_name);
    void createIndexes(const std::string& schema_name, const std::string& table_name);
    
    // Query operations
    void execute(const std::string& query);
    PGresult* query(const std::string& query);
    
private:
    PGconn* conn_;
    bool connected_;
};
