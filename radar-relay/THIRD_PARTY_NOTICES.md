# Third-party notices

The Radar Relay includes the following dependencies:

| Module | Version | License | Purpose |
|---|---:|---|---|
| `github.com/gorilla/websocket` | `v1.5.4-0.20240701034025-d67f41855da4` | BSD-2-Clause | RFC 6455 WebSocket transport |
| `golang.org/x/net` | `v0.63.0` | BSD-3-Clause | Indirect dependency for WebSocket proxy connections |

The exact module graph is recorded in `go.mod`, `go.sum`, and
`vendor/modules.txt`. Dependency sources, licenses, and applicable patent grants are retained under
`vendor/` so release builds do not resolve unpinned network dependencies.

Gorilla WebSocket is pinned to upstream commit
`d67f41855da42d7bccd9ef050c49f7e54e783b95`, which fixes
[GO-2026-6278](https://pkg.go.dev/vuln/GO-2026-6278).
