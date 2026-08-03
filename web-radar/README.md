# Fixed Web Radar

Clean-room React/Vite frontend for the embedded CivetWeb radar service. The map is always north-up; player markers rotate instead of the map.

## Build and verify

Requires Node.js 20.19+ (or 22.12+).

```bash
npm ci
npm test
npm run build
```

`dist/` is the CivetWeb static root. Vite copies `public/maps/` to `dist/maps/` during the build.

## Endpoints

- Page and assets are loaded from the current HTTP(S) origin.
- The stream URL is derived as `ws(s)://<current-host>/api/v1/stream`.
- Only the current page's `token` query parameter is forwarded to the WebSocket URL.
- The map manifest is loaded once from `/maps/manifest.json`.

## WebSocket v1

The frontend accepts these server messages and ignores unknown additional fields:

```ts
type ServerMessage =
  | { v: 1; type: 'hello'; serverTimeMs: number; updateHz?: number }
  | { v: 1; type: 'snapshot'; seq: number; capturedAtMs: number; map: MapState;
      localPlayerId?: string | null; players: Player[]; bomb: Bomb }
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
