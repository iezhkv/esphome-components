#include "rest_handler.h"
#include "access_control.h"
#include "esphome/core/log.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome {
namespace access_control {

static const char *const TAG = "access_control.rest";

// ---- Status mapping ----

// Map application error codes (from op_*) to HTTP status. Unknown error codes
// stay 200-with-body.error so existing clients that only branch on error
// strings keep working. New codes can be added freely.
static int status_for_error(const char *err) {
  if (err == nullptr) return 200;
  if (std::strcmp(err, "NOT_FOUND") == 0) return 404;
  if (std::strcmp(err, "ALREADY_EXISTS") == 0) return 409;
  if (std::strcmp(err, "BAD_INPUT") == 0) return 400;
  if (std::strcmp(err, "INVALID_UID") == 0) return 400;
  if (std::strcmp(err, "FORBIDDEN") == 0) return 403;
  if (std::strcmp(err, "CONFIRMATION_REQUIRED") == 0) return 403;
  if (std::strcmp(err, "CAPACITY_EXCEEDED") == 0) return 507;
  return 200;
}

// ---- ArgReader ----

std::string ArgReader::str(const char *name) const {
  if (this->body != nullptr && !(*this->body)[name].isNull())
    return (*this->body)[name].as<std::string>();
  return this->request->arg(name);
}

int32_t ArgReader::i32(const char *name) const {
  if (this->body != nullptr && !(*this->body)[name].isNull())
    return (*this->body)[name].as<int32_t>();
  std::string s = this->request->arg(name);
  if (s.empty()) return 0;
  return static_cast<int32_t>(std::strtol(s.c_str(), nullptr, 10));
}

bool ArgReader::boolean(const char *name) const {
  if (this->body != nullptr && !(*this->body)[name].isNull())
    return (*this->body)[name].as<bool>();
  std::string s = this->request->arg(name);
  if (s.empty()) return false;
  return s == "1" || s == "true" || s == "True" || s == "TRUE" || s == "yes";
}

// ---- Route parsing ----

enum class Route : uint8_t {
  CREDENTIALS_COLLECTION,  // /credentials
  CREDENTIALS_ITEM,        // /credentials/{uid}  (uid_out populated)
  SCAN,                    // /scan
  UNKNOWN,
};

static Route parse_route(const std::string &path, std::string &uid_out) {
  if (path == "credentials") return Route::CREDENTIALS_COLLECTION;
  if (path.rfind("credentials/", 0) == 0) {
    uid_out = path.substr(std::strlen("credentials/"));
    return uid_out.empty() ? Route::UNKNOWN : Route::CREDENTIALS_ITEM;
  }
  if (path == "scan") return Route::SCAN;
  return Route::UNKNOWN;
}

// ---- Handler ----

bool AccessControlRestHandler::canHandle(AsyncWebServerRequest *request) const {
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  std::string url(request->url_to(url_buf));
  std::string prefix = "/access_control/" + parent_->get_controller_id() + "/";
  return url.compare(0, prefix.size(), prefix) == 0;
}

void AccessControlRestHandler::handleRequest(AsyncWebServerRequest *request) {
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  std::string url(request->url_to(url_buf));
  std::string prefix = "/access_control/" + parent_->get_controller_id() + "/";
  if (url.compare(0, prefix.size(), prefix) != 0) {
    request->send(404);
    return;
  }
  std::string path = url.substr(prefix.size());
  size_t q = path.find('?');
  if (q != std::string::npos)
    path.resize(q);

  // Parse JSON body once if present; ArgReader falls back to form/query args.
  JsonDocument body_doc;
  bool has_json = false;
  auto raw_body = request->body();
  if (raw_body.size() > 0 &&
      deserializeJson(body_doc, raw_body.c_str(), raw_body.size()) == DeserializationError::Ok) {
    has_json = true;
  }
  ArgReader args{request, has_json ? &body_doc : nullptr};

  std::string uid;
  Route route = parse_route(path, uid);
  int method = request->method();

  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  int status = 200;

  switch (route) {
    case Route::CREDENTIALS_COLLECTION:
      switch (method) {
        case HTTP_GET:    this->handle_get_credentials_(root); break;
        case HTTP_POST:   this->handle_post_credentials_(root, args); break;
        case HTTP_DELETE: this->handle_delete_credentials_(root, request); break;
        default:
          status = 405;
          root["error"] = "METHOD_NOT_ALLOWED";
      }
      break;

    case Route::CREDENTIALS_ITEM:
      switch (method) {
        case HTTP_PATCH:
        case HTTP_PUT:    this->handle_patch_credential_(root, uid, args); break;
        case HTTP_DELETE: this->handle_delete_credential_(root, uid); break;
        default:
          status = 405;
          root["error"] = "METHOD_NOT_ALLOWED";
      }
      break;

    case Route::SCAN:
      if (method == HTTP_POST) {
        this->handle_post_scan_(root, args);
      } else {
        status = 405;
        root["error"] = "METHOD_NOT_ALLOWED";
      }
      break;

    case Route::UNKNOWN:
    default:
      status = 404;
      root["error"] = "NOT_FOUND";
      root["path"] = path;
      break;
  }

  // If routing didn't hard-set a 4xx above, derive status from op_* error code.
  if (status == 200) {
    const char *err = root["error"].as<const char *>();
    status = status_for_error(err);
  }

  std::string out;
  serializeJson(doc, out);
  request->send(status, "application/json", out.c_str());
}

// ---- Per-method handlers ----

void AccessControlRestHandler::handle_get_credentials_(JsonObject root) {
  parent_->op_get_all(root);
}

void AccessControlRestHandler::handle_post_credentials_(JsonObject root, const ArgReader &args) {
  parent_->op_add(root, args.str("uid"), args.i32("expires_at"),
                  args.boolean("one_time"), args.boolean("privileged"));
}

void AccessControlRestHandler::handle_delete_credentials_(JsonObject root, AsyncWebServerRequest *request) {
  // DELETE on the collection = delete all. op_delete_all validates the
  // confirm string and returns CONFIRMATION_REQUIRED (→ 403) if missing/wrong.
  // Read from request->arg() directly (not ArgReader): the value comes from
  // a query string, never from a JSON body, for this idempotent endpoint.
  parent_->op_delete_all(root, request->arg("confirm"));
}

void AccessControlRestHandler::handle_patch_credential_(JsonObject root, const std::string &uid,
                                                       const ArgReader &args) {
  parent_->op_update(root, uid, args.i32("expires_at"),
                     args.boolean("one_time"), args.boolean("privileged"));
}

void AccessControlRestHandler::handle_delete_credential_(JsonObject root, const std::string &uid) {
  parent_->op_delete(root, uid);
}

void AccessControlRestHandler::handle_post_scan_(JsonObject root, const ArgReader &args) {
  parent_->op_scan(root, args.str("uid"));
}

}  // namespace access_control
}  // namespace esphome
