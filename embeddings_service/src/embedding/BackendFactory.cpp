#include "embedding/BackendFactory.h"

#include "embedding/MlxBackend.h"
#include "embedding/OnnxCpuBackend.h"
#include "embedding/OnnxCudaBackend.h"

#include <stdexcept>

std::unique_ptr<EmbeddingBackend>
createEmbeddingBackend(const EmbeddingServiceConfig& config) {
  if (config.accelerator_backend == "mlx" || config.device == "mlx") {
    return std::make_unique<MlxBackend>(config.model_dimension);
  }

  if (config.device == "cuda") {
    return std::make_unique<OnnxCudaBackend>(config.model_dimension);
  }

  if (config.device == "cpu") {
    return std::make_unique<OnnxCpuBackend>(config.model_dimension);
  }

  throw std::runtime_error("Unsupported embedding backend/device combination: " +
                           config.accelerator_backend + "/" + config.device);
}
