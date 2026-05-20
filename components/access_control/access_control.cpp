#include "access_control.h"
#include "rest_handler.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstring>
#include <functional>

namespace esphome {
namespace access_control {

static const char *const TAG = "access_control";

void AccessControl::setup() {
  uint32_t hash = std::hash<std::string>{}(std::string("access_control:") + controller_id_);
  pref_ = global_preferences->make_preference<NvsPayload>(hash, true);
  load_nvs_();

  if (web_server_base_ != nullptr) {
    rest_handler_ = new AccessControlRestHandler(this);
    web_server_base_->add_handler(rest_handler_);
    ESP_LOGI(TAG, "REST endpoints registered at /access_control/%s/", controller_id_.c_str());
  }
}

void AccessControl::loop() {
  if (state_ == RelayState::IDLE || mode_ != LockMode::MOMENTARY)
    return;

  bool door_open = !bypass_door_sensor_ && door_sensor_ != nullptr && door_sensor_->state;
  uint32_t now = millis();

  if (state_ == RelayState::WAITING_OPEN) {
    if (door_open) {
      ESP_LOGD(TAG, "Door opened; waiting up to %u ms for close", close_wait_ms_);
      state_ = RelayState::WAITING_CLOSE;
      state_deadline_ = now + close_wait_ms_;
      return;
    }
    if (now >= state_deadline_) {
      ESP_LOGI(TAG, "Open timeout — locking (door never opened)");
      relay_->turn_off();
      state_ = RelayState::IDLE;
    }
    return;
  }

  // WAITING_CLOSE
  if (!door_open) {
    ESP_LOGI(TAG, "Door closed — locking");
    relay_->turn_off();
    state_ = RelayState::IDLE;
    return;
  }
  if (now >= state_deadline_) {
    ESP_LOGW(TAG, "Close timeout — locking (door left open)");
    relay_->turn_off();
    state_ = RelayState::IDLE;
  }
}

void AccessControl::dump_config() {
  ESP_LOGCONFIG(TAG, "Access Control:");
  ESP_LOGCONFIG(TAG, "  Controller ID: %s", controller_id_.c_str());
  ESP_LOGCONFIG(TAG, "  Mode: %s", mode_string());
  ESP_LOGCONFIG(TAG, "  Open wait: %u ms", open_wait_ms_);
  ESP_LOGCONFIG(TAG, "  Close wait: %u ms", close_wait_ms_);
  ESP_LOGCONFIG(TAG, "  Bypass door sensor: %s", bypass_door_sensor_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Debounce: %u ms", debounce_ms_);
  ESP_LOGCONFIG(TAG, "  Time source: %s", time_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Door sensor: %s", door_sensor_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Restrict sensor: %s", restrict_sensor_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  REST endpoints: %s", web_server_base_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Credentials loaded: %u", payload_.count);
}

const char *AccessControl::mode_string() const {
  return mode_ == LockMode::LATCHING ? "latching" : "momentary";
}

void AccessControl::load_nvs_() {
  if (!pref_.load(&payload_) || payload_.magic != NVS_MAGIC) {
    ESP_LOGW(TAG, "NVS empty or invalid; initializing");
    std::memset(&payload_, 0, sizeof(payload_));
    payload_.magic = NVS_MAGIC;
    payload_.count = 0;
    save_nvs_();
  }
  if (payload_.count > MAX_CREDS)
    payload_.count = MAX_CREDS;
}

void AccessControl::save_nvs_() {
  payload_.magic = NVS_MAGIC;
  pref_.save(&payload_);
  // Force immediate commit. Without this, a quick reboot can lose recent writes.
  global_preferences->sync();
}

int AccessControl::find_credential_(const std::string &uid) const {
  for (uint16_t i = 0; i < payload_.count; i++) {
    if (std::strncmp(payload_.credentials[i].value, uid.c_str(),
                     sizeof(payload_.credentials[i].value)) == 0)
      return i;
  }
  return -1;
}

void AccessControl::engage_relay_() {
  if (mode_ == LockMode::MOMENTARY) {
    relay_->turn_on();
    state_ = RelayState::WAITING_OPEN;
    state_deadline_ = millis() + open_wait_ms_;
  } else {
    // LATCHING: simple toggle. State machine unused.
    if (relay_->state) {
      relay_->turn_off();
    } else {
      relay_->turn_on();
    }
  }
}

ScanResult AccessControl::scan(const std::string &uid) {
  uint32_t now = millis();
  if (uid == last_scanned_uid_ && (now - last_scan_time_) < debounce_ms_) {
    ESP_LOGD(TAG, "Debounced scan: %s", uid.c_str());
    return {false, "DEBOUNCED"};
  }
  last_scanned_uid_ = uid;
  last_scan_time_ = now;

  // Pre-check physical state (momentary only).
  if (mode_ == LockMode::MOMENTARY) {
    bool door_open = !bypass_door_sensor_ && door_sensor_ != nullptr && door_sensor_->state;
    bool relay_engaged = state_ != RelayState::IDLE;
    const char *state_code = nullptr;
    if (door_open && relay_engaged) {
      state_code = "DOOR_ALREADY_OPEN_AND_UNLOCKED";
    } else if (door_open) {
      state_code = "DOOR_ALREADY_OPEN";
    } else if (relay_engaged) {
      state_code = "DOOR_ALREADY_UNLOCKED";
    }
    if (state_code != nullptr) {
      ESP_LOGI(TAG, "DENIED %s %s", uid.c_str(), state_code);
      return {false, state_code};
    }
  }

  int idx = find_credential_(uid);
  if (idx < 0) {
    ESP_LOGI(TAG, "DENIED %s NOT_FOUND", uid.c_str());
    return {false, "NOT_FOUND"};
  }

  Credential &c = payload_.credentials[idx];

  if (time_ != nullptr && c.expires_at > 0) {
    auto t = time_->now();
    if (t.is_valid() && t.timestamp > static_cast<time_t>(c.expires_at)) {
      ESP_LOGI(TAG, "DENIED %s EXPIRED", uid.c_str());
      return {false, "EXPIRED"};
    }
  }

  bool restricted = restrict_sensor_ != nullptr && restrict_sensor_->state;
  if (restricted && !c.privileged) {
    ESP_LOGI(TAG, "DENIED %s RESTRICTED", uid.c_str());
    return {false, "RESTRICTED"};
  }

  if (c.one_time) {
    for (uint16_t i = idx; i + 1 < payload_.count; i++)
      payload_.credentials[i] = payload_.credentials[i + 1];
    payload_.count--;
    save_nvs_();
  }

  ESP_LOGI(TAG, "GRANTED %s", uid.c_str());
  engage_relay_();
  return {true, ""};
}

// ---- Credential operations ----
// Convention: on failure set root["error"] = "CODE"; on success set data keys.
// All UIDs are written via std::string to force ArduinoJson to copy them — the
// caller's stack frames may go out of scope before serializeJson runs.

static void write_credential_json(JsonObject root, const Credential &c) {
  root["uid"] = std::string(c.value);
  root["expires_at"] = c.expires_at;
  root["one_time"] = c.one_time;
  root["privileged"] = c.privileged;
}

void AccessControl::op_get_all(JsonObject root) {
  root["count"] = payload_.count;
  JsonArray creds = root["credentials"].to<JsonArray>();
  for (uint16_t i = 0; i < payload_.count; i++) {
    JsonObject obj = creds.add<JsonObject>();
    write_credential_json(obj, payload_.credentials[i]);
  }
}

void AccessControl::op_add(JsonObject root, const std::string &uid, int32_t expires_at,
                           bool one_time, bool privileged) {
  if (uid.empty() || uid.size() >= sizeof(payload_.credentials[0].value)) {
    root["error"] = "INVALID_UID";
    return;
  }
  if (find_credential_(uid) >= 0) {
    root["error"] = "ALREADY_EXISTS";
    return;
  }
  if (payload_.count >= MAX_CREDS) {
    root["error"] = "MAX_CREDS_REACHED";
    return;
  }

  Credential &c = payload_.credentials[payload_.count];
  std::memset(&c, 0, sizeof(c));
  std::strncpy(c.value, uid.c_str(), sizeof(c.value) - 1);
  c.expires_at = expires_at < 0 ? 0 : static_cast<uint32_t>(expires_at);
  c.one_time = one_time;
  c.privileged = privileged;
  payload_.count++;
  save_nvs_();
  ESP_LOGI(TAG, "Added credential %s (count=%u)", c.value, payload_.count);
  write_credential_json(root, c);
  root["count"] = payload_.count;
}

void AccessControl::op_update(JsonObject root, const std::string &uid, int32_t expires_at,
                              bool one_time, bool privileged) {
  if (uid.empty() || uid.size() >= sizeof(payload_.credentials[0].value)) {
    root["error"] = "INVALID_UID";
    return;
  }
  int idx = find_credential_(uid);
  if (idx < 0) {
    root["error"] = "NOT_FOUND";
    return;
  }
  Credential &c = payload_.credentials[idx];
  c.expires_at = expires_at < 0 ? 0 : static_cast<uint32_t>(expires_at);
  c.one_time = one_time;
  c.privileged = privileged;
  save_nvs_();
  ESP_LOGI(TAG, "Updated credential %s", c.value);
  write_credential_json(root, c);
}

void AccessControl::op_delete(JsonObject root, const std::string &uid) {
  int idx = find_credential_(uid);
  if (idx < 0) {
    root["error"] = "NOT_FOUND";
    return;
  }
  // Copy the removed entry into JSON *before* mutating the array — once we
  // compact, the original slot's data is overwritten.
  write_credential_json(root, payload_.credentials[idx]);
  for (uint16_t i = idx; i + 1 < payload_.count; i++)
    payload_.credentials[i] = payload_.credentials[i + 1];
  payload_.count--;
  save_nvs_();
  ESP_LOGI(TAG, "Deleted credential (count=%u)", payload_.count);
  root["count"] = payload_.count;
}

void AccessControl::op_delete_all(JsonObject root, const std::string &confirm) {
  static const char *const REQUIRED_CONFIRM = "delete all credentials";
  if (confirm != REQUIRED_CONFIRM) {
    root["error"] = "CONFIRMATION_REQUIRED";
    root["required_confirm"] = REQUIRED_CONFIRM;
    return;
  }
  uint16_t prev = payload_.count;
  payload_.count = 0;
  save_nvs_();
  ESP_LOGI(TAG, "Deleted all %u credentials", prev);
  root["previous_count"] = prev;
  root["count"] = 0;
}

void AccessControl::op_scan(JsonObject root, const std::string &uid) {
  ScanResult r = scan(uid);
  root["uid"] = uid;
  root["granted"] = r.granted;
  root["code"] = r.code;
}

}  // namespace access_control
}  // namespace esphome
