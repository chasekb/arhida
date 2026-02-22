/**
 * @file Database.cpp
 * @brief PostgreSQL database connection and operations implementation
 * @author Bernard Chase
 */

#include "Database.h"
#include "../config/Config.h"
#include "../utils/Logger.h"
#include <cstring>
#include <sstream>
#include <fstream>

Database::Database() : conn_(nullptr), connected_(false) {}

Database::~Database() {
    disconnect();
}

void Database::connect() {
    Config& config = Config::instance();
    
    std::string host, user, password;
    int port;
    
    // Check if running in Docker environment
    std::ifstream user_file(config.getDockerPostgresUserFile());
    std::ifstream pass_file(config.getDockerPostgresPasswordFile());
    
    if (user_file.is_open() && pass_file.is_open()) {
        std::getline(user_file, user);
        std::getline(pass_file, password);
        user_file.close();
        pass_file.close();
        host = config.getDockerPostgresHost();
    } else {
        // Local development
        host = config.getPostgresHost();
        user = config.getPostgresUser();
        password = config.getPostgresPassword();
    }
    
    port = config.getPostgresPort();
    std::string database = config.getPostgresDatabase();
    
    // Build connection string
    std::stringstream conninfo;
    conninfo << "host=" << host << " ";
    conninfo << "dbname=" << database << " ";
    conninfo << "user=" << user << " ";
    conninfo << "password=" << password << " ";
    conninfo << "port=" << port;
    
    conn_ = PQconnectdb(conninfo.str().c_str());
    
    if (PQstatus(conn_) != CONNECTION_OK) {
        spdlog::error("Failed to connect to PostgreSQL: {}", PQerrorMessage(conn_));
        throw std::runtime_error("Database connection failed");
    }
    
    connected_ = true;
    spdlog::info("Connected to PostgreSQL database: {}", database);
}

void Database::disconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
        connected_ = false;
    }
}

bool Database::isConnected() const {
    return connected_ && conn_ && PQstatus(conn_) == CONNECTION_OK;
}

void Database::createSchema(const std::string& schema_name) {
    std::string query = "CREATE SCHEMA IF NOT EXISTS " + schema_name;
    execute(query);
    spdlog::info("Created schema: {}", schema_name);
}

void Database::createTable(const std::string& schema_name, const std::string& table_name) {
    std::stringstream query;
    query << "CREATE TABLE IF NOT EXISTS " << schema_name << "." << table_name << " ("
          << "id SERIAL PRIMARY KEY, "
          << "header_datestamp TIMESTAMP, "
          << "header_identifier VARCHAR(255) UNIQUE NOT NULL, "
          << "header_setSpecs JSONB, "
          << "metadata_creator JSONB, "
          << "metadata_date JSONB, "
          << "metadata_description TEXT, "
          << "metadata_identifier JSONB, "
          << "metadata_subject JSONB, "
          << "metadata_title JSONB, "
          << "metadata_type VARCHAR(100), "
          << "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
          << "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
          << ")";
    
    execute(query.str());
    spdlog::info("Created table: {}.{}", schema_name, table_name);
}

void Database::createIndexes(const std::string& schema_name, const std::string& table_name) {
    std::vector<std::string> indexes = {
        "CREATE UNIQUE INDEX IF NOT EXISTS " + table_name + "_header_identifier_idx ON " + schema_name + "." + table_name + " (header_identifier)",
        "CREATE INDEX IF NOT EXISTS " + table_name + "_header_datestamp_idx ON " + schema_name + "." + table_name + " (header_datestamp)",
        "CREATE INDEX IF NOT EXISTS " + table_name + "_header_setspecs_idx ON " + schema_name + "." + table_name + " USING GIN (header_setSpecs)",
        "CREATE INDEX IF NOT EXISTS " + table_name + "_header_datestamp_setspecs_idx ON " + schema_name + "." + table_name + " (header_datestamp, header_setSpecs)",
        "CREATE INDEX IF NOT EXISTS " + table_name + "_metadata_subject_idx ON " + schema_name + "." + table_name + " USING GIN (metadata_subject)",
        "CREATE INDEX IF NOT EXISTS " + table_name + "_created_at_idx ON " + schema_name + "." + table_name + " (created_at)",
        "CREATE INDEX IF NOT EXISTS " + table_name + "_updated_at_idx ON " + schema_name + "." + table_name + " (updated_at)"
    };
    
    for (const auto& idx : indexes) {
        execute(idx);
    }
    spdlog::info("Created indexes for table: {}.{}", schema_name, table_name);
}

void Database::execute(const std::string& query) {
    PGresult* res = PQexec(conn_, query.c_str());
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Query failed: {}", PQerrorMessage(conn_));
        spdlog::error("Query: {}", query);
        PQclear(res);
        throw std::runtime_error("Query execution failed");
    }
    
    PQclear(res);
}

PGresult* Database::query(const std::string& query) {
    PGresult* res = PQexec(conn_, query.c_str());
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("Query failed: {}", PQerrorMessage(conn_));
        spdlog::error("Query: {}", query);
        PQclear(res);
        throw std::runtime_error("Query execution failed");
    }
    
    return res;
}
