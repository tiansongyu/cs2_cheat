# CS2 External ESP

[![Build Status](https://github.com/tiansongyu/cs2_cheat/actions/workflows/build.yaml/badge.svg)](https://github.com/tiansongyu/cs2_cheat/actions/workflows/build.yaml)
[![Auto Update](https://github.com/tiansongyu/cs2_cheat/actions/workflows/update-files.yml/badge.svg)](https://github.com/tiansongyu/cs2_cheat/actions/workflows/update-files.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A CS2 external ESP project for learning game reverse engineering, built with SDL2 + ImGui.

[中文文档](README.md) | **English**

> 🎓 **This project is for educational purposes only**, designed to help understand game memory structures, process communication, and graphics rendering techniques.

## ⚠️ Important Notice

**This project is strictly for offline learning:**
- ✅ Must launch CS2 with `-insecure` parameter for offline mode
- ❌ **DO NOT use on VAC servers or any online mode**
- ❌ Online usage will result in VAC ban, use at your own risk

## 🔄 Auto Offset Updates

GitHub Actions checks for CS2 updates every hour. Generated headers are downloaded
at an exact `cs2-dumper` commit SHA, committed, and only then built so one release
cannot mix offsets from different revisions.

## 📥 Download

**[⬇️ Download Latest Release](https://github.com/tiansongyu/cs2_cheat/releases/latest)**

## 📺 Video Tutorials (Chinese)

Step-by-step tutorials on game reverse engineering:

| Lesson | Link |
|--------|------|
| Lesson 1: Memory Structure Basics | [BV14szCBYErE](https://www.bilibili.com/video/BV14szCBYErE) |
| Lesson 2: Source Code + CE Hands-on | [BV1Jm6gBaEEd](https://www.bilibili.com/video/BV1Jm6gBaEEd) |
| Lesson 3: Finding Offsets from Scratch | [BV1Lr6wBeEEF](https://www.bilibili.com/video/BV1Lr6wBeEEF) |
| Lesson 4: Projection Matrix Overview | [BV1goFNzSEP3](https://www.bilibili.com/video/BV1goFNzSEP3) |
| Lesson 5: View Matrix Deep Dive | [BV1AtF5zhE5J](https://www.bilibili.com/video/BV1AtF5zhE5J) |
| Lesson 6: Signature Scanning & IDA Reverse Engineering | [BV1yhcszwEJQ](https://www.bilibili.com/video/BV1yhcszwEJQ) |

## ✨ Features

| Category | Features |
|----------|----------|
| **ESP** | Box ESP, Skeleton ESP, Health Bar, Weapon Display, Distance, Snaplines, CS2 spotted-state indicator |
| **Aimbot** | Auto Aim, FOV Adjustment, Smoothness |
| **Triggerbot** | Auto Fire, Delay Settings |
| **Web Radar** | Browser-based fixed north-up map, all-player position/direction and equipment, CT/T panels, C4 state and timers, local CivetWeb, and a shared public relay |
| **Misc** | Anti-Flash, Bomb Timer, Grenade ESP, Dropped Weapon ESP |
| **UI** | ImGui Menu, Real-time Settings Adjustment |

## 🖼️ Screenshot

![Demo](https://github.com/tiansongyu/cs2_cheat/blob/main/img/hack.png)

## 📖 Usage

### Requirements
- Windows 10/11
- CS2 in **Windowed Fullscreen** mode
- Keep the game client entirely on one monitor; the overlay pauses while it spans monitors and resumes when moved back
- Launch CS2 with `-insecure` parameter

> Exclusive fullscreen cannot be reliably covered by an external top-level window and is unsupported. The program reconnects automatically after CS2 restarts.

### Hotkeys

| Key | Function |
|-----|----------|
| **F4** | Show/Hide Menu |
| **F9** | Exit Program |
| **Shift** | Aimbot (when enabled) |
| **F** | Triggerbot (when enabled) |

### Quick Start
1. Launch CS2 with `-insecure` for offline mode
2. Run the program, press **F4** to open menu
3. Press **F4** to hide menu for normal gameplay

### Fixed-map Radar

Radar no longer uses a player-centred circular or rotating view. The local SDL
overlay, embedded CivetWeb page, and public Relay share one north-up full-map
catalogue, calibration model, and complete player snapshot. The map stays fixed;
all available T/CT players are placed in world coordinates and living-player
markers rotate with yaw. Death, dormant, floor, and C4 states have distinct marks.

1. Press **F4**, open **World & Radar**, and enable **Local map overlay** to
   render the full fixed map in the game overlay. Position, size, marker size,
   and player names are configurable.
2. For a browser view, enable **Web Radar** and open the token-bearing URL shown
   in the menu. The default endpoint is `127.0.0.1:22006`, so only the same
   computer can access it.
3. To view it on a phone or tablet on the same trusted LAN, explicitly enable LAN binding and replace `127.0.0.1` in the copied URL with this PC's private LAN IPv4 address. Keep the token in the URL.

The local overlay loads the exact same 1024×1024 PNG files from
`web-radar/dist/maps` next to the executable. Always distribute the complete
`web-radar/dist` directory with the program.

The LAN token prevents access by clients that do not have the link; it does not add TLS to HTTP/WS. Never port-forward the service, expose it through a public tunnel, or share it over an untrusted Wi-Fi network. Restrict any Windows Firewall rule to private networks. See the [Web Radar design](docs/web-radar-design.md) for the architecture, security boundaries, and optimization roadmap.

### Production public shared Radar

Public mode is an independent path and never exposes CivetWeb or an inbound port
on the game PC. The Windows process makes an outbound-only `wss://` connection;
Caddy provides automatic HTTPS at the server, and the Go relay provides room
isolation, separate Producer/Viewer credentials, short-lived HttpOnly sessions,
rate limits, and bounded latest-frame fan-out. Viewers share one prepared,
optionally compressed frame; the browser handles weak-network/offline/background
recovery, while aggregate metrics and deployment verification remain internal.

1. Follow the [production deployment guide](deploy/public-relay/README.md) on a
   Linux server with a domain, generate the room credentials, and start Caddy +
   Relay.
2. In the Windows **Public Relay** settings, enter
   `wss://your-domain/api/v1/publish`, the room, and the Producer token, then
   enable it.
3. Viewers open `https://your-domain/` and enter the same room plus the separate
   Viewer invite token. Never give the Producer token to viewers.

The server does not retain snapshot history; a restart clears sessions and the
latest frame. Local and public Radar can be enabled independently, and public
mode does not require the embedded service. See the
[public Relay design](docs/public-radar-relay-design.md) for the protocol,
security model, and scaling boundary.
The [public Radar architecture diagram](docs/public-radar-relay-architecture.html)
shows the ports, trust boundaries, and end-to-end data flow.

Web Radar and the memory-writing Anti-Flash option are disabled by default. Only opt
into Anti-Flash for explicit offline testing:

```powershell
.\external-cheat-base.exe --allow-memory-writes
```

Runtime diagnostics are written to `%TEMP%\cs2-esp.log`.

## 🛠️ Building

```bash
git clone https://github.com/tiansongyu/cs2_cheat.git
cd cs2_cheat
# Build the fixed-map Web Radar assets first (Node.js 22.12+)
cd web-radar
npm ci
npm test
npm run build
cd ..

# Open external-cheat-base.sln with Visual Studio 2022
# Select Release | x64 and build

# Or run all frontend/backend tests and the Windows cross-build with Docker
docker build --target artifacts -t cs2-cheat-build .

# Build the production public Relay image, including the frontend bundle
docker build -f radar-relay/Dockerfile -t cs2-radar-relay:local .
```

MSBuild copies an existing `web-radar/dist` into `web-radar/dist` under the binary output directory. CI and Docker build the frontend automatically; run the npm commands above before a local Visual Studio build. The map synchronization script downloads a pinned, SHA-256-verified asset set:

```bash
python3 scripts/sync_web_radar_maps.py
```

Radar images and overview coordinates are Valve property and remain subject to Valve's copyright and terms; keep `web-radar/dist/maps/NOTICE.txt` and `SOURCE.json` in every distribution. The Web Radar code is a clean-room reimplementation of the requested feature set and does not contain GPL-3.0 source from `cs2_webradar`.

## 📜 License

MIT License

## 🙏 Acknowledgments

- [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper) - Offset source
- [libsdl-org/SDL](https://github.com/libsdl-org/SDL) - SDL2 graphics library
- [ocornut/imgui](https://github.com/ocornut/imgui) - ImGui interface library
- [CivetWeb](https://github.com/civetweb/civetweb) - MIT-licensed embedded HTTP/WebSocket server
- [Caddy](https://caddyserver.com/) - public edge, automatic HTTPS, and WebSocket reverse proxy
- [gorilla/websocket](https://github.com/gorilla/websocket) - BSD-2-Clause Relay WebSocket transport
- [awpy-data](https://github.com/pnxenopoulos/awpy-data) - map synchronization source and overview-data pipeline (see the bundled NOTICE for asset rights)
