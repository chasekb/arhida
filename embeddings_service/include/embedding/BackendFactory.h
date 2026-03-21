#pragma once

#include "config/EmbeddingServiceConfig.h"
#include "embedding/EmbeddingBackend.h"

#include <memory>

std::unique_ptr<EmbeddingBackend>
createEmbeddingBackend(const EmbeddingServiceConfig& config);
