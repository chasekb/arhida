/**
 * @file QdrantStorage.h
 * @brief Qdrant storage scaffold for vector persistence
 * @author Bernard Chase
 */

#pragma once

#include "db/StorageEngine.h"
#include <string>

class QdrantStorage : public StorageEngine {
public:
    QdrantStorage();
    ~QdrantStorage() override = default;

    void connect() override;
    void disconnect() override;
    bool isConnected() const override;

    void createSchema(const std::string& schema_name) override;
    void createTable(const std::string& schema_name, const std::string& table_name) override;
    void createIndexes(const std::string& schema_name, const std::string& table_name) override;

    std::string getCollectionName() const;

private:
    bool connected_;
};