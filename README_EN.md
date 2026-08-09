# CS2 External ESP

[![Build](https://github.com/tiansongyu/cs2_cheat/actions/workflows/build.yaml/badge.svg?branch=main)](https://github.com/tiansongyu/cs2_cheat/actions/workflows/build.yaml)
[![Offsets](https://github.com/tiansongyu/cs2_cheat/actions/workflows/update-files.yml/badge.svg?branch=main)](https://github.com/tiansongyu/cs2_cheat/actions/workflows/update-files.yml)
[![License](https://img.shields.io/github/license/tiansongyu/cs2_cheat)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)

An educational CS2 external ESP project built with SDL2, Dear ImGui, and C++20. It includes an in-game overlay, a fixed-map Radar, and an optional public sharing service.

[中文](README.md) | **English**

> [!WARNING]
> This project is intended only for learning reverse engineering and graphics rendering. Run it only in an offline environment launched with `-insecure`. Do not use it on VAC servers or in any online game. You are responsible for the consequences of misuse.

## Features

| Component | Capabilities |
| --- | --- |
| ESP | Boxes, skeletons, health, weapons, distance, snaplines, spotted state, grenades, and dropped weapons |
| Assistance | Aim assistance with FOV and smoothing, triggerbot, bomb timer; Anti-Flash requires explicit memory-write permission |
| Radar | Fixed north-up maps, player position and direction, floor and confidence state, team panels, equipment, and C4 state |
| Sharing and replay | Local Web Radar, trusted-LAN access, public Relay, sanitized NDJSON recording, and browser playback |
| Performance and diagnostics | Adaptive 4/10/20 Hz Radar sampling, slow-client backpressure, timing, RPM, P95/P99, and deadline metrics |

![Application screenshot](img/hack.png)

## Requirements

- Windows 10 or Windows 11 (x64)
- Visual Studio 2022 with the Desktop development with C++ workload
- Node.js 22.12 or newer when building Web Radar
- CS2 launched with `-insecure` in Windowed Fullscreen mode

The overlay supports a game window located entirely on one monitor. Rendering pauses while the window spans monitors and resumes when it moves back. Exclusive fullscreen is unsupported. The application requests administrator privileges at startup and reconnects automatically after CS2 restarts.

## Quick Start

1. Download and extract the latest package from [Releases](https://github.com/tiansongyu/cs2_cheat/releases/latest).
2. Launch CS2 with `-insecure` and enter an offline game.
3. Run `external-cheat-base.exe` and accept the Windows administrator prompt.
4. Press `F4` to show or hide the settings menu. Press `F9` to exit.

| Key | Default action |
| --- | --- |
| `F4` | Show or hide the menu |
| `F9` | Exit the application |
| `Shift` | Aim assistance when enabled |
| `F` | Triggerbot when enabled |

Web Radar and the memory-writing Anti-Flash option are disabled by default. Enable memory writes only for explicit offline testing:

```powershell
.\external-cheat-base.exe --allow-memory-writes
```

Runtime logs are written to `%TEMP%\cs2-esp.log`.

## Radar

### Local and browser modes

1. Press `F4` and open **World & Radar**.
2. Enable **Local map overlay** to display the fixed full map in the game overlay.
3. For a browser view, enable **Web Radar** and open the token-bearing URL shown in the menu. The default address is `127.0.0.1:22006`.
4. For access from a trusted LAN, enable LAN binding, replace `127.0.0.1` with the PC's private IPv4 address, and keep the full token in the URL.

Player names, Steam IDs, and team information can be filtered before transmission. Sampling runs at 4 Hz with no viewers, 10 Hz for background sharing, and 20 Hz with active viewers. When **Record sanitized Radar snapshots** is enabled, sanitized recordings are written to `%LOCALAPPDATA%\AegisCS2\recordings`; load a `.ndjson` file in Web Radar for playback and seeking.

The local service uses HTTP/WS, so its token does not encrypt transport. Do not port-forward it, expose it through a public tunnel, or share it over an untrusted network. See the [Web Radar design](docs/web-radar-design.md) for architecture and security details.

### Public sharing

The public Relay shares Radar data through an outbound `wss://` connection from the application, without exposing an inbound port on the gaming PC. It provides separate Producer and Viewer credentials, room isolation, rate limiting, and bounded latest-frame broadcasting.

Deployment and design references:

- [Public Relay deployment guide](deploy/public-relay/README.md)
- [Public Relay design](docs/public-radar-relay-design.md)
- [Public Radar architecture](docs/public-radar-relay-architecture.html)

## Building

### Visual Studio 2022

```powershell
git clone https://github.com/tiansongyu/cs2_cheat.git
cd cs2_cheat\web-radar
npm ci
npm test
npm run build
cd ..
```

Open `external-cheat-base.sln` in Visual Studio 2022, select `Release | x64`, and build. MSBuild copies an existing `web-radar/dist` directory into the binary output. Keep the complete `web-radar/dist` directory when distributing the application.

### Reproducible Docker build

```bash
docker build --target artifacts -t cs2-cheat-build .
```

This runs the frontend, C++, and Go tests before cross-compiling the Windows application. Build the production public Relay image separately:

```bash
docker build -f radar-relay/Dockerfile -t cs2-radar-relay:local .
```

To synchronize the pinned, SHA-256-verified map assets manually:

```bash
python3 scripts/sync_web_radar_maps.py
```

## Project Structure

| Path | Purpose |
| --- | --- |
| `external-cheat-base/` | Windows C++ application, overlay, and embedded Radar service |
| `web-radar/` | Browser Radar frontend and map assets |
| `radar-relay/` | Go public Relay service |
| `deploy/public-relay/` | Caddy, Docker Compose, and production deployment tools |
| `tests/` | C++ regression tests |
| `docs/` | Technical, architecture, and security documentation |

## CI and Offset Updates

The **Build** badge at the top is generated dynamically by GitHub Actions. Green means the latest `main` build passed; red means a build or test failed. CI runs the C++ regression suite, Web Radar tests and build, Go tests and vulnerability checks, Relay image builds, and the Windows `Release | x64` build.

The **Offsets** workflow checks `cs2-dumper` hourly. It fetches generated headers at an exact commit SHA, validates and commits them, and then triggers the complete build.

## Learning Resources

| Topic | Video | Documentation |
| --- | --- | --- |
| Memory structures | [BV14szCBYErE](https://www.bilibili.com/video/BV14szCBYErE) | [CS2 memory structures](docs/CS2_Memory_Structure_Basics.md) |
| Source and CE walkthrough | [BV1Jm6gBaEEd](https://www.bilibili.com/video/BV1Jm6gBaEEd) | [Code walkthrough](docs/cheat_code.md) |
| Finding offsets | [BV1Lr6wBeEEF](https://www.bilibili.com/video/BV1Lr6wBeEEF) | [IDA reverse engineering](docs/IDA_Reverse_Engineering_Client_DLL.md) |
| Projection matrices | [BV1goFNzSEP3](https://www.bilibili.com/video/BV1goFNzSEP3) | [3D projection](docs/3d_projection_explained.md) |
| View matrices | [BV1AtF5zhE5J](https://www.bilibili.com/video/BV1AtF5zhE5J) | [View-matrix math](docs/cs2_view_matrix_math.md) |
| Signatures and IDA | [BV1yhcszwEJQ](https://www.bilibili.com/video/BV1yhcszwEJQ) | - |

## License and Third-Party Assets

Project code is released under the [MIT License](LICENSE).

Radar images and overview coordinates are Valve property and remain subject to Valve's copyright and terms. Every distribution must retain `web-radar/dist/maps/NOTICE.txt` and `SOURCE.json`. Web Radar is a clean-room reimplementation and contains no GPL-3.0 source from `cs2_webradar`.

Primary dependencies and data sources: [cs2-dumper](https://github.com/a2x/cs2-dumper), [SDL](https://github.com/libsdl-org/SDL), [Dear ImGui](https://github.com/ocornut/imgui), [CivetWeb](https://github.com/civetweb/civetweb), [Caddy](https://caddyserver.com/), [gorilla/websocket](https://github.com/gorilla/websocket), and [awpy-data](https://github.com/pnxenopoulos/awpy-data).
