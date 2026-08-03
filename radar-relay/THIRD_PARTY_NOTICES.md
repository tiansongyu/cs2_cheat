# Third-party notices

The Radar Relay directly depends on:

| Module | Version | License | Purpose |
|---|---:|---|---|
| `github.com/gorilla/websocket` | `v1.5.3` | BSD-2-Clause | RFC 6455 WebSocket transport |

The exact module graph is recorded in `go.mod`, `go.sum`, and
`vendor/modules.txt`. The dependency source and its license are vendored at
`vendor/github.com/gorilla/websocket/` so release builds do not resolve an
unpinned network dependency.
