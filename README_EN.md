# CS2 External ESP

An external ESP overlay tool for CS2 based on SDL2 + ImGui.

[中文文档](README.md) | **English**

## 📥 Download

**[⬇️ Download Latest Release](https://github.com/tiansongyu/cs2_cheat/releases/latest)**

## ✨ Features

### ✅ Implemented Features

| Feature | Description |
|---------|-------------|
| 🔲 **Box ESP** | Display enemy positions with customizable colors |
| ❤️ **Health Bar** | Real-time enemy health display (left side of box) |
| 📏 **Distance Display** | Show distance to enemies in meters with customizable colors |
| 📍 **Snaplines** | Lines from screen to enemies with customizable colors and origin position |
| ⚙️ **ImGui Menu** | Graphical interface for real-time settings adjustment |

### 🚧 TODO (Planned Features)

| Feature | Description |
|---------|-------------|
| 🎒 **Item Display** | Show enemy's current weapon/equipment |
| 🧭 **Enemy Direction** | Display enemy facing angle (detect if spotted) |
| 💥 **Flash Status** | Box color changes when enemy is flashed |
| 🧱 **Occlusion Indicator** | Visual hint when enemy is behind walls/obstacles |
| 📡 **Radar Display** | Mini-map radar showing all character positions in real-time |

## 🖼️ Screenshot

![Demo](https://github.com/tiansongyu/cs2_cheat/blob/main/img/hack.png)

## 📖 Usage Guide

### Requirements
- CS2 must be running in **Windowed Fullscreen** mode (not Fullscreen)
- Run this program as **Administrator**

### Hotkeys

| Key | Function |
|-----|----------|
| **F4** | Show/Hide settings menu |
| **F9** | Exit program |

### Quick Start
1. Launch CS2 and enter the game
2. Press **F4** to open the menu and adjust settings
3. **Press F4 to hide the menu before playing**, otherwise mouse clicks will be intercepted by the overlay
4. Press **F9** to exit

> ⚠️ **Important**: When the menu is visible, you can adjust settings. When hidden, click-through is enabled for normal gameplay.

## 🚀 Building from Source

### Prerequisites
- Windows 10/11
- Visual Studio 2022 or later
- CS2 game

### Build Steps

1. Clone the repository
```bash
git clone https://github.com/tiansongyu/cs2_cheat.git
cd cs2_cheat
```

2. Open `external-cheat-base.sln` with Visual Studio

3. Select **Release | x64** configuration and build

4. The compiled executable will be located at `x64/Release/external-cheat-base.exe`

## 🔄 Updating Offsets

CS2 game updates require memory offset updates:

Offset source: [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper)

The project includes automated GitHub Actions workflow to update offsets automatically.

## 📁 Project Structure

```
cs2_cheat/
├── external-cheat-base/
│   ├── src/
│   │   ├── core/
│   │   │   ├── memory/      # Memory reading module
│   │   │   └── renderer/    # SDL2 renderer
│   │   ├── features/
│   │   │   ├── esp.cpp/hpp  # ESP feature implementation
│   │   │   └── menu.hpp     # ImGui menu
│   │   ├── utils/math/      # Math utilities
│   │   └── main.cpp         # Program entry point
│   ├── generated/           # Auto-generated offset headers
│   └── vendor/
│       ├── SDL2/            # SDL2 library
│       └── imgui/           # ImGui library
├── update_offset.bat        # Offset update script
└── docs/                    # Documentation
```

## 🛠️ Technical Details

### Architecture

- **External Memory Reading**: Uses Windows API `ReadProcessMemory` to read CS2 game memory
- **SDL2 Overlay**: Creates a transparent fullscreen window overlay on top of the game
- **ImGui Interface**: Provides an intuitive graphical menu for configuration
- **World-to-Screen**: Implements view matrix transformation for 3D to 2D projection

### Core Components

1. **Memory Module** (`src/core/memory/`)
   - Process and module enumeration
   - Memory reading/writing operations
   - Handle management

2. **Renderer Module** (`src/core/renderer/`)
   - SDL2 window creation and management
   - Transparent overlay rendering
   - ImGui integration

3. **ESP Features** (`src/features/`)
   - Entity list parsing
   - Box ESP rendering
   - Health bar display
   - Distance calculation
   - Snapline drawing

## ⚠️ Disclaimer

This project is for **educational and research purposes only**. Using this tool may violate the game's Terms of Service and result in account bans. The author is not responsible for any consequences resulting from the use of this tool.

**Use at your own risk.**

## 📜 License

MIT License

Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## 🙏 Acknowledgments

- [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper) - Offset source
- [libsdl-org/SDL](https://github.com/libsdl-org/SDL) - SDL2 graphics library
- [ocornut/imgui](https://github.com/ocornut/imgui) - ImGui interface library

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## 📞 Contact

- GitHub: [@tiansongyu](https://github.com/tiansongyu)
- Issues: [Report a bug](https://github.com/tiansongyu/cs2_cheat/issues)

---

**⭐ If you find this project helpful, please consider giving it a star!**

