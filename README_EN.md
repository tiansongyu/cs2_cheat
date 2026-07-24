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
| **Radar** | Standalone Radar Overlay, Enemy Position/Direction Display |
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

Radar and the memory-writing Anti-Flash option are disabled by default. Only opt
into Anti-Flash for explicit offline testing:

```powershell
.\external-cheat-base.exe --allow-memory-writes
```

Runtime diagnostics are written to `%TEMP%\cs2-esp.log`.

## 🛠️ Building

```bash
git clone https://github.com/tiansongyu/cs2_cheat.git
cd cs2_cheat
# Open external-cheat-base.sln with Visual Studio 2022
# Select Release | x64 and build

# Or run the CI-equivalent unit test and Windows cross-build with Docker
docker build --target artifacts -t cs2-cheat-build .
```

## 📜 License

MIT License

## 🙏 Acknowledgments

- [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper) - Offset source
- [libsdl-org/SDL](https://github.com/libsdl-org/SDL) - SDL2 graphics library
- [ocornut/imgui](https://github.com/ocornut/imgui) - ImGui interface library
