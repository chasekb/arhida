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
#include <optional>
#include <set>
#include <sstream>
#include <utility>

using json = nlohmann::json;

namespace {

std::vector<std::string> parseJsonArrayText(const char *text) {
  if (!text) {
    return {};
  }

  auto payload = json::parse(text, nullptr, false);
  if (payload.is_discarded() || !payload.is_array()) {
    return {};
  }

  std::vector<std::string> values;
  values.reserve(payload.size());
  for (const auto &item : payload) {
    if (item.is_string()) {
      values.push_back(item.get<std::string>());
    }
  }

  return values;
}

std::optional<std::size_t> parseCount(PGresult *res) {
  if (!res || PQntuples(res) != 1 || PQnfields(res) != 1 ||
      PQgetisnull(res, 0, 0)) {
    return std::nullopt;
  }

  try {
    return static_cast<std::size_t>(
        std::stoull(std::string(PQgetvalue(res, 0, 0))));
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace

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

void Database::initialize() {
  Config &config = Config::instance();
  std::string schema = config.getPostgresSchema();
  std::string table = config.getPostgresTable();

  createSchema(schema);
  createTable(schema, table);
  createIndexes(schema, table);
  validateStorageConfiguration();
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

void Database::validateStorageConfiguration() const {
  if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
    throw std::runtime_error(
        "PostgreSQL storage validation failed: connection is not available");
  }

  Config &config = Config::instance();
  std::string schema = config.getPostgresSchema();
  std::string table = config.getPostgresTable();

  const std::string validation_query =
      "SELECT to_regclass('" + schema + "." + table + "')";

  PGresult *res = PQexec(conn_, validation_query.c_str());
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    std::string error = PQerrorMessage(conn_);
    PQclear(res);
    throw std::runtime_error("PostgreSQL storage validation query failed: " +
                             error);
  }

  const bool relation_found =
      PQntuples(res) == 1 && !PQgetisnull(res, 0, 0) &&
      std::strlen(PQgetvalue(res, 0, 0)) > 0;
  PQclear(res);

  if (!relation_found) {
    throw std::runtime_error("PostgreSQL storage validation failed: expected " +
                             schema + "." + table + " to exist");
  }

  spdlog::info("Validated PostgreSQL storage configuration for {}.{}", schema,
               table);
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

std::vector<Record> Database::fetchRecordsChunk(std::size_t limit,
                                                std::size_t offset) const {
  if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
    throw std::runtime_error(
        "Cannot fetch PostgreSQL chunk: connection is not available");
  }

  if (limit == 0) {
    return {};
  }

  Config &config = Config::instance();
  const std::string schema = config.getPostgresSchema();
  const std::string table = config.getPostgresTable();

  const std::string query = R"(
    SELECT
      header_identifier,
      COALESCE(header_datestamp::text, ''),
      COALESCE(header_setSpecs::text, '[]'),
      COALESCE(metadata_creator::text, '[]'),
      COALESCE(metadata_date::text, '[]'),
      COALESCE(metadata_description, ''),
      COALESCE(metadata_identifier::text, '[]'),
      COALESCE(metadata_subject::text, '[]'),
      COALESCE(metadata_title::text, '[]'),
      COALESCE(metadata_type, '')
    FROM )" +
                            schema + "." + table + R"(
    ORDER BY id ASC
    LIMIT $1::bigint
    OFFSET $2::bigint
  )";

  const std::string limit_text = std::to_string(limit);
  const std::string offset_text = std::to_string(offset);
  const char *param_values[2] = {limit_text.c_str(), offset_text.c_str()};

  PGresult *res =
      PQexecParams(conn_, query.c_str(), 2, nullptr, param_values, nullptr,
                   nullptr, 0);
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    const std::string error = PQerrorMessage(conn_);
    PQclear(res);
    throw std::runtime_error("Failed to fetch PostgreSQL records chunk: " +
                             error);
  }

  const int rows = PQntuples(res);
  std::vector<Record> records;
  records.reserve(static_cast<std::size_t>(rows));

  for (int row = 0; row < rows; ++row) {
    Record record;
    record.header_identifier = PQgetvalue(res, row, 0);
    record.header_datestamp = PQgetvalue(res, row, 1);
    record.header_setSpecs = parseJsonArrayText(PQgetvalue(res, row, 2));
    record.metadata_creator = parseJsonArrayText(PQgetvalue(res, row, 3));
    record.metadata_date = parseJsonArrayText(PQgetvalue(res, row, 4));
    record.metadata_description = PQgetvalue(res, row, 5);
    record.metadata_identifier = parseJsonArrayText(PQgetvalue(res, row, 6));
    record.metadata_subject = parseJsonArrayText(PQgetvalue(res, row, 7));
    record.metadata_title = parseJsonArrayText(PQgetvalue(res, row, 8));
    record.metadata_type = PQgetvalue(res, row, 9);
    records.push_back(std::move(record));
  }

  PQclear(res);
  return records;
}

std::size_t Database::countRecords() const {
  if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
    throw std::runtime_error(
        "Cannot count PostgreSQL records: connection is not available");
  }

  Config &config = Config::instance();
  const std::string schema = config.getPostgresSchema();
  const std::string table = config.getPostgresTable();

  const std::string query =
      "SELECT COUNT(*)::bigint FROM " + schema + "." + table;

  PGresult *res = PQexec(conn_, query.c_str());
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    const std::string error = PQerrorMessage(conn_);
    PQclear(res);
    throw std::runtime_error("Failed to count PostgreSQL records: " + error);
  }

  const auto parsed = parseCount(res);
  PQclear(res);
  if (!parsed.has_value()) {
    throw std::runtime_error("Failed to parse PostgreSQL record count result");
  }

  return parsed.value();
}

std::size_t Database::countRecordsForDate(const std::string &date) const {
  if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
    throw std::runtime_error(
        "Cannot count PostgreSQL records by date: connection is not available");
  }

  Config &config = Config::instance();
  const std::string schema = config.getPostgresSchema();
  const std::string table = config.getPostgresTable();

  const std::string query =
      "SELECT COUNT(*)::bigint FROM " + schema + "." + table +
      " WHERE DATE(header_datestamp) = $1::date";

  const char *param_values[1] = {date.c_str()};
  PGresult *res = PQexecParams(conn_, query.c_str(), 1, nullptr, param_values,
                               nullptr, nullptr, 0);
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    const std::string error = PQerrorMessage(conn_);
    PQclear(res);
    throw std::runtime_error("Failed to count PostgreSQL records for date " +
                             date + ": " + error);
  }

  const auto parsed = parseCount(res);
  PQclear(res);
  if (!parsed.has_value()) {
    throw std::runtime_error(
        "Failed to parse PostgreSQL date count result for " + date);
  }

  return parsed.value();
}

std::size_t Database::countRecordsForSetSpec(const std::string &set_spec) const {
  if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
    throw std::runtime_error(
        "Cannot count PostgreSQL records by set spec: connection is not available");
  }

  Config &config = Config::instance();
  const std::string schema = config.getPostgresSchema();
  const std::string table = config.getPostgresTable();
  const std::string set_spec_json = json::array({set_spec}).dump();

  const std::string query =
      "SELECT COUNT(*)::bigint FROM " + schema + "." + table +
      " WHERE header_setSpecs @> $1::jsonb";

  const char *param_values[1] = {set_spec_json.c_str()};
  PGresult *res = PQexecParams(conn_, query.c_str(), 1, nullptr, param_values,
                               nullptr, nullptr, 0);
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    const std::string error = PQerrorMessage(conn_);
    PQclear(res);
    throw std::runtime_error(
        "Failed to count PostgreSQL records for set spec " + set_spec +
        ": " + error);
  }

  const auto parsed = parseCount(res);
  PQclear(res);
  if (!parsed.has_value()) {
    throw std::runtime_error(
        "Failed to parse PostgreSQL set-spec count result for " + set_spec);
  }

  return parsed.value();
}

bool Database::identifierExists(const std::string &identifier) const {
  if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
    throw std::runtime_error(
        "Cannot check PostgreSQL identifier: connection is not available");
  }

  Config &config = Config::instance();
  const std::string schema = config.getPostgresSchema();
  const std::string table = config.getPostgresTable();

  const std::string query =
      "SELECT 1 FROM " + schema + "." + table +
      " WHERE header_identifier = $1 LIMIT 1";

  const char *param_values[1] = {identifier.c_str()};
  PGresult *res = PQexecParams(conn_, query.c_str(), 1, nullptr, param_values,
                               nullptr, nullptr, 0);
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    const std::string error = PQerrorMessage(conn_);
    PQclear(res);
    throw std::runtime_error("Failed to query PostgreSQL identifier " +
                             identifier + ": " + error);
  }

  const bool exists = PQntuples(res) > 0;
  PQclear(res);
  return exists;
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
