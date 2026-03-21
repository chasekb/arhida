#pragma once

#include "config/EmbeddingServiceConfig.h"

class HttpServer {
public:
  explicit HttpServer(EmbeddingServiceConfig config);
  void run() const;

private:
  EmbeddingServiceConfig config_;
};
