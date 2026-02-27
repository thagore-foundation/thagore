# Thagore Stdlib

This directory contains the standard library surface shipped with the rewrite lane.

## Layout

- `lib/http.tg`: minimal HTTP client API (`http_get`, `http_post`).
- `lib/ws.tg`: minimal WebSocket API (`ws_connect`, `ws_send`, `ws_close`).
- `lib/db.tg`: minimal database client API (`db_connect`, `db_query`, `db_close`).
- `lib/time.tg`: timing helpers (`now_ms`, `sleep_ms`).
- `lib/map.tg`: key-value map contract functions.
- `std/core.tg`: core helper functions.
- `std/string.tg`: string utility helpers.
- `std/list.tg`: list utility helpers.

The APIs are intentionally small and opinionated to keep the runtime footprint low.
