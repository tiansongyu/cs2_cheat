# Fixed Web Radar

Clean-room React/Vite frontend for both the embedded CivetWeb service and the public Radar Relay. Its map catalogue and PNG files are also consumed by the optional native SDL overlay. The map is always north-up; player markers rotate instead of the map.

## Build and verify

Requires Node.js 20.19+ (or 22.12+).

```bash
npm ci
npm test
npm run build
node scripts/validate-bundle.mjs dist
```

`dist/` is the CivetWeb static root. Vite copies `public/maps/` to `dist/maps/` during the build.
The validator checks every manifest image, enforces safe map-relative paths and
1024×1024 PNGs, and verifies the required map attribution metadata. The whole
directory must remain next to the Windows executable because the local overlay
loads the same map files through WIC.

## Endpoints

- Page and assets are loaded from the current HTTP(S) origin.
- A page URL containing a non-empty `token` selects embedded mode. Only that query parameter is forwarded to the embedded WebSocket URL.
- A page URL without `token` probes `GET /api/v1/session`. A `404` is treated as an embedded link missing its access token; a `401`/`403` shows the Relay login screen.
- Relay login sends `{room, inviteToken}` to `POST /api/v1/session`. The server returns `{authenticated:true, room, expiresAtMs}` and owns the `Secure; HttpOnly` session cookie.
- Relay streaming always uses the query-free, same-origin `ws(s)://<current-host>/api/v1/stream`; browsers attach the session cookie during the handshake.
- Relay logout uses `DELETE /api/v1/session`. Neither invite credentials nor session credentials are stored in `localStorage` or a URL; credential-shaped Relay query parameters are removed from browser history.
- Session requests and WebSocket handshakes have explicit timeouts. Offline/online, foreground, BFCache, stale-frame, and superseded-connection transitions recover without accumulating sockets.
- The map manifest is conditionally revalidated from `/maps/manifest.json` and retried with bounded backoff. Missing manifests and map images also expose manual retry actions.
- `/maps/SOURCE.json` is revalidated with the manifest; malformed digests or a release mismatch prevent the map package from being accepted.
- The footer reports observed update rate, snapshot age and skipped sequence count.
- Sanitized newline-delimited v1 recordings can be loaded from the settings panel for local playback and scrubbing without a live game connection.

## WebSocket v1

The frontend accepts these server messages and ignores unknown additional fields:

```ts
type ServerMessage =
  | { v: 1; type: 'hello'; serverTimeMs: number; updateHz?: number }
  | { v: 1; type: 'snapshot'; seq: number; capturedAtMs: number; map: MapState;
      localPlayerId?: string | null; observedPlayerId?: string | null;
      localTeam?: Team; players: Player[]; bomb: Bomb }
  | { v: 1; type: 'error'; code: string; message: string };
```

Important compatibility rules:

- IDs are JSON strings so 64-bit identifiers are not rounded by JavaScript.
- Teams are `T`, `CT`, `SPEC`, or `NONE`.
- `Player.position` and `Player.yaw` may be `null` when tracking data is unavailable.
- Rich equipment uses `activeWeapon` and `inventory`; the earlier `weapons` array remains accepted.
- Bomb state is `unknown`, `carried`, `dropped`, `planted`, `defused`, or `exploded`.
- Bomb site is `a`, `b`, or `unknown`.
- Relative `explodeInSeconds` and `defuseInSeconds` values are preferred. They are anchored to browser `performance.now()` when the frame arrives. Absolute `explodeAtMs` and `defuseEndAtMs` are accepted as a fallback.

The exact TypeScript contract and runtime guards are in `src/types/protocol.ts`.

## Map manifest v1

```json
{
  "version": 1,
  "maps": [
    {
      "id": "de_example",
      "name": "Example",
      "origin": { "x": -3230, "y": 1713 },
      "scale": 5.0,
      "image": "/maps/de_example/radar.png",
      "levels": [
        { "id": "lower", "image": "/maps/de_example/lower.png", "minZ": -1000, "maxZ": 0 }
      ]
    }
  ]
}
```

World positions are normalized with:

```text
x = (world.x - origin.x) / (scale * 1024)
y = (origin.y - world.y) / (scale * 1024)
```

Manifest-provided image paths are authoritative. If `image` is absent, the UI falls back to `/maps/<id>/radar.png`. A missing definition or image produces an explicit in-page placeholder rather than a blank radar.
