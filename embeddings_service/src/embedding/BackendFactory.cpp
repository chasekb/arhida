#include "embedding/BackendFactory.h"

#include "embedding/MlxBackend.h"
#include "embedding/OnnxCpuBackend.h"
#include "embedding/OnnxCudaBackend.h"

#include <spdlog/spdlog.h>
#include <stdexcept>

std::unique_ptr<EmbeddingBackend>
createEmbeddingBackend(const EmbeddingServiceConfig& config) {
  auto fallbackToCpu = [&config](const std::string& reason)
      -> std::unique_ptr<EmbeddingBackend> {
    if (!config.shouldFallbackToCpu()) {
      throw std::runtime_error(reason);
    }
    spdlog::warn("{}; falling back to CPU backend", reason);
    return std::make_unique<OnnxCpuBackend>(config);
  };

  if (config.device == "mlx" || config.accelerator_backend == "mlx") {
    if (config.device != "mlx" || config.accelerator_backend != "mlx") {
      return fallbackToCpu(
          "MLX backend requires DEVICE=mlx and ACCELERATOR_BACKEND=mlx");
    }
    return std::make_unique<MlxBackend>(config.model_dimension);
  }

  if (config.device == "cuda") {
    if (config.accelerator_backend != "onnx") {
      return fallbackToCpu(
          "CUDA mode requires ACCELERATOR_BACKEND=onnx");
    }
    if (config.ort_execution_provider != "CUDA") {
      return fallbackToCpu(
          "CUDA mode requires ORT_EXECUTION_PROVIDER=CUDA");
    }
    return std::make_unique<OnnxCudaBackend>(config);
  }

  if (config.device == "cpu") {
    if (config.accelerator_backend != "onnx") {
      throw std::runtime_error(
          "CPU mode requires ACCELERATOR_BACKEND=onnx");
    }
    return std::make_unique<OnnxCpuBackend>(config);
  }

  return fallbackToCpu("Unsupported embedding backend/device combination: " +
                       config.accelerator_backend + "/" + config.device);
}
