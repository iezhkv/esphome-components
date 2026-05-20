#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/preferences.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include <ArduinoJson.h>
#include <string>

namespace esphome {
namespace access_control {

// Bumped from 0xACCE5507: dropped mode/open_wait_ms/close_wait_ms from NVS now
// that they're owned by YAML config, not runtime-mutable persistent state.
// Old payloads won't be recognized and credentials will be wiped on first boot.
static const uint32_t NVS_MAGIC = 0xACCE5508;

static const uint16_t MAX_CREDS = 200;

enum class LockMode : uint8_t {
  MOMENTARY = 0,  // scan -> relay ON for open_wait_ms / close_wait_ms, then OFF
  LATCHING = 1,   // scan toggles relay; no auto-close
};

struct Credential {
  char value[32];
  uint32_t expires_at;
  bool one_time;
  bool privileged;  // bypass restrict_sensor when it's active
} __attribute__((packed));

struct NvsPayload {
  uint32_t magic;
  uint16_t count;
  Credential credentials[MAX_CREDS];
} __attribute__((packed));

enum class RelayState : uint8_t {
  IDLE,
  WAITING_OPEN,     // relay on; waiting for door to open (or timeout -> lock)
  WAITING_CLOSE,    // relay on; door open; waiting for close (or timeout -> lock)
};

struct ScanResult {
  bool granted;
  // Codes:
  //   "" on grant
  //   "NOT_FOUND", "EXPIRED", "RESTRICTED", "DEBOUNCED"
  //   "DOOR_ALREADY_OPEN", "DOOR_ALREADY_UNLOCKED",
  //   "DOOR_ALREADY_OPEN_AND_UNLOCKED" (momentary mode only)
  const char *code;
};

class AccessControlRestHandler;

class AccessControl : public Component {
 public:
  // ---- YAML configuration setters (called from to_code) ----
  void set_controller_id(const std::string &id) { controller_id_ = id; }
  const std::string &get_controller_id() const { return controller_id_; }
  void set_relay(switch_::Switch *relay) { relay_ = relay; }
  void set_time(time::RealTimeClock *t) { time_ = t; }
  void set_door_sensor(binary_sensor::BinarySensor *s) { door_sensor_ = s; }
  void set_restrict_sensor(binary_sensor::BinarySensor *s) { restrict_sensor_ = s; }
  void set_debounce_ms(uint32_t ms) { debounce_ms_ = ms; }
  void set_mode(LockMode m) { mode_ = m; }
  void set_open_wait_ms(uint32_t ms) { open_wait_ms_ = ms; }
  void set_close_wait_ms(uint32_t ms) { close_wait_ms_ = ms; }
  void set_bypass_door_sensor(bool b) { bypass_door_sensor_ = b; }
  void set_web_server_base(web_server_base::WebServerBase *base) { web_server_base_ = base; }

  // ---- Runtime accessors ----
  LockMode get_mode() const { return mode_; }
  uint32_t get_open_wait_ms() const { return open_wait_ms_; }
  uint32_t get_close_wait_ms() const { return close_wait_ms_; }
  const char *mode_string() const;

  // ---- Component lifecycle ----
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  ScanResult scan(const std::string &uid);

  // ---- Credential operations (called from the REST handler) ----
  // Each populates `root` for the JSON response. On error sets root["error"] = "CODE";
  // on success writes data fields. UID strings are copied into JsonDocument storage
  // by the caller via the patterns in rest_handler.cpp.
  void op_get_all(JsonObject root);
  void op_add(JsonObject root, const std::string &uid, int32_t expires_at,
              bool one_time, bool privileged);
  void op_update(JsonObject root, const std::string &uid, int32_t expires_at,
                 bool one_time, bool privileged);
  void op_delete(JsonObject root, const std::string &uid);
  void op_delete_all(JsonObject root, const std::string &confirm);
  void op_scan(JsonObject root, const std::string &uid);

 protected:
  void load_nvs_();
  void save_nvs_();
  int find_credential_(const std::string &uid) const;
  void engage_relay_();

  std::string controller_id_;
  switch_::Switch *relay_{nullptr};
  time::RealTimeClock *time_{nullptr};
  binary_sensor::BinarySensor *door_sensor_{nullptr};
  binary_sensor::BinarySensor *restrict_sensor_{nullptr};

  // YAML-configured behavior — set once at boot, not runtime-mutable.
  LockMode mode_{LockMode::MOMENTARY};
  uint32_t open_wait_ms_{3000};
  uint32_t close_wait_ms_{10000};
  bool bypass_door_sensor_{false};
  uint32_t debounce_ms_{3000};

  web_server_base::WebServerBase *web_server_base_{nullptr};
  AccessControlRestHandler *rest_handler_{nullptr};

  NvsPayload payload_{};
  ESPPreferenceObject pref_;

  RelayState state_{RelayState::IDLE};
  uint32_t state_deadline_{0};
  std::string last_scanned_uid_;
  uint32_t last_scan_time_{0};
};

template<typename... Ts> class ScanAction : public Action<Ts...> {
 public:
  explicit ScanAction(AccessControl *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, uid)
  void play(Ts... x) override { this->parent_->scan(this->uid_.value(x...)); }

 protected:
  AccessControl *parent_;
};

}  // namespace access_control
}  // namespace esphome
