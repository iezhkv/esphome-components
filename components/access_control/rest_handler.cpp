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

// ---- Helpers ----

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

// arg() looks up both URL query params and form-URL-encoded body fields.
static bool arg_bool(AsyncWebServerRequest *request, const char *name, bool def = false) {
  std::string s = request->arg(name);
  if (s.empty()) return def;
  return s == "1" || s == "true" || s == "True" || s == "TRUE" || s == "yes";
}

static int32_t arg_int(AsyncWebServerRequest *request, const char *name, int32_t def = 0) {
  std::string s = request->arg(name);
  if (s.empty()) return def;
  return static_cast<int32_t>(std::strtol(s.c_str(), nullptr, 10));
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
  // url_to() already strips the query string, but keep this for defense.
  size_t q = path.find('?');
  if (q != std::string::npos)
    path.resize(q);

  int method = request->method();

  // Parse JSON body once if present; fall back to form/query args otherwise.
  JsonDocument body_doc;
  bool has_json = false;
  auto raw_body = request->body();
  if (raw_body.size() > 0 &&
      deserializeJson(body_doc, raw_body.c_str(), raw_body.size()) == DeserializationError::Ok) {
    has_json = true;
  }
  auto get_str = [&](const char *name) -> std::string {
    if (has_json && !body_doc[name].isNull())
      return body_doc[name].as<std::string>();
    return request->arg(name);
  };
  auto get_int = [&](const char *name) -> int32_t {
    if (has_json && !body_doc[name].isNull())
      return body_doc[name].as<int32_t>();
    return arg_int(request, name);
  };
  auto get_bool = [&](const char *name) -> bool {
    if (has_json && !body_doc[name].isNull())
      return body_doc[name].as<bool>();
    return arg_bool(request, name);
  };

  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  int status = 200;

  if (path == "credentials") {
    // Collection: list, create, or delete-all
    if (method == HTTP_GET) {
      parent_->op_get_all(root);
    } else if (method == HTTP_POST) {
      parent_->op_add(root, get_str("uid"), get_int("expires_at"),
                      get_bool("one_time"), get_bool("privileged"));
    } else if (method == HTTP_DELETE) {
      // DELETE on the collection = delete all. op_delete_all validates the
      // confirm string and returns CONFIRMATION_REQUIRED (→ 403) if missing/wrong.
      parent_->op_delete_all(root, request->arg("confirm"));
    } else {
      status = 405;
      root["error"] = "METHOD_NOT_ALLOWED";
    }
  } else if (path.rfind("credentials/", 0) == 0) {
    // Item: update or delete
    std::string uid = path.substr(strlen("credentials/"));
    if (uid.empty()) {
      status = 404;
      root["error"] = "MISSING_UID";
    } else if (method == HTTP_PATCH || method == HTTP_PUT) {
      parent_->op_update(root, uid, get_int("expires_at"),
                         get_bool("one_time"), get_bool("privileged"));
    } else if (method == HTTP_DELETE) {
      parent_->op_delete(root, uid);
    } else {
      status = 405;
      root["error"] = "METHOD_NOT_ALLOWED";
    }
  } else if (path == "scan" && method == HTTP_POST) {
    parent_->op_scan(root, get_str("uid"));
  } else {
    status = 404;
    root["error"] = "NOT_FOUND";
    root["path"] = path;
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

}  // namespace access_control
}  // namespace esphome
