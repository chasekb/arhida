#include "embedding/EmbeddingClient.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class MockEmbeddingClient : public EmbeddingClient {
public:
  using PlannedResponse = std::pair<long, std::string>;

  MockEmbeddingClient(int max_batch_size,
                      int retry_count,
                      int expected_vector_size,
                      std::vector<PlannedResponse> responses)
      : EmbeddingClient("http://mock", 100, max_batch_size, retry_count,
                        expected_vector_size),
        responses_(std::move(responses)) {}

  int requestCount() const { return request_count_; }

protected:
  std::string performRequest(const std::string &method, const std::string &path,
                             const std::string &body,
                             long *response_code) const override {
    (void)method;
    (void)path;
    (void)body;

    if (responses_.empty()) {
      throw std::runtime_error("No mock responses configured");
    }

    std::size_t index = static_cast<std::size_t>(request_count_);
    if (index >= responses_.size()) {
      index = responses_.size() - 1;
    }

    request_count_++;
    if (response_code) {
      *response_code = responses_[index].first;
    }
    return responses_[index].second;
  }

private:
  mutable int request_count_ = 0;
  std::vector<PlannedResponse> responses_;
};

void expectTrue(bool condition, const std::string &scenario) {
  if (!condition) {
    std::cerr << "[FAIL] " << scenario << std::endl;
    std::exit(1);
  }
  std::cout << "[PASS] " << scenario << std::endl;
}

void expectThrows(const std::function<void()> &fn,
                  const std::string &scenario) {
  try {
    fn();
  } catch (...) {
    std::cout << "[PASS] " << scenario << std::endl;
    return;
  }

  std::cerr << "[FAIL] " << scenario << " (expected exception)" << std::endl;
  std::exit(1);
}

void testEmbedSuccess() {
  MockEmbeddingClient client(
      4, 3, 3,
      {{200, R"({"vectors":[[0.1,0.2,0.3],[0.4,0.5,0.6]]})"}});

  auto vectors = client.embed({"first", "second"});
  expectTrue(vectors.size() == 2, "embed returns one vector per input");
  expectTrue(vectors[0].size() == 3, "embed enforces expected vector size");
  expectTrue(client.requestCount() == 1,
             "embed success performs single request");
}

void testBatchSizeLimit() {
  MockEmbeddingClient client(1, 2, 3,
                             {{200, R"({"vectors":[[0.1,0.2,0.3]]})"}});

  expectThrows(
      [&]() {
        (void)client.embed({"one", "two"});
      },
      "embed fails when input batch exceeds configured limit");
}

void testRetryBehavior() {
  MockEmbeddingClient client(
      2, 3, 3,
      {{500, R"({"error":"temporary"})"},
       {200, R"({"vectors":[[0.9,0.8,0.7]]})"}});

  auto vectors = client.embed({"retry"});
  expectTrue(vectors.size() == 1, "embed succeeds after retryable failure");
  expectTrue(client.requestCount() == 2,
             "embed retries request after non-2xx response");
}

void testInvalidDimension() {
  MockEmbeddingClient client(
      2, 2, 3,
      {{200, R"({"vectors":[[0.1,0.2]]})"}});

  expectThrows(
      [&]() {
        (void)client.embed({"bad-dimension"});
      },
      "embed fails when returned vector dimension does not match expected");
}

int main() {
  testEmbedSuccess();
  testBatchSizeLimit();
  testRetryBehavior();
  testInvalidDimension();
  std::cout << "EmbeddingClient tests passed" << std::endl;
  return 0;
}
