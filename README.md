# CS2 External ESP

[![Build](https://github.com/tiansongyu/cs2_cheat/actions/workflows/build.yaml/badge.svg?branch=main)](https://github.com/tiansongyu/cs2_cheat/actions/workflows/build.yaml)
[![Offsets](https://github.com/tiansongyu/cs2_cheat/actions/workflows/update-files.yml/badge.svg?branch=main)](https://github.com/tiansongyu/cs2_cheat/actions/workflows/update-files.yml)
[![License](https://img.shields.io/github/license/tiansongyu/cs2_cheat)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)

一个基于 SDL2、ImGui 和 C++20 的 CS2 外部 ESP 教学项目，包含游戏覆盖层、固定地图 Radar 与可选的公网共享服务。

**中文** | [English](README_EN.md)

> [!WARNING]
> 本项目仅用于逆向工程和图形渲染学习。仅可在使用 `-insecure` 启动的离线环境中运行，禁止用于 VAC 服务器或任何在线游戏。使用者须自行承担不当使用造成的风险。

## 学习资料

| 主题 | 视频 | 文档 |
| --- | --- | --- |
| 内存结构基础 | [BV14szCBYErE](https://www.bilibili.com/video/BV14szCBYErE) | [CS2 内存结构](docs/CS2_Memory_Structure_Basics.md) |
| 源码与 CE 实战 | [BV1Jm6gBaEEd](https://www.bilibili.com/video/BV1Jm6gBaEEd) | [代码说明](docs/cheat_code.md) |
| 偏移量查找 | [BV1Lr6wBeEEF](https://www.bilibili.com/video/BV1Lr6wBeEEF) | [IDA 逆向](docs/IDA_Reverse_Engineering_Client_DLL.md) |
| 投影矩阵 | [BV1goFNzSEP3](https://www.bilibili.com/video/BV1goFNzSEP3) | [3D 投影原理](docs/3d_projection_explained.md) |
| 视图矩阵 | [BV1AtF5zhE5J](https://www.bilibili.com/video/BV1AtF5zhE5J) | [视图矩阵数学](docs/cs2_view_matrix_math.md) |
| 特征码与 IDA | [BV1yhcszwEJQ](https://www.bilibili.com/video/BV1yhcszwEJQ) | - |

## 功能

| 模块 | 功能 |
| --- | --- |
| ESP | 方框、骨骼、血条、武器、距离、射线、spotted 状态、手雷和掉落武器 |
| 辅助功能 | 自动瞄准、FOV 与平滑度调节、自动扳机、炸弹计时；防闪光需显式允许内存写入 |
| 本地换皮 | 普通枪械皮肤选择、磨损与种子、配置保存、参数读回、关闭时按实体身份恢复；需显式允许内存写入 |
| Radar | 北向固定全地图、玩家位置与朝向、楼层与置信度、队伍面板、装备、C4 状态和倒计时 |
| 共享与回放 | 本机 Web Radar、可信局域网访问、公网 Relay、脱敏 NDJSON 录制和浏览器回放 |
| 性能与诊断 | 4/10/20 Hz 自适应 Radar 采样、慢客户端背压、采样与渲染耗时、RPM、P95/P99 和截止时间统计 |

![程序截图](img/hack.png)

## 系统要求

- Windows 10 或 Windows 11（x64）
- Visual Studio 2022，使用 C++ 桌面开发工作负载
- Node.js 22.12 或更高版本（构建 Web Radar 时需要）
- CS2 使用 `-insecure` 参数和全屏窗口化模式

覆盖层仅支持完整位于单个显示器中的游戏窗口。跨显示器时会暂停渲染，窗口恢复后自动继续；独占全屏不受支持。程序启动时会请求管理员权限，并能在 CS2 重启后自动重新连接。

## 快速开始

1. 从 [Releases](https://github.com/tiansongyu/cs2_cheat/releases/latest) 下载并解压最新版本。
2. 使用 `-insecure` 参数启动 CS2，并进入离线模式。
3. 运行 `external-cheat-base.exe`，接受 Windows 管理员权限提示。
4. 按 `F4` 打开或隐藏设置菜单，按 `F9` 退出程序。

| 按键 | 默认操作 |
| --- | --- |
| `F4` | 显示或隐藏菜单 |
| `F9` | 退出程序 |
| `Shift` | 自动瞄准（启用后） |
| `F` | 自动扳机（启用后） |

Web Radar 和写内存的防闪光默认关闭。仅在明确进行离线测试时允许内存写入：

```powershell
.\external-cheat-base.exe --allow-memory-writes
```

运行日志位于 `%TEMP%\cs2-esp.log`。

## 本地枪械换皮

第一版保持外部程序架构，提供 34 种普通枪械、1,448 个武器与皮肤组合。
皮肤目录由固定版本的 Valve 游戏数据生成，包含兼容关系、磨损范围和模型选择。
刀、手套、贴纸、挂件和 StatTrak 不在第一版范围内。

1. 使用 `-insecure` 启动 CS2 并进入离线局，运行程序时加入 `--allow-memory-writes`。
2. 按 `F4` 打开 **Skins**，选枪械、搜索皮肤、调整磨损和图案种子。
3. 点击 **Apply & save**，勾选 **Enable local skins**。
4. 在 **Held firearms** 查看参数读回结果，在游戏画面中检查实际外观。
5. 如果材质尚未改变，切换武器或重新购买；**Reapply** 可重新请求应用。
6. 关闭开关会恢复仍由本地玩家持有且身份有效的武器；**Restore all** 同时清空保存的选择。

选择保存在 `%LOCALAPPDATA%\AegisCS2\skins-v1.ini`；重启后换皮开关保持关闭。
首次验收建议使用 AK-47 Redline（磨损 0.20、种子 42），再测试 AWP Dragon Lore 和
Glock-18 Fade，并检查关闭恢复、重生、拾取、切图和游戏重启。

参数写入后会读回校验，并请求客户端正常重建视觉数据。读回成功仅证明参数一致；
当前开发环境无法运行 CS2，实际材质刷新仍需 Windows 离线局验收。
实现没有调用游戏内部函数或执行远程代码，也不修改账户库存。
切枪后新建的第一人称模型会单独更新；反复覆盖参数的实体会在三次应用后停止写入，
直至点击 **Reapply** 或实体重建。失焦暂停写入但保留恢复记录。
恢复时跳过归属已改变、身份已失效或值已被游戏替换的字段。

偏移文件固定到 `cs2-dumper` 提交 `4116de000e085d62bbd42334c67b35bda37bda4f`
（生成日期 2026-10-06）；游戏更新后仍需重新匹配。
皮肤数据来源与哈希见 `external-cheat-base/src/features/skins/SOURCE.json`。
复核生成目录：

```bash
python3 scripts/generate_skin_catalog.py --check
```

代码实施步骤已完成：武器访问与身份校验、换皮核心与恢复、菜单与本地保存、回归测试与构建接入。
视觉刷新以实机验收为完成条件。

## Radar

### 本地与浏览器模式

1. 按 `F4` 打开菜单并进入 **World & Radar**。
2. 启用 **Local map overlay**，在游戏覆盖层显示固定全地图。
3. 如需浏览器视图，启用 **Web Radar** 并打开菜单中带 token 的 URL。默认服务地址为 `127.0.0.1:22006`。
4. 如需在可信局域网内访问，启用 LAN 监听，将 URL 中的 `127.0.0.1` 替换为本机私有 IPv4 地址，并完整保留 token。

姓名、Steam ID 和队伍信息可在发送前过滤。无人连接时采样频率降至 4 Hz，后台共享时为 10 Hz，活动连接时为 20 Hz。启用 **Record sanitized Radar snapshots** 后，脱敏录制保存在 `%LOCALAPPDATA%\AegisCS2\recordings`；可在 Web Radar 中载入 `.ndjson` 文件进行播放和拖动回放。

本地服务使用 HTTP/WS，token 不提供传输加密。不要进行端口映射或公网穿透，也不要在不可信网络中共享 URL。完整说明见 [Web Radar 设计文档](docs/web-radar-design.md)。

### 公网共享模式

公网 Relay 通过主程序主动建立的 `wss://` 出站连接共享 Radar，不暴露游戏电脑的入站端口。服务端使用独立的 Producer 和 Viewer 凭证、房间隔离、限流及仅保留最新帧的有界广播。

部署与使用请参阅：

- [公网 Relay 部署指南](deploy/public-relay/README.md)
- [公网 Relay 设计文档](docs/public-radar-relay-design.md)
- [公网 Radar 架构图](docs/public-radar-relay-architecture.html)

## 构建

### Visual Studio 2022

```powershell
git clone https://github.com/tiansongyu/cs2_cheat.git
cd cs2_cheat\web-radar
npm ci
npm test
npm run build
cd ..
```

使用 Visual Studio 2022 打开 `external-cheat-base.sln`，选择 `Release | x64` 后构建。MSBuild 会将已有的 `web-radar/dist` 复制到输出目录；发布程序时必须保留完整的 `web-radar/dist` 目录。

### Docker 可复现构建

```bash
docker build --target artifacts -t cs2-cheat-build .
```

该命令会运行前端、C++ 和 Go 测试，并完成 Windows 交叉编译。单独构建公网 Relay 生产镜像：

```bash
docker build -f radar-relay/Dockerfile -t cs2-radar-relay:local .
```

如需手动同步固定版本且经过 SHA-256 校验的地图素材：

```bash
python3 scripts/sync_web_radar_maps.py
```

## 项目结构

| 路径 | 内容 |
| --- | --- |
| `external-cheat-base/` | Windows C++ 主程序、覆盖层和内嵌 Radar 服务 |
| `web-radar/` | 浏览器 Radar 前端与地图资源 |
| `radar-relay/` | Go 公网 Relay 服务 |
| `deploy/public-relay/` | Caddy、Docker Compose 和生产部署工具 |
| `tests/` | C++ 回归测试 |
| `docs/` | 原理、架构与安全设计文档 |

## CI 与自动更新

顶部的 **Build** 徽章由 GitHub Actions 动态生成：绿色表示 `main` 分支最近一次构建成功，红色表示构建或测试失败。CI 会执行 C++ 回归测试、Web Radar 测试与构建、Go 测试与漏洞检查、Relay 镜像构建以及 Windows `Release | x64` 编译。

**Offsets** 工作流每小时检查一次 `cs2-dumper` 更新。偏移文件按确切提交 SHA 获取、验证并提交，验证通过后才触发完整构建。

## 许可证与第三方资产

项目代码以 [MIT License](LICENSE) 发布。

地图图片和 overview 坐标归 Valve 所有，并继续受 Valve 的版权和条款约束。分发时必须保留 `web-radar/dist/maps/NOTICE.txt` 和 `SOURCE.json`。Web Radar 是 clean-room 重写，不包含 `cs2_webradar` 的 GPL-3.0 源代码。

主要依赖和数据来源：[cs2-dumper](https://github.com/a2x/cs2-dumper)、[SDL](https://github.com/libsdl-org/SDL)、[Dear ImGui](https://github.com/ocornut/imgui)、[CivetWeb](https://github.com/civetweb/civetweb)、[Caddy](https://caddyserver.com/)、[gorilla/websocket](https://github.com/gorilla/websocket) 和 [awpy-data](https://github.com/pnxenopoulos/awpy-data)。
