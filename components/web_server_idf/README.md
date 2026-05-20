# web_server_idf (override)

Drop-in override of ESPHome's built-in `web_server_idf` component. Identical to
upstream except for three additive changes:

- **PUT / DELETE / PATCH registration.** Upstream only registers GET / POST /
  OPTIONS, so any other verb is rejected with `405` by the IDF httpd before
  reaching `AsyncWebHandler::canHandle()`. This component registers PUT and
  PATCH against the body-reading POST path, and DELETE against the no-body
  GET path (so clients that omit `Content-Length` are not rejected with `411`).

- **`AsyncWebServerRequest::body()`** — exposes the raw request body for
  content types other than `application/x-www-form-urlencoded` and
  `multipart/form-data`. Upstream silently drops these bodies. Returns a
  `StringRef` view into the request buffer (no allocation).

- **Expanded HTTP status code map.** Adds 201, 204, 400, 403, 405, 500, 507
  to `init_response_()`. Upstream silently maps these to 500.

Scope: ESP32 IDF framework only. The Arduino backend (`ESPAsyncWebServer`)
already supports all of the above.

## Usage

```yaml
external_components:
  - source: github://iezhkv/esphome-components
    components: [web_server_idf]
    refresh: 1h

web_server:
  port: 80
```

That's it — `web_server_idf` is loaded automatically by `web_server` /
`web_server_base`; this override replaces it transparently.

## Compatibility

Tracks upstream `dev`. If upstream merges
[#16517](https://github.com/esphome/esphome/pull/16517), this override becomes
unnecessary.

## License

MIT
