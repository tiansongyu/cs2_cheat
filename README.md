# CS2 External ESP

[![Build Status](https://github.com/tiansongyu/cs2_cheat/actions/workflows/build.yaml/badge.svg)](https://github.com/tiansongyu/cs2_cheat/actions/workflows/build.yaml)
[![Auto Update](https://github.com/tiansongyu/cs2_cheat/actions/workflows/update-files.yml/badge.svg)](https://github.com/tiansongyu/cs2_cheat/actions/workflows/update-files.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

一个用于学习游戏逆向工程的 CS2 外部 ESP 项目，基于 SDL2 + ImGui 实现。

**中文** | [English](README_EN.md)

> 🎓 **本项目仅用于学习逆向工程知识**，帮助理解游戏内存结构、进程通信、图形渲染等技术原理。

## ⚠️ 重要声明

**本项目仅限离线模式学习使用：**
- ✅ 必须使用 `-insecure` 参数启动 CS2 离线模式
- ❌ **禁止在 VAC 服务器或任何在线模式中使用**
- ❌ 在线使用将导致 VAC 封禁，后果自负

## 🔄 自动偏移更新

本项目通过 GitHub Actions **每小时自动检测** CS2 游戏更新。偏移头文件按
`cs2-dumper` 的确切提交 SHA 下载并提交后再构建，避免同一次发布混入不同版本的偏移。

## 📥 下载

**[⬇️ 下载最新版本](https://github.com/tiansongyu/cs2_cheat/releases/latest)**

## 📺 视频教程

配套 B 站教程，从零开始学习游戏逆向：

| 课程 | 链接 |
|------|------|
| 第一课：内存结构基础 | [BV14szCBYErE](https://www.bilibili.com/video/BV14szCBYErE) |
| 第二课：源码 + CE 实战演示 | [BV1Jm6gBaEEd](https://www.bilibili.com/video/BV1Jm6gBaEEd) |
| 第三课：零基础查找偏移量 | [BV1Lr6wBeEEF](https://www.bilibili.com/video/BV1Lr6wBeEEF) |
| 第四课：投影矩阵原理概述 | [BV1goFNzSEP3](https://www.bilibili.com/video/BV1goFNzSEP3) |
| 第五课：视图矩阵深度解析 | [BV1AtF5zhE5J](https://www.bilibili.com/video/BV1AtF5zhE5J) |
| 第六课：手动构造特征码及原理讲解IDA逆向使用教程 | [BV1yhcszwEJQ](https://www.bilibili.com/video/BV1yhcszwEJQ) |

## ✨ 功能特性

| 分类 | 功能 |
|------|------|
| **ESP** | 方框透视、骨骼透视、血条、武器显示、距离、射线、CS2 spotted 状态提示 |
| **Aimbot** | 自动瞄准、FOV 调节、平滑度|
| **Triggerbot** | 自动扳机、延迟设置 |
| **Web Radar** | 浏览器固定北向全地图、全体玩家位置/朝向与装备、CT/T 面板、C4 状态与倒计时、本地 CivetWeb、多人共享公网 Relay |
| **Misc** | 防闪光、炸弹倒计时、手雷 ESP、掉落武器 ESP |
| **界面** | ImGui 图形菜单、实时调整所有设置 |

## 🖼️ 截图

![Demo](https://github.com/tiansongyu/cs2_cheat/blob/main/img/hack.png)

## 📖 使用说明

### 环境要求
- Windows 10/11
- CS2 使用 **全屏窗口化** 模式
- 游戏窗口必须完整位于一台显示器内；跨显示器时覆盖层会暂停，移回后自动恢复
- 使用 `-insecure` 参数启动 CS2

> 独占全屏无法由外部顶层窗口可靠覆盖，不受支持。程序会在 CS2 重启后自动重新连接。

### 快捷键

| 按键 | 功能 |
|------|------|
| **F4** | 显示/隐藏菜单 |
| **F9** | 退出程序 |
| **Shift** | 自动瞄准（需启用） |
| **F** | 自动扳机（需启用） |

### 快速开始
1. 使用 `-insecure` 参数启动 CS2 进入离线模式
2. 运行程序，按 **F4** 打开菜单调整设置
3. 按 **F4** 隐藏菜单后可正常操作游戏

### 固定地图 Web Radar

Web Radar 是独立的浏览器页面，不是圆形覆盖雷达。它始终显示北向朝上的完整地图；玩家方向图标单独旋转，并支持多层地图、队伍面板和 C4 时序。

1. 按 **F4**，在 Web Radar 设置中启用内嵌服务。
2. 本机浏览器打开菜单中显示的带 token URL。默认服务地址是 `127.0.0.1:22006`，只能由本机访问。
3. 如需在同一可信局域网的手机或平板上查看，主动启用 LAN 监听，并把复制 URL 中的 `127.0.0.1` 替换为本机的私有局域网 IPv4。不要删除 URL 中的 token。

LAN token 只能限制未持有链接的访问者，HTTP/WS 本身没有 TLS。不要把端口映射到公网、使用公网穿透或在不可信 Wi-Fi 上共享链接。建议 Windows 防火墙只允许专用网络。详细架构、安全边界和优化路线见 [Web Radar 设计文档](docs/web-radar-design.md)。

### 正式公网共享 Radar

公网模式是一条独立链路，不会把游戏电脑的 CivetWeb 或任何入站端口暴露到
Internet。Windows 主程序只主动连接 `wss://`；公网服务器由 Caddy 提供自动
HTTPS，再由 Go Relay 完成房间隔离、Producer/Viewer 分权、短期 HttpOnly
会话、限流和“只保留最新帧”的有界广播。

1. 按 [正式部署指南](deploy/public-relay/README.md) 在有域名的 Linux 服务器上
   生成房间凭证并启动 Caddy + Relay。
2. 在 Windows 菜单的 **Public Relay** 中填写
   `wss://你的域名/api/v1/publish`、房间名和 Producer token，然后启用。
3. 观看者打开 `https://你的域名/`，输入同一房间名和独立的 Viewer invite
   token。不要把 Producer token 发给观看者。

服务器不保存快照历史，重启会清空会话和最新帧。本地 Radar 与公网 Radar
可以独立启停；公网模式不要求启用内嵌服务。完整协议、安全模型和扩展边界见
[公网 Relay 设计文档](docs/public-radar-relay-design.md)。
也可以直接打开 [公网 Radar 架构图](docs/public-radar-relay-architecture.html)
查看端口、信任边界和完整数据流。

Web Radar 和写内存的防闪光功能默认关闭。只有在明确需要离线测试防闪光时，才从命令行使用：

```powershell
.\external-cheat-base.exe --allow-memory-writes
```

运行诊断会写入 `%TEMP%\cs2-esp.log`。

## 🛠️ 编译

```bash
git clone https://github.com/tiansongyu/cs2_cheat.git
cd cs2_cheat
# 先构建固定地图 Web Radar 静态文件（需要 Node.js 22.12+）
cd web-radar
npm ci
npm test
npm run build
cd ..

# 使用 Visual Studio 2022 打开 external-cheat-base.sln
# 选择 Release | x64 配置编译

# 或在安装 Docker 后一次性运行前后端测试和 Windows 交叉编译
docker build --target artifacts -t cs2-cheat-build .

# 单独构建包含前端静态文件的公网 Relay 生产镜像
docker build -f radar-relay/Dockerfile -t cs2-radar-relay:local .
```

MSBuild 会把已有的 `web-radar/dist` 复制到输出目录的 `web-radar/dist`。CI 与 Docker 会自动构建前端；本机使用 Visual Studio 前请执行上面的 npm 命令。地图同步脚本会下载经过 SHA-256 校验的固定版本素材：

```bash
python3 scripts/sync_web_radar_maps.py
```

地图图片和 overview 坐标属于 Valve，并继续受 Valve 的版权和条款约束；发布包中的 `web-radar/dist/maps/NOTICE.txt` 与 `SOURCE.json` 必须保留。Web Radar 代码是参考功能后的 clean-room 重写，不包含 `cs2_webradar` 的 GPL-3.0 源代码。


## 📜 许可证

MIT License

## 🙏 致谢

- [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper) - 偏移量来源
- [libsdl-org/SDL](https://github.com/libsdl-org/SDL) - SDL2 图形库
- [ocornut/imgui](https://github.com/ocornut/imgui) - ImGui 界面库
- [CivetWeb](https://github.com/civetweb/civetweb) - MIT 许可的内嵌 HTTP/WebSocket 服务
- [Caddy](https://caddyserver.com/) - 公网入口、自动 HTTPS 与 WebSocket 反向代理
- [gorilla/websocket](https://github.com/gorilla/websocket) - BSD-2-Clause 许可的 Relay WebSocket 传输
- [awpy-data](https://github.com/pnxenopoulos/awpy-data) - 地图素材同步来源与 overview 数据流水线（素材版权见随附 NOTICE）
