# Thagore Stdlib

This directory contains the standard library surface shipped with the rewrite lane.

## Layout

- `lib/http.tg`: HTTP client API (`http_get`, `http_post`, retry/backoff helpers, `http_get_body`/`http_post_body` to capture payloads).
- `lib/ws.tg`: WebSocket API (`ws_connect`, `ws_send`, `ws_close`, retry connect).
- `lib/db.tg`: database API (`db_connect`, `db_query`, `db_close`, retry helpers).
- `lib/time.tg`: timing helpers (`now_ms`, `sleep_ms`).
- `lib/gui.tg`: canvas/window helpers (draw point/line, present frame, fixed timestep).
- `lib/json.tg`: JSON parse/serialize helpers.
- `lib/env.tg`: environment and argv helpers.
- `lib/crypto.tg`: SHA-256 and HMAC-SHA256 helpers.
- `lib/map.tg`: key-value map contract functions.
- `std/core.tg`: core helper functions.
- `std/string.tg`: string utility helpers.
- `std/list.tg`: list utility helpers.

The APIs are intentionally small and opinionated to keep the runtime footprint low.
