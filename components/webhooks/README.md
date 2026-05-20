# webhooks

User-defined HTTP endpoints in ESPHome YAML. Declare a path + method, run an
automation on request, return a templated `body:` or structured `json:`
response.

Depends on [`web_server`](https://esphome.io/components/web_server.html).
Endpoints share the same `AsyncWebServer` instance, port, and auth (if
configured) as `web_server`.

## Quick example

```yaml
external_components:
  - source: github://iezhkv/esphome-components
    components: [web_server_idf, webhooks]
    refresh: 1h

web_server:
  port: 80

webhooks:
  - id: hello
    path: /api/hello
    method: GET
    response:
      body: !lambda 'return std::string("hello\n");'

  - id: door_open
    path: /api/door/open
    method: POST
    on_request:
      - switch.turn_on: door_relay
    response:
      json:
        ok: !lambda 'return std::string("true");'
        uid: !lambda 'return std::string(request->arg("uid"));'
```

## Configuration

Each entry under `webhooks:` declares one endpoint.

| Key | Type | Default | Notes |
|---|---|---|---|
| `id` | id | required | Unique component id. |
| `path` | string | required | Exact path match. No templates / prefix matching (v1). |
| `method` | string | `GET` | One of `GET`, `POST`, `PUT`, `DELETE`, `PATCH`. |
| `on_request` | automation | — | Runs before the response is built. `request` is exposed as `AsyncWebServerRequest *`. |
| `response.status` | int | `200` | HTTP status code. |
| `response.content_type` | string | auto | Defaults to `text/plain` for `body:` or `application/json` for `json:`. |
| `response.body` | templatable string | — | Plain / templated response body. Mutually exclusive with `json:`. |
| `response.json` | object \| lambda | — | Structured JSON response. Either `{key: templatable-string}` map or `!lambda` taking `root` as `JsonObject`. Mutually exclusive with `body:`. |

### Accessing the request

Inside `on_request` action lambdas, and `body` / `json` lambdas, a `request`
variable is in scope:

| Call | Returns |
|---|---|
| `request->method()` | `http_method` enum |
| `request->arg("name")` | `std::string` (form fields + query params) |
| `request->header("name")` | `optional<std::string>` |
| `request->body()` | `StringRef` (raw body — requires the `web_server_idf` override in this repo, or ESPHome's upstream fix #16517) |

## Known limitations

- **Handlers run on the httpd worker task, not the main loop.** Any
  `on_request` action that yields (`delay:`, `wait_until:`, `script.wait`,
  etc.) will crash the httpd task. Synchronous actions
  (`switch.turn_on`, lambdas, NVS writes, logger calls) are fine.
- **`POST` with no body returns `411 Length Required`** — `Content-Length: 0`
  must be present. Workaround: `curl -d ''`.
- **Exact path matching only.** No `/api/door/{id}` templates.
- **No per-endpoint auth override.** Auth comes from `web_server.auth` for all
  endpoints uniformly.
- **Tested on ESP32 IDF only.** Arduino backend not verified.

Upstream design discussion: [esphome discussion #3673](https://github.com/orgs/esphome/discussions/3673).

## License

MIT
