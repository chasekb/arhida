/**
 * @file Database.cpp
 * @brief PostgreSQL database connection and operations implementation
 * @author Bernard Chase
 */

#include "db/Database.h"
#include "config/Config.h"
#include "utils/Logger.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

using json = nlohmann::json;

Database::Database() : conn_(nullptr), connected_(false) {}

Database::~Database() { disconnect(); }

void Database::connect() {
  Config &config = Config::instance();

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

void Database::createSchema(const std::string &schema_name) {
  std::string query = "CREATE SCHEMA IF NOT EXISTS " + schema_name;
  execute(query);
  spdlog::info("Created schema: {}", schema_name);
}

void Database::createTable(const std::string &schema_name,
                           const std::string &table_name) {
  std::stringstream query;
  query << "CREATE TABLE IF NOT EXISTS " << schema_name << "." << table_name
        << " ("
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

void Database::createIndexes(const std::string &schema_name,
                             const std::string &table_name) {
  std::vector<std::string> indexes = {
      "CREATE UNIQUE INDEX IF NOT EXISTS " + table_name +
          "_header_identifier_idx ON " + schema_name + "." + table_name +
          " (header_identifier)",
      "CREATE INDEX IF NOT EXISTS " + table_name + "_header_datestamp_idx ON " +
          schema_name + "." + table_name + " (header_datestamp)",
      "CREATE INDEX IF NOT EXISTS " + table_name + "_header_setspecs_idx ON " +
          schema_name + "." + table_name + " USING GIN (header_setSpecs)",
      "CREATE INDEX IF NOT EXISTS " + table_name +
          "_header_datestamp_setspecs_idx ON " + schema_name + "." +
          table_name + " (header_datestamp, header_setSpecs)",
      "CREATE INDEX IF NOT EXISTS " + table_name + "_metadata_subject_idx ON " +
          schema_name + "." + table_name + " USING GIN (metadata_subject)",
      "CREATE INDEX IF NOT EXISTS " + table_name + "_created_at_idx ON " +
          schema_name + "." + table_name + " (created_at)",
      "CREATE INDEX IF NOT EXISTS " + table_name + "_updated_at_idx ON " +
          schema_name + "." + table_name + " (updated_at)"};

  for (const auto &idx : indexes) {
    execute(idx);
  }
  spdlog::info("Created indexes for table: {}.{}", schema_name, table_name);
}

void Database::upsertRecord(const Record &record,
                            const std::vector<float> &embedding) {
  Config &config = Config::instance();
  std::string schema = config.getPostgresSchema();
  std::string table = config.getPostgresTable();

  json header_setSpecs = json::array();
  for (const auto &s : record.header_setSpecs) {
    header_setSpecs.push_back(s);
  }

  json metadata_creator = json::array();
  for (const auto &c : record.metadata_creator) {
    metadata_creator.push_back(c);
  }

  json metadata_date = json::array();
  for (const auto &d : record.metadata_date) {
    metadata_date.push_back(d);
  }

  json metadata_identifier = json::array();
  for (const auto &id : record.metadata_identifier) {
    metadata_identifier.push_back(id);
  }

  json metadata_subject = json::array();
  for (const auto &s : record.metadata_subject) {
    metadata_subject.push_back(s);
  }

  json metadata_title = json::array();
  for (const auto &t : record.metadata_title) {
    metadata_title.push_back(t);
  }

  const std::string upsert_query = R"(
    INSERT INTO )" +
                                   schema + "." + table + R"( (
      header_datestamp,
      header_identifier,
      header_setSpecs,
      metadata_creator,
      metadata_date,
      metadata_description,
      metadata_identifier,
      metadata_subject,
      metadata_title,
      metadata_type
    ) VALUES (
      $1,
      $2,
      $3::jsonb,
      $4::jsonb,
      $5::jsonb,
      $6,
      $7::jsonb,
      $8::jsonb,
      $9::jsonb,
      $10
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

  const char *param_values[10] = {p1.c_str(), p2.c_str(), p3.c_str(), p4.c_str(),
                                  p5.c_str(), p6.c_str(), p7.c_str(), p8.c_str(),
                                  p9.c_str(), p10.c_str()};

  PGresult *res = PQexecParams(conn_, upsert_query.c_str(), 10, nullptr,
                               param_values, nullptr, nullptr, 0);

  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    std::string error = PQerrorMessage(conn_);
    PQclear(res);
    throw std::runtime_error("PostgreSQL upsert failed for " +
                             record.header_identifier + ": " + error);
  }

  PQclear(res);

  spdlog::debug("Upserted PostgreSQL record {} (embedding_dim={})",
                record.header_identifier, embedding.size());
}

std::vector<std::string>
Database::getMissingDates(const std::string &start_date,
                          const std::string &end_date,
                          const std::string &set_spec) {
  std::set<std::string> existing_dates;

  Config &config = Config::instance();
  std::string schema = config.getPostgresSchema();
  std::string table = config.getPostgresTable();

  const std::string query = R"(
    SELECT DISTINCT DATE(header_datestamp)::text AS existing_date
    FROM )" +
                            schema + "." + table + R"(
    WHERE header_setSpecs @> $1::jsonb
      AND DATE(header_datestamp) BETWEEN $2::date AND $3::date
    ORDER BY existing_date
  )";

  const std::string set_spec_json = json::array({set_spec}).dump();
  const char *param_values[3] = {set_spec_json.c_str(), start_date.c_str(),
                                 end_date.c_str()};

  PGresult *res =
      PQexecParams(conn_, query.c_str(), 3, nullptr, param_values, nullptr,
                   nullptr, 0);

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    std::string error = PQerrorMessage(conn_);
    PQclear(res);
    throw std::runtime_error("Failed to fetch existing dates: " + error);
  }

  int rows = PQntuples(res);
  for (int i = 0; i < rows; ++i) {
    existing_dates.insert(PQgetvalue(res, i, 0));
  }
  PQclear(res);

  std::tm start_tm = {};
  std::tm end_tm = {};
  std::istringstream start_ss(start_date);
  std::istringstream end_ss(end_date);
  start_ss >> std::get_time(&start_tm, "%Y-%m-%d");
  end_ss >> std::get_time(&end_tm, "%Y-%m-%d");

  if (start_ss.fail() || end_ss.fail()) {
    throw std::runtime_error("Invalid date format for missing date lookup");
  }

  std::time_t start_time = std::mktime(&start_tm);
  std::time_t end_time = std::mktime(&end_tm);
  if (end_time < start_time) {
    std::swap(start_time, end_time);
  }

  std::vector<std::string> missing_dates;
  for (std::time_t current = start_time; current <= end_time;
       current += 24 * 3600) {
    std::tm *current_tm = std::localtime(&current);
    char date_str[11];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", current_tm);

    if (existing_dates.find(date_str) == existing_dates.end()) {
      missing_dates.emplace_back(date_str);
    }
  }

  return missing_dates;
}

void Database::execute(const std::string &query) {
  PGresult *res = PQexec(conn_, query.c_str());

  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    spdlog::error("Query failed: {}", PQerrorMessage(conn_));
    spdlog::error("Query: {}", query);
    PQclear(res);
    throw std::runtime_error("Query execution failed");
  }

  PQclear(res);
}

PGresult *Database::query(const std::string &query) {
  PGresult *res = PQexec(conn_, query.c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    spdlog::error("Query failed: {}", PQerrorMessage(conn_));
    spdlog::error("Query: {}", query);
    PQclear(res);
    throw std::runtime_error("Query execution failed");
  }

  return res;
}
