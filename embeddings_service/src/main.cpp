#include "config/EmbeddingServiceConfig.h"
#include "server/HttpServer.h"

#include <spdlog/spdlog.h>

int main() {
  auto config = EmbeddingServiceConfig::fromEnvironment();
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

  HttpServer server(config);
  server.run();
  return 0;
}
