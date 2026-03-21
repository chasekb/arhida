#include "server/HttpServer.h"

#include "embedding/BackendFactory.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

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
        try {
          auto body = json::parse(req->getBody());
          if (!body.contains("inputs") || !body["inputs"].is_array()) {
            cb(buildError(drogon::k400BadRequest, "invalid_request",
                          "Request must include an array field named 'inputs'"));
            return;
          }

          const auto &inputs = body["inputs"];
          if (inputs.size() > static_cast<std::size_t>(cfg.max_batch_size)) {
            cb(buildError(drogon::k400BadRequest, "batch_too_large",
                          "Input batch exceeds configured max batch size"));
            return;
          }

          for (const auto &item : inputs) {
            if (!item.is_string()) {
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
            cb(buildError(drogon::k500InternalServerError,
                          "backend_output_mismatch",
                          "Embedding backend returned incorrect vector count"));
            return;
          }

          for (const auto& vector : vectors) {
            if (vector.size() !=
                static_cast<std::size_t>(cfg.model_dimension)) {
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
          cb(buildJsonResponse(payload, drogon::k200OK));
        } catch (const json::parse_error &ex) {
          cb(buildError(drogon::k400BadRequest, "invalid_json", ex.what()));
        } catch (const std::exception &ex) {
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
