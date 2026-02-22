/**
 * @file JsonHelper.cpp
 * @brief JSON serialization utilities implementation
 * @author Bernard Chase
 */

#include "JsonHelper.h"
#include <iostream>

using json = nlohmann::json;

std::string JsonHelper::vectorToJson(const std::vector<std::string>& vec) {
    json j = json::array();
    for (const auto& item : vec) {
        j.push_back(item);
    }
    return j.dump();
}

std::vector<std::string> JsonHelper::jsonToVector(const std::string& json_str) {
    std::vector<std::string> result;
    
    try {
        json j = json::parse(json_str);
        if (j.is_array()) {
            for (const auto& item : j) {
                if (item.is_string()) {
                    result.push_back(item.get<std::string>());
                }
            }
        }
    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
    
    return result;
}

std::string JsonHelper::safeSerialize(const std::string& str) {
    if (str.empty()) {
        return "null";
    }
    
    try {
        // Try to parse as JSON first
        json j = json::parse(str);
        return j.dump();
    } catch (...) {
        // If not valid JSON, return as serialized string
        json j = str;
        return j.dump();
    }
}

std::string JsonHelper::safeSerialize(const std::vector<std::string>& vec) {
    return vectorToJson(vec);
}
