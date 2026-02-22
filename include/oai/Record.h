/**
 * @file Record.h
 * @brief Record model for arXiv metadata
 * @author Bernard Chase
 */

#pragma once

#include <string>
#include <vector>

struct Record {
    // Header fields
    std::string header_identifier;
    std::string header_datestamp;
    std::vector<std::string> header_setSpecs;
    
    // Dublin Core metadata fields
    std::vector<std::string> metadata_creator;
    std::vector<std::string> metadata_date;
    std::string metadata_description;
    std::vector<std::string> metadata_identifier;
    std::vector<std::string> metadata_subject;
    std::vector<std::string> metadata_title;
    std::string metadata_type;
};
