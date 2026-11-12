# 模块与数据流

主程序入口是 `external-cheat-base/src/main.cpp`。它组织覆盖层、Radar 服务、录制器和皮肤运行时；浏览器前端和公网 Relay 分别位于独立目录。

## 目录索引

| 路径 | 职责 |
| --- | --- |
| `external-cheat-base/src/core/memory/` | 进程内存访问与布局定义 |
| `external-cheat-base/src/core/game/` | 实体访问、游戏快照、固定地图数据和 Radar JSON 序列化 |
| `external-cheat-base/src/core/renderer/` | SDL 渲染、视口与 DPI 计算 |
| `external-cheat-base/src/features/` | ESP、菜单、辅助功能、Radar 与皮肤模块 |
| `web-radar/src/` | React 页面、地图坐标、连接状态、回放逻辑 |
| `radar-relay/internal/` | 凭证、配置、房间、会话、广播和限流 |
| `deploy/public-relay/` | Caddy、Compose、配置生成与部署验证 |
| `tests/` | C++ 与构建元数据回归测试 |
| `scripts/` | 地图、皮肤目录和构建元数据生成 |
| `docs/` | 原理与设计资料；`docs/site/` 为本站源文件 |

## 快照的去向

```text
游戏状态 → C++ 快照
              ├─ SDL2 / ImGui → 本地覆盖层与固定地图
              ├─ JSON → 内嵌 CivetWeb → 本机 / 局域网浏览器
              ├─ JSON → 出站 WSS → Go Relay → 授权浏览器
              └─ 脱敏 NDJSON → 本地文件 → 浏览器回放
```

`core/game/web_radar_json.cpp` 负责快照序列化；主程序根据隐私选项构造序列化参数。内嵌服务和公网生产者复用序列化后的快照。

## 渲染与传输边界

`features/web_radar/web_radar_service.cpp` 管理内嵌 HTTP/WebSocket 服务。发布快照的调用不会直接执行 socket I/O；每个观看者有独立写入路径和一个可替换的待发快照，慢连接跳过旧帧。

Relay 也保留最新完整快照，而不存储历史轨迹。它是单节点内存服务：重启会清空房间快照并使浏览器会话失效。现有实现不支持把同一房间分散到多个副本。

## 建议的源码阅读顺序

1. 从 `src/main.cpp` 查看模块生命周期和连接状态管理。
2. 阅读 `core/game/game_snapshot.hpp` 和 `web_radar_json.cpp`，理解快照与序列化边界。
3. 阅读 `core/renderer/viewport_math.hpp` 和对应测试，理解窗口坐标转换。
4. 对照 `web-radar/src/lib/` 与 `src/hooks/`，查看坐标、流状态和回放行为。
5. 阅读 `radar-relay/internal/relay/`，查看会话、房间和有界广播。

本文是代码导览。具体字段、配置范围和部署约束请继续阅读[资料与排查](resources.md)中的原始设计文档。
