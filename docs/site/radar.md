# Radar 使用说明

本地固定地图、浏览器 Radar 和公网 Relay 共用快照与地图数据，但监听地址、凭证和部署方式不同。

## 模式选择

| 模式 | 数据路径 | 适用范围 |
| --- | --- | --- |
| 本地覆盖层 | C++ 快照 → SDL 地图 | 同一台 Windows 电脑 |
| 内嵌 Web Radar | CivetWeb HTTP/WS → 浏览器 | 本机或可信局域网 |
| 公网 Relay | 主程序出站 WSS → Relay → 浏览器 | 独立服务器上的授权共享 |
| 文件回放 | NDJSON 文件 → 浏览器 | 查看已录制的脱敏快照 |

## 本地与局域网

按 `F4` 打开 **World & Radar**，启用 **Local map overlay** 显示固定地图。需要浏览器视图时启用 **Web Radar**，打开菜单提供的带 token URL。默认监听 `127.0.0.1:22006`。

可信局域网访问需要开启 LAN 监听，并将 URL 中的回环地址替换为主机私有 IPv4 地址，保留完整 token。本地服务使用 HTTP/WS，token 不提供传输加密；不要把该端口直接映射到公网。

仅由 Web Radar 驱动的采样会按状态调整：无人连接时为 4 Hz，显式后台共享时为 10 Hz，前台观看者连接时恢复为 20 Hz。本地覆盖层和录制仍按前台需求工作。

## 录制与回放

启用 **Record sanitized Radar snapshots** 后，脱敏文件保存在 `%LOCALAPPDATA%\AegisCS2\recordings`。在 Web Radar 设置面板载入 `.ndjson` 文件，可播放或拖动时间位置，无需保持实时游戏连接。

## 公网 Relay

Windows 主程序主动向 Relay 建立 WSS 连接，不需要给游戏电脑开放入站端口。Producer 和 Viewer 使用独立凭证；Viewer 用房间邀请码换取短期、同源会话 Cookie。

| 接口 | 用途 |
| --- | --- |
| `POST /api/v1/session` | 用邀请码建立观看会话 |
| `GET /api/v1/session` | 查询当前会话 |
| `DELETE /api/v1/session` | 注销会话 |
| `GET /api/v1/publish` | Producer WebSocket 升级入口 |
| `GET /api/v1/stream` | Viewer WebSocket 升级入口 |
| `GET /healthz`、`GET /readyz` | 存活与就绪检查 |

Relay API 拒绝 query string；凭证不应放入公网 URL。浏览器会话 Cookie 使用 `Secure`、`HttpOnly` 和 `SameSite=Strict`。内嵌服务的 token URL 规则不能用于 Relay。

完整部署步骤见[公网部署指南](https://github.com/tiansongyu/cs2_cheat/blob/main/deploy/public-relay/README.md)。服务需要 HTTPS/WSS、独立凭证及正确的 Origin 配置，不能在 GitHub Pages 上运行。
