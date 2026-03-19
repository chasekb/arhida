/**
 * @file EmbeddingTextBuilder.cpp
 * @brief Canonical embedding text construction for arXiv records
 * @author Bernard Chase
 */

#include "embedding/EmbeddingTextBuilder.h"
#include <cctype>
#include <sstream>

std::string EmbeddingTextBuilder::build(const Record &record) {
  std::ostringstream builder;

  std::string title = normalizeWhitespace(joinValues(record.metadata_title, " "));
  std::string subjects =
      normalizeWhitespace(joinValues(record.metadata_subject, "; "));
  std::string description = normalizeWhitespace(record.metadata_description);

  builder << "Title: " << (title.empty() ? "<missing>" : title) << "\n";
  builder << "Subjects: "
          << (subjects.empty() ? "<missing>" : subjects) << "\n";
  builder << "Description: "
          << (description.empty() ? "<missing>" : description);

  return builder.str();
}

std::string EmbeddingTextBuilder::normalizeWhitespace(const std::string &input) {
  std::ostringstream normalized;
  bool in_whitespace = false;

  for (unsigned char ch : input) {
    if (std::isspace(ch)) {
      if (!in_whitespace) {
        normalized << ' ';
        in_whitespace = true;
      }
    } else {
      normalized << static_cast<char>(ch);
      in_whitespace = false;
    }
  }

  std::string result = normalized.str();
  if (!result.empty() && result.front() == ' ') {
    result.erase(result.begin());
  }
  if (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }
  return result;
}

std::string EmbeddingTextBuilder::joinValues(const std::vector<std::string> &values,
                                             const std::string &delimiter) {
  std::ostringstream joined;
  for (size_t i = 0; i < values.size(); ++i) {
    joined << values[i];
    if (i + 1 < values.size()) {
      joined << delimiter;
    }
  }
  return joined.str();
}