#include "server/HttpServer.h"

#include "embedding/BackendFactory.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>

namespace {
using json = nlohmann::json;

drogon::HttpResponsePtr buildJsonResponse(const json &payload,
                                          drogon::HttpStatusCode status) {
  auto response =
      drogon::HttpResponse::newHttpResponse(drogon::HttpResponse::k200OK);
  response->setStatusCode(status);
  response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
  response->setBody(payload.dump());
  return response;
}

drogon::HttpResponsePtr buildError(drogon::HttpStatusCode status,
                                   const std::string &code,
                                   const std::string &message) {
  json error = {
      {"error", {{"code", code}, {"message", message}}},
  };
  return buildJsonResponse(error, status);
}
} // namespace

HttpServer::HttpServer(EmbeddingServiceConfig config)
    : config_(std::move(config)) {}

void HttpServer::run() const {
  auto backend = createEmbeddingBackend(config_);
  backend->initialize();

  // Startup warmup: fail fast if backend cannot produce expected output shape.
  auto warmup_vectors = backend->embed(BatchInput{"warmup"});
  if (warmup_vectors.size() != 1 ||
      warmup_vectors[0].size() != static_cast<std::size_t>(config_.model_dimension)) {
    throw std::runtime_error(
        "Embedding backend warmup output dimension mismatch");
  }

  auto shared_backend =
      std::shared_ptr<EmbeddingBackend>(std::move(backend));

  drogon::app().registerHandler(
      "/health",
      [cfg = config_, shared_backend](
          const drogon::HttpRequestPtr &,
          std::function<void(const drogon::HttpResponsePtr &)> &&cb) {
        json payload = {
            {"ok", true},
            {"service", "arhida-embeddings-service"},
            {"version", cfg.service_version},
            {"model", cfg.model_name},
            {"dimension", cfg.model_dimension},
            {"max_batch_size", cfg.max_batch_size},
            {"device", cfg.device},
            {"accelerator_backend", cfg.accelerator_backend},
            {"backend", shared_backend->backendName()},
            {"model_loaded", cfg.model_loaded},
            {"tokenizer_loaded", cfg.tokenizer_loaded},
            {"warmup_complete", true},
        };
        cb(buildJsonResponse(payload, drogon::k200OK));
      },
      {drogon::Get});

  drogon::app().registerHandler(
      "/embed",
      [cfg = config_, shared_backend](
          const drogon::HttpRequestPtr &req,
          std::function<void(const drogon::HttpResponsePtr &)> &&cb) {
        const auto started_at = std::chrono::steady_clock::now();
        auto elapsedMs = [&started_at]() {
          return std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - started_at)
              .count();
        };

        try {
          auto body = json::parse(req->getBody());
          if (!body.contains("inputs") || !body["inputs"].is_array()) {
            spdlog::warn("embed request rejected code=invalid_request duration_ms={}",
                         elapsedMs());
            cb(buildError(drogon::k400BadRequest, "invalid_request",
                          "Request must include an array field named 'inputs'"));
            return;
          }

          const auto &inputs = body["inputs"];
          if (inputs.size() > static_cast<std::size_t>(cfg.max_batch_size)) {
            spdlog::warn(
                "embed request rejected code=batch_too_large batch_size={} max_batch_size={} duration_ms={}",
                inputs.size(), cfg.max_batch_size, elapsedMs());
            cb(buildError(drogon::k400BadRequest, "batch_too_large",
                          "Input batch exceeds configured max batch size"));
            return;
          }

          for (const auto &item : inputs) {
            if (!item.is_string()) {
              spdlog::warn(
                  "embed request rejected code=invalid_input_type batch_size={} duration_ms={}",
                  inputs.size(), elapsedMs());
              cb(buildError(drogon::k400BadRequest, "invalid_input_type",
                            "All inputs must be strings"));
              return;
            }
          }

          BatchInput batch;
          batch.reserve(inputs.size());
          for (const auto& item : inputs) {
            batch.push_back(item.get<std::string>());
          }

          auto vectors = shared_backend->embed(batch);
          if (vectors.size() != batch.size()) {
            spdlog::error(
                "embed request failed code=backend_output_mismatch reason=vector_count expected={} actual={} backend={} duration_ms={}",
                batch.size(), vectors.size(), shared_backend->backendName(),
                elapsedMs());
            cb(buildError(drogon::k500InternalServerError,
                          "backend_output_mismatch",
                          "Embedding backend returned incorrect vector count"));
            return;
          }

          for (const auto& vector : vectors) {
            if (vector.size() !=
                static_cast<std::size_t>(cfg.model_dimension)) {
              spdlog::error(
                  "embed request failed code=backend_output_mismatch reason=vector_dimension expected={} actual={} backend={} duration_ms={}",
                  cfg.model_dimension, vector.size(),
                  shared_backend->backendName(), elapsedMs());
              cb(buildError(drogon::k500InternalServerError,
                            "backend_output_mismatch",
                            "Embedding backend returned incorrect vector dimension"));
              return;
            }
          }

          json payload = {
              {"model", cfg.model_name},
              {"dimension", cfg.model_dimension},
              {"backend", shared_backend->backendName()},
              {"vectors", vectors},
          };
          spdlog::info(
              "embed request succeeded batch_size={} backend={} duration_ms={}",
              batch.size(), shared_backend->backendName(), elapsedMs());
          cb(buildJsonResponse(payload, drogon::k200OK));
        } catch (const json::parse_error &ex) {
          spdlog::warn("embed request rejected code=invalid_json duration_ms={} error='{}'",
                       elapsedMs(), ex.what());
          cb(buildError(drogon::k400BadRequest, "invalid_json", ex.what()));
        } catch (const std::exception &ex) {
          spdlog::error(
              "embed request failed code=embedding_failure duration_ms={} error='{}'",
              elapsedMs(), ex.what());
          cb(buildError(drogon::k500InternalServerError,
                        "embedding_failure", ex.what()));
        }
      },
      {drogon::Post});

  spdlog::info(
      "starting embeddings service version={} host={} port={} model={} "
      "dimension={} device={} backend={}",
      config_.service_version, config_.host, config_.port, config_.model_name,
      config_.model_dimension, config_.device, config_.accelerator_backend);

  drogon::app().addListener(config_.host, static_cast<uint16_t>(config_.port));
  drogon::app().run();
}
