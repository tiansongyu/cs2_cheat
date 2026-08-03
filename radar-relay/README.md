# Radar Relay

`radar-relay` is the single-node public deployment boundary for Web Radar. A
Windows producer makes an outbound authenticated WebSocket connection; viewers
exchange a room invite for a short-lived, same-origin session cookie. The
service keeps only the newest complete snapshot in memory and never writes
snapshot data or plaintext tokens to disk or logs.

## Data flow

```text
CS2 producer -- WSS + producer token --> room (latest frame, 3 s TTL)
                                                  |
browser -- invite --> HttpOnly session -- WSS ----+
```

The relay is deliberately a single-node, in-memory service. Restarting it
invalidates all browser sessions and retained frames. Run one replica unless a
future shared session/room backend is implemented; a generic load balancer must
not distribute one room across replicas.

## API

| Endpoint | Authentication | Behavior |
|---|---|---|
| `GET /healthz` | none | Process liveness |
| `GET /readyz` | none | Accepting new sessions/connections |
| `GET /metrics` | none | Optional internal aggregate Prometheus metrics; disabled by default |
| `POST /api/v1/session` | exact configured `Origin`; JSON invite | Creates session cookie |
| `GET /api/v1/session` | session cookie | Returns `{authenticated,room,expiresAtMs}` |
| `DELETE /api/v1/session` | cookie + exact `Origin` | Revokes current session |
| `GET /api/v1/publish` | bearer token + `X-Radar-Room` | Producer WebSocket |
| `GET /api/v1/stream` | cookie + exact `Origin` | Receive-only viewer WebSocket |

All Relay API endpoints reject query strings. Credentials are carried only in
the producer header, the one-time session JSON body, or the host-only cookie;
they never belong in a public URL.

Producer handshake headers:

```http
Authorization: Bearer <producer-token>
X-Radar-Room: match_one
```

Only UTF-8 JSON text frames with `v: 1`, `type: "snapshot"`,
`protocolVersion: 1`, and the required full-snapshot fields are accepted. The
relay does not transform the existing Web Radar JSON protocol. Binary frames,
oversized frames, excess publish frequency, and non-snapshot messages close the
producer connection. A room permits one producer. Each viewer has a one-frame
replaceable queue, so a slow viewer skips obsolete frames instead of applying
backpressure to the game process.

Viewer WebSockets reject application data. Sessions are `Secure`, `HttpOnly`,
`SameSite=Strict`, host-only `__Host-` cookies. Active WebSockets close with
code `4401` when their session expires. Browser session mutations and viewer
upgrades use exact string Origin matching; the GET session probe does not
require an Origin because browsers normally omit it on same-origin GET.

Viewer upgrades negotiate `permessage-deflate` with
`server_no_context_takeover` and `client_no_context_takeover` when the browser
offers it. One immutable Gorilla `PreparedMessage` is shared by every viewer of
a snapshot, so framing and BestSpeed deflate work are performed once rather
than once per connection. Clients which do not offer the extension continue to
receive ordinary uncompressed WebSocket text messages.

## Create configuration and secrets

Use the generator rather than inventing tokens:

```bash
docker build -f radar-relay/Dockerfile -t radar-relay .
docker run --rm -v "$PWD:/work" -w /work radar-relay \
  init-config -out relay-config.json \
  -origin https://radar.example.com -room match_one -listen 0.0.0.0:8080 \
  -static-dir /srv/web-radar
```

`init-config` creates the config with mode `0600`, stores only SHA-256 token
digests, and prints the producer and invite tokens once. Put those plaintext
tokens into separate secret managers. Do not use the opaque example hashes in
`config.example.json`; their corresponding tokens are intentionally unknown.

Generate an additional token or hash an existing high-entropy token without
putting it in shell history:

```bash
docker run --rm radar-relay token
printf '%s\n' "$RADAR_TOKEN" | docker run --rm -i radar-relay hash-token
```

An invite can be rotated without immediately removing the previous one by
placing both digests in `inviteTokenSha256` (maximum 16). Remove the old digest
after the distribution window. Existing sessions remain valid until their
configured expiry or relay restart.

## Production configuration

Important fields:

- `publicOrigin`: one browser-canonical HTTPS origin, with no trailing slash.
  DNS names must be lowercase ASCII/punycode with no trailing dot, and the
  default `:443` port must be omitted, matching browser Origin serialization.
- `rooms[].id`: 3–64 characters from `A-Z a-z 0-9 _ -`.
- `producerTokenSha256` / `inviteTokenSha256`: SHA-256 hex digests, never
  plaintext secrets. Every digest must be globally unique across rooms and
  roles, preventing one credential from accidentally gaining multiple powers.
- `snapshotTTLMillis`: retained latest-frame lifetime; default and recommended
  value is 3000. TTL starts when the relay accepts a frame.
- `maxSnapshotAgeMillis` and `maxFutureSkewMillis`: compare producer Unix
  `capturedAtMs` with relay time and reject replayed or implausibly future
  snapshots. Recommended values are 10000 and 30000; synchronize both machines
  with NTP.
- `maxSnapshotBytes`, `maxPublishHz`, `publishBurst`: producer resource bounds.
- `maxViewers`: per-room WebSocket cap.
- `maxViewersPerSession`: cap for one leaked/shared cookie; default 2, valid
  range 1–8. Deleting or replacing a session immediately closes all of its
  active viewers with code `4401`.
- `loginAttemptsPerMinute`, `loginBurst`: token-bucket login limit per client IP.
- `trustedProxyCIDRs`: peers allowed to supply a single `X-Real-IP`; all other
  clients are limited by their socket address. The proxy must overwrite this
  header.
- `staticDir`: optional Web Radar production `dist` directory. Unknown frontend
  routes fall back to `index.html`; hidden paths, symlink escapes, and unknown
  `/api/` routes are not served.
- `tlsCertFile` and `tlsKeyFile`: optional direct TLS. Configure both or neither.
- `enableMetrics`: exposes aggregate Prometheus text metrics at `/metrics` on
  the Relay listener. It is `false` by default. Enable it only on a private
  backend network and make the public reverse proxy return `404` for
  `/metrics`. Metrics contain no room, token, or client-IP labels.

Plain HTTP is accepted on the backend only so TLS can terminate at a same-host
reverse proxy. The example binds the container port, which must remain on a
private Docker network and must not be published directly. The public reverse
proxy is the only component that should expose TCP 443.

The proxy must preserve `Host`, `Origin`, `Cookie`, `Authorization`, and
`X-Radar-Room`, pass WebSocket `Upgrade`/`Connection`, overwrite `X-Real-IP`,
disable proxy buffering for both WebSocket paths, and avoid logging request
headers, bodies, or query strings. There is no token in the normal viewer URL.

Static Vite assets with a filename hash receive a one-year `immutable` cache
policy. `index.html` remains `no-store`, and missing `/assets`, `/maps`, or
file-extension paths return `404` instead of incorrectly returning the SPA
shell. These error responses are `no-store`; extensionless frontend routes
still fall back to `index.html`.

## Run

```bash
docker run --rm --read-only --cap-drop ALL --security-opt no-new-privileges \
  --user 65532:65532 \
  --mount type=bind,src="$PWD/relay-config.json",dst=/run/secrets/relay-config.json,readonly \
  --network radar-private radar-relay
```

The production image builds and tests Web Radar with Node 22 and embeds its
`dist` tree at `/srv/web-radar`. It is `scratch`, contains only the static Go
binary, frontend assets, and license notices, runs as UID/GID `65532`, has no
shell or package manager, and includes an internal health checker:

```bash
/radar-relay healthcheck -url http://127.0.0.1:8080/healthz
```

Validate configuration during deployment without printing token hashes or
other credentials. Supplying `-origin` also catches a mismatch between the
Relay config and reverse-proxy hostname before the service starts:

```bash
/radar-relay check-config -config /run/secrets/relay-config.json \
  -origin https://radar.example.com
```

SIGTERM first makes readiness fail, gracefully stops HTTP, closes producer and
viewer WebSockets with a server-shutdown close frame, clears sessions, and exits
within `shutdownTimeoutSeconds`.

## Test

The module targets the pinned Go 1.26 toolchain and vendors its only external
dependency:

```bash
docker build -f radar-relay/Dockerfile --target build -t radar-relay-test .
```

For a local Go 1.26.5 installation:

```bash
cd radar-relay
go test -mod=vendor -race ./...
go vet -mod=vendor ./...
```

Tests cover authentication, strict Origin handling, secure cookies, session
expiry, single-producer enforcement, latest-frame delivery/TTL, viewer caps,
receive-only viewers, schema rejection, frame frequency limiting, IP login
limiting, graceful connection closure primitives, and the secret/config CLI.
