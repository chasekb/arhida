/**
 * @file JsonHelper.h
 * @brief JSON serialization utilities
 * @author Bernard Chase
 */

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class JsonHelper {
public:
    // Convert vector to JSON array string
    static std::string vectorToJson(const std::vector<std::string>& vec);
    
    // Parse JSON string to vector
    static std::vector<std::string> jsonToVector(const std::string& json_str);
    
    // Safe JSON serialization
    static std::string safeSerialize(const std::string& str);
    static std::string safeSerialize(const std::vector<std::string>& vec);
};
