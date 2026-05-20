# access_control

An RFID/wiegand-driven door lock controller for hotel-room style access. Stores
up to 200 credentials in NVS, validates them on scan, and engages a relay if
the scan is granted. All credential management happens over a small REST API.

Built on top of the `wiegand` (or any reader-of-your-choice that lands UIDs via
the `access_control.scan` action), `switch`, `binary_sensor`, `time` and
`web_server_base` components.

## Quick start

```yaml
external_components:
  - source: github://iezhkv/esphome-components
    components: [web_server_idf, access_control]
    refresh: 1h

web_server:
  port: 80

switch:
  - platform: gpio
    id: door_relay
    pin: ...
    restore_mode: ALWAYS_OFF

binary_sensor:
  - platform: gpio
    id: reed
    pin: ...
    device_class: door

time:
  - platform: sntp
    id: sntp_time

access_control:
  id: door_controller
  relay_id: door_relay
  time_id: sntp_time
  door_sensor_id: reed
  mode: momentary           # momentary | latching
  open_wait: 3s             # max wait for door to OPEN after unlock (momentary mode)
  close_wait: 10s           # max time door can stay OPEN before relock
  debounce_time: 3000ms     # ignore repeat scans of the same UID within this window
```

Wiring a reader to feed UIDs into this component is up to you — typically a
`wiegand` component's `on_tag` automation calls the `access_control.scan` action:

```yaml
wiegand:
  - id: rdr
    d0: GPIO16
    d1: GPIO17
    on_tag:
      - access_control.scan:
          id: door_controller
          uid: !lambda 'return x;'
```

## REST API

All endpoints live under `/access_control/<id>/` where `<id>` is the
`id:` you gave the `access_control:` block. Examples below use
`door_controller`.

Request bodies accept either form-urlencoded or JSON.

### List

```
GET /access_control/door_controller/credentials
```

```json
200 OK
{
  "count": 2,
  "credentials": [
    {"uid": "04AABBCC", "expires_at": 0, "one_time": false, "privileged": false},
    {"uid": "DEADBEEF", "expires_at": 1900000000, "one_time": true, "privileged": false}
  ]
}
```

### Create

```
POST /access_control/door_controller/credentials
Content-Type: application/json

{"uid": "04AABBCC", "expires_at": 0, "one_time": false, "privileged": false}
```

| Field | Type | Default | Meaning |
|---|---|---|---|
| `uid` | string | — | Card UID. 1–31 chars. Required. |
| `expires_at` | int | `0` | Unix timestamp. `0` means never expires. |
| `one_time` | bool | `false` | Credential is deleted after first granted scan. |
| `privileged` | bool | `false` | Bypasses `restrict_sensor` (e.g. DND) when active. |

Responses:
- `200` — created. Body echoes the credential plus `count`.
- `400 BAD_REQUEST` — `INVALID_UID`.
- `409 CONFLICT` — `ALREADY_EXISTS`.
- `507 INSUFFICIENT_STORAGE` — `CAPACITY_EXCEEDED` (200-credential cap reached).

### Update (partial)

```
PATCH /access_control/door_controller/credentials/04AABBCC
Content-Type: application/json

{"expires_at": 2000000000, "one_time": true}
```

Only the supplied fields are changed; omitted fields keep their previous values.

Responses:
- `200` — updated. Body echoes the new credential state.
- `404 NOT_FOUND`.

### Delete one

```
DELETE /access_control/door_controller/credentials/04AABBCC
```

Responses:
- `200` — deleted. Body echoes the deleted credential plus updated `count`.
- `404 NOT_FOUND`.

### Delete all

```
DELETE /access_control/door_controller/credentials?confirm=delete%20all%20credentials
```

The `confirm` string must equal `"delete all credentials"` exactly. Anything else returns:

- `403 FORBIDDEN` — `CONFIRMATION_REQUIRED`.
- `200` on success, body `{"count": 0}`.

### Scan (RPC)

Simulates a card-reader scan against the stored credential list. Same path the
relay engagement logic uses internally.

```
POST /access_control/door_controller/scan
Content-Type: application/json

{"uid": "04AABBCC"}
```

```json
200 OK
{"uid": "04AABBCC", "granted": true, "code": ""}
```

`code` values when `granted: false`:

| code | meaning |
|---|---|
| `NOT_FOUND` | UID not in the credential list |
| `EXPIRED` | `expires_at` is in the past |
| `RESTRICTED` | `restrict_sensor` is active and credential is not `privileged` |
| `DEBOUNCED` | Repeat scan of the same UID within `debounce_time` |
| `DOOR_ALREADY_OPEN` | Door is already open (latching mode safety) |
| `DOOR_ALREADY_UNLOCKED` | Relay already on |
| `DOOR_ALREADY_OPEN_AND_UNLOCKED` | Both (momentary mode only) |

## HTTP status code reference

| Status | When |
|---|---|
| `200 OK` | Operation succeeded |
| `400 Bad Request` | `INVALID_UID` (empty / too long) |
| `403 Forbidden` | `CONFIRMATION_REQUIRED` (delete-all) |
| `404 Not Found` | Unknown path, or `NOT_FOUND` (no such credential) |
| `405 Method Not Allowed` | Verb not supported for that path |
| `409 Conflict` | `ALREADY_EXISTS` (duplicate UID on create) |
| `507 Insufficient Storage` | `CAPACITY_EXCEEDED` (credential cap reached) |

Clients can branch on the HTTP status code; the `error` field in the body
gives the specific code as a string for finer-grained handling.

## Browser tester

[`tester.html`](tester.html) is a single-file dashboard for exercising the
API by hand. Open it locally:

```bash
open components/access_control/tester.html       # macOS
xdg-open components/access_control/tester.html   # linux
```

Set the **Host** field to your device's IP and **ID** to the controller id
(`door_controller` by default). Operations show colored toasts at the top
(green = success, yellow = application error, red = connection failure) and
the full request/response log is in the collapsible panel at the bottom.

## Python integration tests

[`test_rest.py`](test_rest.py) — a destructive integration suite that wipes
the credential store before and after running. Don't point it at a
production controller.

```bash
python3 components/access_control/test_rest.py --host 192.168.1.42 --id door_controller
```

Exits 0 on all-green, 1 on any failure.

## Dependencies

This component depends on PR-#1-style `web_server_idf` plumbing for proper
`PATCH` / `DELETE` registration and raw request body access. If you're
running stock ESPHome (without the [`web_server_idf` override in this
repo](../web_server_idf/) or upstream
[esphome/esphome#16517](https://github.com/esphome/esphome/pull/16517)),
`PATCH` and `DELETE` requests will be rejected with 405, and JSON request
bodies will be silently dropped.

The recommended setup is to load both:

```yaml
external_components:
  - source: github://iezhkv/esphome-components
    components: [web_server_idf, access_control]
    refresh: 1h
```

## Known issues / limitations

| # | Issue | Severity | Notes |
|:---:|---|---|---|
| 1 | NVS stores up to **200 credentials max** | ⭐⭐⭐ | Hard-coded `MAX_CREDS` in `access_control.h`. Increase if you need more, mindful of NVS slot size. |
| 2 | UIDs are limited to **31 characters** | ⭐ | `Credential::value[32]` — buffer + null. Wiegand UIDs are well under this. |
| 3 | All credential data lives in a single NVS payload | ⭐⭐ | Whole payload is rewritten on every change. For a 200-row store with frequent updates this still won't wear NVS for years, but worth knowing. |
| 4 | No bulk endpoint | ⭐⭐ | Hotel check-in scenarios (50 guests rolling over at once) take 50 separate POSTs. A `POST /credentials/bulk` accepting an array would be a clean v2. |
| 5 | No pagination on `GET /credentials` | ⭐ | At 200 max, the whole list fits comfortably in one response. Becomes a concern if `MAX_CREDS` grows. |
| 6 | REST handlers run on the httpd worker task | ⭐⭐ | Same caveat as the [`webhooks`](../webhooks/) component — yielding actions inside `on_request` will crash. Not relevant here because `op_*` methods are all synchronous (NVS write + JSON build, no yields). |
| 7 | `time_id` not strictly required, but `expires_at` is meaningless without it | ⭐ | If you omit `time_id`, expiry checks always pass. |
| 8 | No auth on REST endpoints by default | ⭐⭐⭐ | Inherits whatever `web_server.auth` is configured with (typically nothing). For internet-exposed devices, set `web_server.auth` or put the device behind something with TLS+auth. |

## License

MIT
