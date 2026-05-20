# esphome-components

External components for [ESPHome](https://esphome.io).

## Usage

Add to your device YAML:

```yaml
external_components:
  - source: github://iezhkv/esphome-components
    components: [web_server_idf, webhooks]
    refresh: 1h
```

Then configure the components you want. See per-component READMEs below.

## Components

| Component | What it does |
|---|---|
| [`web_server_idf`](components/web_server_idf/) | Drop-in override of ESPHome's IDF web server backend. Adds `PUT` / `DELETE` / `PATCH` registration, raw request body access via `AsyncWebServerRequest::body()`, and an expanded HTTP status code map. Upstream proposal: [esphome/esphome#16517](https://github.com/esphome/esphome/pull/16517). |
| [`webhooks`](components/webhooks/) | User-defined HTTP endpoints declared in YAML. Each entry registers a path + method, runs an `on_request` automation, and returns a templated `body:` or structured `json:` response. Upstream proposal: [esphome discussion #3673](https://github.com/orgs/esphome/discussions/3673). |

## Status

These are working components used in production by the author, but they are not yet upstreamed into ESPHome. Both have known limitations documented in their respective READMEs. APIs may change as the upstream proposals evolve.

## Roadmap / known issues

Stars: ⭐ trivial → ⭐⭐⭐⭐⭐ very hard.

| Done | # | Issue | Severity | Complexity | Notes |
|:---:|---|---|---|---|---|
| [ ] | 1 | Handlers run on httpd task — `delay:` / yielding actions crash | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | Real fix is main-loop deferral via `httpd_req_async_handler_begin/_end` + queue. Required for upstream. |
| [ ] | 2 | `POST` with no body returns 411 Length Required | ⭐⭐⭐ | ⭐ | Few-line change in `web_server_idf.cpp::request_post_handler` (allow missing Content-Length → treat as 0). |
| [ ] | 3 | Per-request heap allocation (`std::string` return, `JsonBuilder`) | ⭐⭐⭐ | ⭐⭐⭐⭐ | Refactor to `StringRef` views + write-to-buffer JSON. Main upstream review blocker. |
| [ ] | 4 | Exact path matching only — no `/api/door/{id}` templates | ⭐⭐⭐ | ⭐⭐⭐ | Add a tiny matcher (`{name}` capture into `request->path_param("name")`). |
| [ ] | 5 | NVS string cap 254 bytes for `globals: restore_value: true` | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | Not a webhooks issue per se — needs a different storage component (NVS-per-key, or chunked custom storage). |
| [ ] | 6 | Structured `json:` values must be strings (no numbers / nested objects) | ⭐⭐ | ⭐⭐ | Extend schema to accept richer types. Lambda form already does everything. |
| [ ] | 7 | No per-endpoint auth override | ⭐⭐ | ⭐⭐ | Wrap handler in `AuthMiddlewareHandler` with per-endpoint credentials. |
| [ ] | 8 | No streaming responses (response built fully in memory) | ⭐⭐ | ⭐⭐⭐ | Use `AsyncResponseStream` instead of one-shot `request->send`. |
| [ ] | 9 | Arduino backend untested (ESP8266, RP2040, ESP32-Arduino) | ⭐ | ⭐⭐ | Method enum is bit-flags vs single values. Verify + fix divergence. |
| [ ] | 10 | No automated `tests/components/webhooks/` fixtures | ⭐ | ⭐ | Required to upstream. |
| [ ] | 11 | No HEAD method support | ⭐ | ⭐ | Register `HTTP_HEAD` in `web_server_idf::begin()` + accept in schema. |
| [ ] | 12 | CORS preflight not endpoint-aware | ⭐ | ⭐⭐ | Endpoint-specific `Access-Control-Allow-Methods`. |

### What I'd attack first

- **Hotel-lock production-readiness:** #5 (storage) and #1 (concurrency safety) are the only ones that matter.
- **Getting upstreamed:** #1 and #3 are the gates. #10 is table-stakes. #2 fits neatly inside the existing `web_server_idf` PR.

Everything else is nice-to-have / v2.

## License

MIT — see [LICENSE](LICENSE).
