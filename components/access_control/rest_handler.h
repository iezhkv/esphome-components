#pragma once

#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome {
namespace access_control {

class AccessControl;  // forward decl

// Routes `/access_control/{id}/...` requests to AccessControl::op_* methods.
//
// The IDF web server (used in this build) has two constraints we work around:
//   1. Only GET, POST, OPTIONS are routed — PUT/DELETE return 405 at the framework
//      layer, never reaching our handler. So every mutation is `POST <action>`.
//   2. Only HTTP status codes 200, 404, 409 map to real status strings; any
//      other code silently becomes 500. So we put the application-level error
//      code in the JSON body and only differentiate 404 / 409 at the HTTP layer.
//
// Body must be application/x-www-form-urlencoded — raw JSON bodies are not
// exposed by the framework. Field names are read with request->arg().
class AccessControlRestHandler : public AsyncWebHandler {
 public:
  explicit AccessControlRestHandler(AccessControl *parent) : parent_(parent) {}
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  AccessControl *parent_;
};

}  // namespace access_control
}  // namespace esphome
