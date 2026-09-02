#include "config/Config.h"
#include "db/QdrantStorage.h"
#include <arpa/inet.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;

namespace {

void expect(bool condition, const std::string &scenario) {
  if (!condition) {
    throw std::runtime_error("[FAIL] " + scenario);
  }
  std::cout << "[PASS] " << scenario << '\n';
}

class FakeQdrantServer {
public:
  FakeQdrantServer() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      throw std::runtime_error("failed to create fake Qdrant socket");
    }

    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0 ||
        listen(listen_fd_, 2) < 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      throw std::runtime_error("failed to bind fake Qdrant socket");
    }

    socklen_t address_size = sizeof(address);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&address),
                    &address_size) < 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      throw std::runtime_error("failed to inspect fake Qdrant socket");
    }
    port_ = ntohs(address.sin_port);
    worker_ = std::thread(&FakeQdrantServer::serve, this);
  }

  ~FakeQdrantServer() {
    if (listen_fd_ >= 0) {
      shutdown(listen_fd_, SHUT_RDWR);
      close(listen_fd_);
      listen_fd_ = -1;
    }
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  std::string url() const {
    return "http://127.0.0.1:" + std::to_string(port_);
  }

  const std::vector<std::string> &requestBodies() const { return request_bodies_; }

  void wait() {
    if (worker_.joinable()) {
      worker_.join();
    }
    if (!server_error_.empty()) {
      throw std::runtime_error(server_error_);
    }
  }

private:
  static std::string readRequestBody(int client_fd) {
    std::string request;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    while ((header_end = request.find("\r\n\r\n")) == std::string::npos) {
      const ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
      if (received <= 0) {
        throw std::runtime_error("fake Qdrant client closed before headers");
      }
      request.append(buffer, static_cast<std::size_t>(received));
    }

    const auto length_marker = request.find("Content-Length:");
    if (length_marker == std::string::npos) {
      throw std::runtime_error("fake Qdrant request omitted Content-Length");
    }
    const auto length_start = length_marker + std::strlen("Content-Length:");
    const auto length_end = request.find('\r', length_start);
    const auto content_length = static_cast<std::size_t>(std::stoul(
        request.substr(length_start, length_end - length_start)));
    const auto body_start = header_end + 4;
    while (request.size() - body_start < content_length) {
      const ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
      if (received <= 0) {
        throw std::runtime_error("fake Qdrant client closed before body");
      }
      request.append(buffer, static_cast<std::size_t>(received));
    }
    return request.substr(body_start, content_length);
  }

  static void sendResponse(int client_fd, const json &response) {
    const std::string body = response.dump();
    const std::string headers =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    const std::string message = headers + body;
    std::size_t sent = 0;
    while (sent < message.size()) {
      const ssize_t written = send(client_fd, message.data() + sent,
                                   message.size() - sent, 0);
      if (written <= 0) {
        throw std::runtime_error("failed to write fake Qdrant response");
      }
      sent += static_cast<std::size_t>(written);
    }
  }

  void serve() {
    try {
      for (int page = 0; page < 2; ++page) {
        const int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
          throw std::runtime_error("fake Qdrant accept failed");
        }
        request_bodies_.push_back(readRequestBody(client_fd));
        const json response = {
            {"result",
             {{"points",
               json::array({{{"id", page + 1},
                             {"payload",
                              {{"header_datestamp",
                                page == 0 ? "2026-08-31" : "2026-09-02"}}}}})},
              {"next_page_offset", page == 0 ? json(17) : json(nullptr)}}},
            {"status", "ok"}};
        sendResponse(client_fd, response);
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
      }
    } catch (const std::exception &error) {
      server_error_ = error.what();
    }
  }

  int listen_fd_ = -1;
  unsigned short port_ = 0;
  std::thread worker_;
  std::vector<std::string> request_bodies_;
  std::string server_error_;
};

void testDateOnlyMissingDateFilter() {
  FakeQdrantServer server;
  setenv("QDRANT_URL", server.url().c_str(), 1);
  setenv("QDRANT_COLLECTION", "date_only_filter_regression", 1);
  Config::instance().load();

  QdrantStorage storage;
  const auto missing =
      storage.getMissingDates("2026-08-31", "2026-09-02", "cs");
  server.wait();

  expect(missing == std::vector<std::string>{"2026-09-01"},
         "occupied date is excluded and absent date is reported missing");

  const auto &requests = server.requestBodies();
  expect(requests.size() == 2,
         "scroll pagination makes exactly two deterministic requests");
  if (requests.size() != 2) {
    return;
  }

  const auto first = json::parse(requests[0]);
  const auto second = json::parse(requests[1]);
  std::cout << "[EVIDENCE] first_scroll_body=" << first.dump() << '\n';
  std::cout << "[EVIDENCE] second_scroll_body=" << second.dump() << '\n';
  const auto &range = first["filter"]["must"][1]["range"];
  expect(range["gte"] == "2026-08-31" && range["lte"] == "2026-09-02",
         "date filter uses exact date-only inclusive bounds");
  expect(requests[0].find('T') == std::string::npos &&
             requests[1].find('T') == std::string::npos,
         "date-only filter requests contain no timestamp bounds");
  expect(second["offset"] == 17,
         "second scroll request forwards the first next_page_offset");
  expect(second["filter"] == first["filter"],
         "pagination preserves the exact set/date filter");
}

} // namespace

int main() {
  try {
    testDateOnlyMissingDateFilter();
    std::cout << "Qdrant missing-date tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
