#include "embedding/EmbeddingTextBuilder.h"
#include "oai/Record.h"

#include <iostream>
#include <string>

namespace {

void expectEqual(const std::string &actual, const std::string &expected,
                 const std::string &scenario) {
  if (actual != expected) {
    std::cerr << "[FAIL] " << scenario << "\nExpected:\n"
              << expected << "\nActual:\n"
              << actual << std::endl;
    std::exit(1);
  }
  std::cout << "[PASS] " << scenario << std::endl;
}

Record makeBaseRecord() {
  Record record;
  record.header_identifier = "oai:arxiv.org:1234.5678";
  record.header_datestamp = "2026-01-01";
  record.header_setSpecs = {"cs"};
  record.metadata_creator = {"Author One"};
  record.metadata_date = {"2026-01-01"};
  record.metadata_identifier = {"http://arxiv.org/abs/1234.5678"};
  record.metadata_type = "text";
  return record;
}

void testCanonicalLayoutAndWhitespaceNormalization() {
  Record record = makeBaseRecord();
  record.metadata_title = {"  A   Title ", " With   Extra  Spaces   "};
  record.metadata_subject = {" AI  ", "  ML   Systems "};
  record.metadata_description = "  First   line\n\nSecond\tline   ";

  const std::string actual = EmbeddingTextBuilder::build(record);
  const std::string expected =
      "Title: A Title With Extra Spaces\n"
      "Subjects: AI; ML Systems\n"
      "Description: First line Second line";

  expectEqual(actual, expected,
              "canonical layout and whitespace normalization");
}

void testMissingFieldFallbacks() {
  Record record = makeBaseRecord();

  const std::string actual = EmbeddingTextBuilder::build(record);
  const std::string expected =
      "Title: <missing>\n"
      "Subjects: <missing>\n"
      "Description: <missing>";

  expectEqual(actual, expected, "missing field fallback placeholders");
}

} // namespace

int main() {
  testCanonicalLayoutAndWhitespaceNormalization();
  testMissingFieldFallbacks();
  std::cout << "EmbeddingTextBuilder tests passed" << std::endl;
  return 0;
}
