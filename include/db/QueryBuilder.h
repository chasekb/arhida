/**
 * @file QueryBuilder.h
 * @brief SQL query builder for PostgreSQL operations
 * @author Bernard Chase
 */

#pragma once

#include <string>
#include <vector>

class QueryBuilder {
public:
    QueryBuilder& select(const std::string& table);
    QueryBuilder& from(const std::string& table);
    QueryBuilder& where(const std::string& condition);
    QueryBuilder& orderBy(const std::string& column, bool asc = true);
    QueryBuilder& limit(int count);
    
    std::string build() const;
    
private:
    std::string select_clause_;
    std::string from_clause_;
    std::string where_clause_;
    std::string order_clause_;
    std::string limit_clause_;
};
