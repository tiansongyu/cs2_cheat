# Embedded Web Radar service

`WebRadarService` owns a CivetWeb HTTP/WebSocket server and is safe to start
and stop repeatedly. Static files are served from `WebRadarConfig::documentRoot`.

Endpoints:

- `GET /api/v1/status` returns service state, viewer count, published/sent/
  replaced frame counters, bytes and maximum send latency. It never returns
  the bearer token.
- `WS /api/v1/stream?token=<token>` streams complete JSON snapshots.

WebSocket viewers are receive-only. Sending an application data frame closes
the connection. Each viewer has an independent writer with one replaceable
pending snapshot; calls to `publish()` never perform socket I/O and slow
viewers skip obsolete snapshots.

The application lowers Web-Radar-only sampling to 4 Hz while no viewer is
connected, restores 20 Hz for foreground viewers, and uses 10 Hz for explicit
background sharing. Local overlay and recording demand remain foreground-only.

## Build integration

Add these sources:

- `vendor/civetweb/src/civetweb.c` (compile as C, `/TC` with MSVC)
- `vendor/civetweb/src/CivetServer.cpp`
- `external-cheat-base/src/features/web_radar/web_radar_service.cpp`

Add these include directories:

- `vendor/civetweb/include`
- `external-cheat-base/src/features/web_radar`

Compile CivetWeb with:

- `USE_WEBSOCKET`
- `USE_IPV6`
- `NO_SSL`
- `NO_CGI`

`NO_SSL` is intentional: this embedded service provides local HTTP/WS. A
future Internet sharing mode should use an outbound authenticated WSS relay
instead of exposing this listener directly.

On Windows, link `ws2_32.lib` (or `-lws2_32` with MinGW). The service uses the
C++ standard threading library. MinGW builds therefore need a toolchain with
working `std::thread` support (for example the POSIX thread variant), which is
also required by the existing application.

## Configuration constraints

- Default bind: `127.0.0.1:22006`.
- Token: 16-128 URL-safe characters. Use a cryptographically random token
  with at least 128 bits of entropy.
- Worker threads: at least `maxViewers + 4`.
- The document root must already exist when `start()` is called.

Do not put the bearer token in logs. A caller should create a new service (or
stop and replace its owner) when bind, port, document root, or token changes.
