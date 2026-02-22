/**
 * @file OaiClient.cpp
 * @brief OAI-PMH client implementation for harvesting from arXiv
 * @author Bernard Chase
 */

#include "oai/OaiClient.h"
#include "config/Config.h"
#include "utils/Logger.h"
#include <chrono>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <sstream>
#include <thread>

// Global string for CURL callback
static std::string response_buffer;

OaiClient::OaiClient(const std::string &base_url)
    : base_url_(base_url), curl_(nullptr), rate_limit_delay_(3),
      max_retries_(3) {
  curl_ = curl_easy_init();
}

OaiClient::~OaiClient() {
  if (curl_) {
    curl_easy_cleanup(curl_);
  }
}

void OaiClient::setRateLimitDelay(int delay_seconds) {
  rate_limit_delay_ = delay_seconds;
}

void OaiClient::setMaxRetries(int max_retries) { max_retries_ = max_retries; }

size_t OaiClient::writeCallback(void *contents, size_t size, size_t nmemb,
                                void *userp) {
  size_t realsize = size * nmemb;
  response_buffer.append(static_cast<char *>(contents), realsize);
  return realsize;
}

std::string OaiClient::fetchUrl(const std::string &url) {
  response_buffer.clear();

  curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_buffer);
  curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 60L);

  CURLcode res = curl_easy_perform(curl_);

  if (res != CURLE_OK) {
    spdlog::error("CURL error: {}", curl_easy_strerror(res));
    throw std::runtime_error("Failed to fetch URL");
  }

  long response_code;
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response_code);

  if (response_code >= 400) {
    spdlog::error("HTTP error: {}", response_code);
    throw std::runtime_error("HTTP request failed");
  }

  return response_buffer;
}

void OaiClient::rateLimitWait() {
  std::this_thread::sleep_for(std::chrono::seconds(rate_limit_delay_));
}

std::vector<Record> OaiClient::listRecords(const std::string &metadata_prefix,
                                           const std::string &set_spec,
                                           const std::string &from_date,
                                           const std::string &until_date) {

  // Build OAI-PMH request URL
  std::stringstream url;
  url << base_url_ << "?verb=ListRecords";
  url << "&metadataPrefix=" << metadata_prefix;
  if (!set_spec.empty()) {
    url << "&set=" << set_spec;
  }
  if (!from_date.empty()) {
    url << "&from=" << from_date;
  }
  if (!until_date.empty()) {
    url << "&until=" << until_date;
  }

  spdlog::info("Fetching records from: {}", url.str());

  // Wait before request (rate limiting)
  rateLimitWait();

  std::string xml_response;
  int retries = 0;

  while (retries < max_retries_) {
    try {
      xml_response = fetchUrl(url.str());
      break;
    } catch (const std::exception &e) {
      retries++;
      spdlog::warn("Request failed (attempt {}/{}): {}", retries, max_retries_,
                   e.what());
      if (retries < max_retries_) {
        rateLimitWait();
      }
    }
  }

  if (xml_response.empty()) {
    spdlog::warn("No records found for set_spec: {}, from: {}, until: {}",
                 set_spec, from_date, until_date);
    return {};
  }

  return parseXmlResponse(xml_response);
}

std::vector<Record> OaiClient::parseXmlResponse(const std::string &xml) {
  std::vector<Record> records;

  xmlDocPtr doc = xmlReadMemory(xml.c_str(), xml.size(), "noname.xml", NULL, 0);
  if (!doc) {
    spdlog::error("Failed to parse XML response");
    return records;
  }

  xmlNodePtr root = xmlDocGetRootElement(doc);
  if (!root) {
    xmlFreeDoc(doc);
    return records;
  }

  // Find all record nodes
  for (xmlNodePtr node = root->children; node; node = node->next) {
    if (xmlStrcmp(node->name, (const xmlChar *)"record") == 0) {
      Record record;

      // Parse header
      for (xmlNodePtr child = node->children; child; child = child->next) {
        if (xmlStrcmp(child->name, (const xmlChar *)"header") == 0) {
          for (xmlNodePtr header_child = child->children; header_child;
               header_child = header_child->next) {
            if (xmlStrcmp(header_child->name, (const xmlChar *)"identifier") ==
                0) {
              record.header_identifier =
                  (const char *)xmlNodeGetContent(header_child);
            } else if (xmlStrcmp(header_child->name,
                                 (const xmlChar *)"datestamp") == 0) {
              record.header_datestamp =
                  (const char *)xmlNodeGetContent(header_child);
            } else if (xmlStrcmp(header_child->name,
                                 (const xmlChar *)"setSpec") == 0) {
              record.header_setSpecs.push_back(
                  (const char *)xmlNodeGetContent(header_child));
            }
          }
        }
        // Parse metadata (Dublin Core)
        else if (xmlStrcmp(child->name, (const xmlChar *)"metadata") == 0) {
          for (xmlNodePtr dc = child->children; dc; dc = dc->next) {
            // Dublin Core elements
            for (xmlNodePtr dc_child = dc->children; dc_child;
                 dc_child = dc_child->next) {
              std::string name = (const char *)dc_child->name;
              std::string content = (const char *)xmlNodeGetContent(dc_child);

              if (name == "creator") {
                record.metadata_creator.push_back(content);
              } else if (name == "date") {
                record.metadata_date.push_back(content);
              } else if (name == "description") {
                record.metadata_description = content;
              } else if (name == "identifier") {
                record.metadata_identifier.push_back(content);
              } else if (name == "subject") {
                record.metadata_subject.push_back(content);
              } else if (name == "title") {
                record.metadata_title.push_back(content);
              } else if (name == "type") {
                record.metadata_type = content;
              }
            }
          }
        }
      }

      if (!record.header_identifier.empty()) {
        records.push_back(record);
      }
    }
  }

  xmlFreeDoc(doc);
  spdlog::info("Parsed {} records from XML", records.size());

  return records;
}
