#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include <esp_http_server.h>

#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace webhooks {

class Webhook : public Component, public AsyncWebHandler {
 public:
  Webhook(web_server_base::WebServerBase *base) : base_(base) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_path(std::string path) { this->path_ = std::move(path); }
  void set_method(http_method method) { this->method_ = method; }
  void set_response_status(int status) { this->response_status_ = status; }
  void set_response_content_type(std::string ct) { this->response_content_type_ = std::move(ct); }

  void set_response_body(TemplatableValue<std::string, AsyncWebServerRequest *> body) {
    this->body_ = std::move(body);
    this->mode_ = ResponseMode::BODY;
  }

  void set_response_json_lambda(std::function<void(AsyncWebServerRequest *, JsonObject)> fn) {
    this->json_lambda_ = std::move(fn);
    this->mode_ = ResponseMode::JSON_LAMBDA;
  }

  void init_response_json_object(size_t n) {
    this->json_fields_.reserve(n);
    this->mode_ = ResponseMode::JSON_OBJECT;
  }
  void add_response_json_field(const char *key, TemplatableValue<std::string, AsyncWebServerRequest *> value) {
    this->json_fields_.push_back({key, std::move(value)});
  }

  template<typename F> void add_on_request_callback(F &&cb) {
    this->on_request_.add(std::forward<F>(cb));
  }

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  enum class ResponseMode : uint8_t { NONE, BODY, JSON_LAMBDA, JSON_OBJECT };

  struct JsonField {
    const char *key;
    TemplatableValue<std::string, AsyncWebServerRequest *> value;
  };

  web_server_base::WebServerBase *base_;
  std::string path_;
  http_method method_{HTTP_GET};

  ResponseMode mode_{ResponseMode::NONE};
  int response_status_{200};
  std::string response_content_type_;  // empty -> pick default by mode

  TemplatableValue<std::string, AsyncWebServerRequest *> body_;
  std::function<void(AsyncWebServerRequest *, JsonObject)> json_lambda_;
  std::vector<JsonField> json_fields_;

  LazyCallbackManager<void(AsyncWebServerRequest *)> on_request_;
};

}  // namespace webhooks
}  // namespace esphome
