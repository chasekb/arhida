/**
 * @file StorageEngine.h
 * @brief Storage abstraction for persistence backends
 * @author Bernard Chase
 */

#pragma once

#include "oai/Record.h"
#include <string>
#include <vector>

class StorageEngine {
public:
    virtual ~StorageEngine() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual void createSchema(const std::string& schema_name) = 0;
    virtual void createTable(const std::string& schema_name, const std::string& table_name) = 0;
    virtual void createIndexes(const std::string& schema_name, const std::string& table_name) = 0;

    virtual void upsertRecord(const Record& record,
                              const std::vector<float>& embedding) = 0;
};