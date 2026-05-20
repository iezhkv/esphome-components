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

## License

MIT — see [LICENSE](LICENSE).
