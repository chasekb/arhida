/**
 * @file QueryBuilder.cpp
 * @brief SQL query builder implementation
 * @author Bernard Chase
 */

#include "QueryBuilder.h"

QueryBuilder& QueryBuilder::select(const std::string& table) {
    select_clause_ = "SELECT * FROM " + table;
    return *this;
}

QueryBuilder& QueryBuilder::from(const std::string& table) {
    from_clause_ = table;
    return *this;
}

QueryBuilder& QueryBuilder::where(const std::string& condition) {
    if (where_clause_.empty()) {
        where_clause_ = "WHERE " + condition;
    } else {
        where_clause_ += " AND " + condition;
    }
    return *this;
}

QueryBuilder& QueryBuilder::orderBy(const std::string& column, bool asc) {
    order_clause_ = "ORDER BY " + column + (asc ? " ASC" : " DESC");
    return *this;
}

QueryBuilder& QueryBuilder::limit(int count) {
    limit_clause_ = "LIMIT " + std::to_string(count);
    return *this;
}

std::string QueryBuilder::build() const {
    std::string query = select_clause_;
    if (!from_clause_.empty()) {
        query = "SELECT * FROM " + from_clause_;
    }
    if (!where_clause_.empty()) {
        query += " " + where_clause_;
    }
    if (!order_clause_.empty()) {
        query += " " + order_clause_;
    }
    if (!limit_clause_.empty()) {
        query += " " + limit_clause_;
    }
    return query;
}
