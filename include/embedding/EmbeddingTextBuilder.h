/**
 * @file EmbeddingTextBuilder.h
 * @brief Canonical embedding text construction for arXiv records
 * @author Bernard Chase
 */

#pragma once

#include "oai/Record.h"
#include <string>

class EmbeddingTextBuilder {
public:
    static std::string build(const Record& record);

private:
    static std::string normalizeWhitespace(const std::string& input);
    static std::string joinValues(const std::vector<std::string>& values,
                                  const std::string& delimiter);
};