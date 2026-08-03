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
| **Web Radar** | Browser-based fixed north-up map, all-player position/direction and equipment, CT/T panels, C4 state and timers, reconnect support |
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

### Fixed-map Web Radar

Web Radar is a standalone browser page, not a circular overlay radar. It keeps the complete map north-up while rotating only player direction markers, and supports multi-level maps, team panels, and C4 timing.

1. Press **F4** and enable the embedded service under the Web Radar settings.
2. Open the token-bearing URL shown in the menu. The default endpoint is `127.0.0.1:22006`, so only the same computer can access it.
3. To view it on a phone or tablet on the same trusted LAN, explicitly enable LAN binding and replace `127.0.0.1` in the copied URL with this PC's private LAN IPv4 address. Keep the token in the URL.

The LAN token prevents access by clients that do not have the link; it does not add TLS to HTTP/WS. Never port-forward the service, expose it through a public tunnel, or share it over an untrusted Wi-Fi network. Restrict any Windows Firewall rule to private networks. See the [Web Radar design](docs/web-radar-design.md) for the architecture, security boundaries, and optimization roadmap.

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
- [awpy-data](https://github.com/pnxenopoulos/awpy-data) - map synchronization source and overview-data pipeline (see the bundled NOTICE for asset rights)
