/**
 * @file QdrantStorage.h
 * @brief Qdrant storage scaffold for vector persistence
 * @author Bernard Chase
 */

#pragma once

#include "db/StorageEngine.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class QdrantStorage : public StorageEngine {
public:
    QdrantStorage();
    ~QdrantStorage() override = default;

    void connect() override;
    void disconnect() override;
    bool isConnected() const override;
    void initialize() override;

    void createSchema(const std::string& schema_name) override;
    void createTable(const std::string& schema_name, const std::string& table_name) override;
    void createIndexes(const std::string& schema_name, const std::string& table_name) override;
    void validateStorageConfiguration() const override;
    void upsertRecord(const Record& record, const std::vector<float>& embedding) override;
    void upsertRecordsBatch(const std::vector<Record>& records,
                            const std::vector<std::vector<float>>& embeddings);
    std::size_t countPoints() const;
    std::size_t countPointsForDate(const std::string& date) const;
    std::size_t countPointsForSetSpec(const std::string& set_spec) const;
    bool identifierExists(const std::string& identifier) const;
    std::vector<std::string> getMissingDates(const std::string& start_date,
                                             const std::string& end_date,
                                             const std::string& set_spec) override;

    std::string getCollectionName() const;

private:
    std::string performRequest(const std::string& method,
                               const std::string& path,
                               const std::string& body = std::string(),
                               long* response_code = nullptr) const;
    bool collectionExists() const;
    void ensureCollection();
    void validateCollectionConfiguration() const;
    std::uint64_t makePointId(const std::string& identifier) const;

    bool connected_;
    std::string base_url_;
    std::string collection_name_;
};