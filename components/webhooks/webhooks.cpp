#include "webhooks.h"

#include "esphome/core/log.h"

namespace esphome {
namespace webhooks {

static const char *const TAG = "webhooks";

static const char *method_name(http_method m) {
  switch (m) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_PUT:
      return "PUT";
    case HTTP_DELETE:
      return "DELETE";
    case HTTP_PATCH:
      return "PATCH";
    default:
      return "?";
  }
}

void Webhook::setup() { this->base_->add_handler(this); }

void Webhook::dump_config() {
  ESP_LOGCONFIG(TAG, "Webhook %s %s", method_name(this->method_), this->path_.c_str());
}

bool Webhook::canHandle(AsyncWebServerRequest *request) const {
  if (request->method() != this->method_)
    return false;
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(url_buf) == this->path_;
}

void Webhook::handleRequest(AsyncWebServerRequest *request) {
  // NOTE: This runs on the httpd worker task, not the main loop.
  // Yielding actions (delay:, wait_until:, script.wait, etc.) inside on_request
  // will crash. For v1 we accept this constraint -- it matches the existing
  // ssieb/web_handler pattern. v2 will defer to main loop via async handlers.
  this->on_request_.call(request);

  const char *content_type = this->response_content_type_.empty() ? nullptr
                                                                  : this->response_content_type_.c_str();

  switch (this->mode_) {
    case ResponseMode::BODY: {
      std::string body = this->body_.value(request);
      if (content_type == nullptr)
        content_type = "text/plain";
      request->send(this->response_status_, content_type, body.c_str());
      return;
    }

    case ResponseMode::JSON_LAMBDA: {
      std::string out = json::build_json(
          [this, request](JsonObject root) { this->json_lambda_(request, root); });
      if (content_type == nullptr)
        content_type = "application/json";
      request->send(this->response_status_, content_type, out.c_str());
      return;
    }

    case ResponseMode::JSON_OBJECT: {
      std::string out = json::build_json([this, request](JsonObject root) {
        for (auto &field : this->json_fields_) {
          root[field.key] = field.value.value(request);
        }
      });
      if (content_type == nullptr)
        content_type = "application/json";
      request->send(this->response_status_, content_type, out.c_str());
      return;
    }

    case ResponseMode::NONE:
    default:
      if (content_type == nullptr)
        content_type = "text/plain";
      request->send(this->response_status_, content_type, nullptr);
      return;
  }
}

}  // namespace webhooks
}  // namespace esphome
