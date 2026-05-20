#pragma once

#include "esphome/components/web_server_base/web_server_base.h"

#include <ArduinoJson.h>
#include <string>

namespace esphome {
namespace access_control {

class AccessControl;  // forward decl

// Reads named scalars from a request body, transparently from either a parsed
// JSON document (preferred when Content-Type is application/json) or
// form-urlencoded fields / query string (everything else). One instance is
// built per request in handleRequest() and threaded into the per-method
// handlers.
struct ArgReader {
  AsyncWebServerRequest *request;
  const JsonDocument *body;  // nullable; null means "no JSON body present"

  std::string str(const char *name) const;
  int32_t i32(const char *name) const;
  bool boolean(const char *name) const;
};

// Routes `/access_control/{id}/...` requests to AccessControl::op_* methods.
//
// Request bodies may be JSON or application/x-www-form-urlencoded; the handler
// transparently reads field values from either via ArgReader. Application
// errors are returned as both an HTTP status (`status_for_error`) and a
// machine-readable `error` field in the JSON body.
//
// Supported routes:
//   GET    /credentials             list
//   POST   /credentials             create
//   DELETE /credentials?confirm=... delete-all
//   PATCH  /credentials/{uid}       update (partial)
//   DELETE /credentials/{uid}       delete one
//   POST   /scan                    simulate a card scan (RPC)
class AccessControlRestHandler : public AsyncWebHandler {
 public:
  explicit AccessControlRestHandler(AccessControl *parent) : parent_(parent) {}
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  // Per-(route × method) handlers. Each populates `root` and may set
  // `root["error"]` on application-level failure; the caller derives the HTTP
  // status from that error code via `status_for_error`.
  void handle_get_credentials_(JsonObject root);
  void handle_post_credentials_(JsonObject root, const ArgReader &args);
  void handle_delete_credentials_(JsonObject root, AsyncWebServerRequest *request);
  void handle_patch_credential_(JsonObject root, const std::string &uid, const ArgReader &args);
  void handle_delete_credential_(JsonObject root, const std::string &uid);
  void handle_post_scan_(JsonObject root, const ArgReader &args);

  AccessControl *parent_;
};

}  // namespace access_control
}  // namespace esphome
