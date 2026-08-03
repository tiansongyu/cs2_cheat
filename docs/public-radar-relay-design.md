# 正式公网 Web Radar Relay 设计

可交互拓扑、信任边界和数据/凭证流见
[公网 Radar 架构图](public-radar-relay-architecture.html)。

## 目标与边界

公网版本保留现有固定北向地图和 JSON v1 数据模型，但改变传输方向：
游戏电脑只建立出站 `wss://` 连接，公网服务器不能连接或控制游戏电脑，
浏览器也不能直接接触内嵌 CivetWeb。

本实现面向单节点、少量私有房间的正式部署。它提供 TLS 入口、房间隔离、
生产者和观看者分权、短期浏览器会话、连接和帧限制以及有界背压。它不是
匿名公共大厅、历史记录服务、比赛回放平台或多节点高可用集群。

仍应只在获得授权的环境中使用。Relay 默认不持久化任何游戏快照。

## 最终拓扑

```text
Windows 游戏电脑
  CS2 只读采样
      │
      ▼
  GameSnapshot / JSON v1
      │
      ▼
  RadarRelayClient（WinHTTP）
      │ 出站 WSS
      │ Authorization: Bearer <producer token>
      │ X-Radar-Room: <room id>
      ▼
┌──────────────────── 公网服务器 ────────────────────┐
│ Caddy :443                                          │
│   ├── 自动 HTTPS / HSTS / 安全响应头                │
│   └── WebSocket 反向代理                            │
│             │                                       │
│             ▼                                       │
│ Radar Relay（仅容器内 HTTP）                        │
│   ├── Producer WSS /api/v1/publish                  │
│   ├── Session API /api/v1/session                   │
│   ├── Viewer WSS /api/v1/stream                     │
│   ├── 每个房间只保留最新快照                        │
│   └── React 固定地图静态文件                        │
└─────────────────────────────────────────────────────┘
      ▲
      │ HTTPS 登录 + HttpOnly Cookie + WSS
      │
远程浏览器
```

现有 CivetWeb 继续负责本机和局域网模式。公网模式不要求开启 CivetWeb，
两条链路可以独立启停，也可以同时使用同一个不可变 `GameSnapshot`。

## 组件选择

### Windows Producer

- 使用 Windows 自带 WinHTTP WebSocket API。
- 只允许 `wss://`，使用系统证书链和主机名校验。
- 不提供关闭证书校验的选项。
- 生产者 token 只保存在进程内存中，不写日志、不出现在 URL 中。
- 网络线程只保留一个可替换的待发送快照；网络慢时丢弃旧帧。
- 断线采用有上限的指数退避，游戏采样线程从不等待网络 I/O。
- 禁用或修改配置时，UI 只发出停止请求，后台 reaper 负责等待 WinHTTP
  worker 退出；旧 worker 未退出前不会再创建新 Producer，避免同步 WebSocket
  send 卡住界面或反复切换累积线程。

选择 WinHTTP 可以复用 Windows 的 TLS、代理和证书更新机制，并避免再向主
程序引入 OpenSSL 或另一个 TLS 运行库。

### Public Relay

- 使用 Go HTTP 服务和固定版本的 WebSocket 库。
- 房间和长期凭证只以 SHA-256 哈希形式出现在服务器配置中。
- 原始凭证由配置生成命令一次性生成，不能从哈希配置恢复。
- 会话和最新快照只保存在内存；服务重启会使所有浏览器会话失效。
- 单节点内不需要 Redis、数据库或消息队列。
- 配置拒绝跨房间或跨角色复用同一个 token 哈希，避免一次配置错误扩大权限。

### TLS Edge

- Caddy 是唯一公开监听 80/443 的进程，并自动管理 HTTPS 证书。
- Relay 端口只暴露在 Compose 内部网络，不映射到宿主机公网。
- Caddy 负责安全响应头；Relay 同时设置核心响应头，避免绕过入口后变成
  完全无保护的服务。

## 协议

### Producer 握手

```http
GET /api/v1/publish HTTP/1.1
Upgrade: websocket
Authorization: Bearer <producer-token>
X-Radar-Room: team-a
```

Relay 要求一个房间最多有一个活动 Producer。Producer 只能发送 UTF-8 文本
帧，每帧必须是有界的 JSON v1 完整快照：

```json
{"v":1,"type":"snapshot","seq":42}
```

Relay 检查版本、类型、UTF-8、最大帧长度、发布频率和 `capturedAtMs`
新鲜度，但不修改或重新序列化快照。默认拒绝采集时间早于当前服务器时间
10 秒或超前 30 秒的帧，防止断网后把积压旧帧伪装成实时状态；游戏电脑和
服务器都必须保持时间同步。
因此本地 CivetWeb 和公网 Relay 看到完全相同的数据合同。

### Viewer 会话

浏览器不把长期凭证放入 URL。登录流程为：

```http
POST /api/v1/session
Content-Type: application/json

{"room":"team-a","inviteToken":"..."}
```

验证成功后，Relay 返回房间和过期时间，并设置：

```text
Secure; HttpOnly; SameSite=Strict; Path=/
```

会话 cookie 是高熵随机值；Relay 内存中只保存其 SHA-256 哈希。后续接口：

- `GET /api/v1/session`：查询当前登录房间。
- `DELETE /api/v1/session`：注销并撤销当前会话。
- `GET /api/v1/stream`：使用同源 cookie 建立 Viewer WebSocket。

Viewer WebSocket 是只读的。客户端发送应用数据会被关闭。
同一浏览器 Session 默认最多建立两个 Viewer 连接；注销、用同一 Cookie
重新登录、会话过期或服务器关闭都会立即撤销该 Session，并以 `4401` 关闭
其所有活动 WebSocket。

## 房间状态与背压

每个房间维护：

- 一个可选的活动 Producer；
- 一个带接收时间的最新快照；
- 有界 Viewer 集合；
- 每个 Viewer 一个容量为一的待发送槽。

新快照原子替换旧快照。慢 Viewer 只会跳过中间帧，不会形成无界队列。
新 Viewer 只在最新快照未超过 TTL 时立即收到该帧。Producer 断开或超过
TTL 后，浏览器依靠现有 stale 状态提示显示数据已过期。

Relay 不保存快照历史，不把快照写入普通日志，也不提供回放接口。

## 安全模型

### 凭证分权

- Producer token 只能向指定房间发布，不能观看。
- Viewer invite token 只能换取指定房间的浏览器会话，不能发布。
- 浏览器 session 只能观看一个房间，具有固定到期时间。
- token 比较使用固定长度哈希和常量时间比较。

### 浏览器边界

- Session 的 `POST`、`DELETE` 和 Viewer WebSocket 必须匹配唯一配置的
  HTTPS Origin。
- `publicOrigin` 必须采用浏览器规范形式：小写 ASCII/punycode 主机名、无尾点，
  且 HTTPS 默认端口不得显式写成 `:443`。
- 不启用跨域 CORS。
- Cookie 使用 `Secure`、`HttpOnly`、`SameSite=Strict`，且不设置 Domain。
- 登录请求大小受限，只接受 JSON，并拒绝未知字段。
- 登录失败返回统一错误，不泄露房间是否存在。

### 网络和资源边界

- Producer 帧大小、发布频率、每房间 Viewer 数和每 IP 登录次数均有上限。
- 每个 Session 的 Viewer 数、全局 Session 数、追踪的限流 IP 数也都有上限。
- HTTP server 设置 header/read/write/idle timeout。
- Relay 信任代理头时必须配置明确的可信代理网段；不能无条件信任客户端
  提交的 `X-Forwarded-For`。
- 日志只记录路径，不记录 Authorization、Cookie、查询参数或快照正文。
- 公网安全组只开放 80/443；Relay 内部端口不得直接发布。

## 前端双模式

前端根据入口选择连接模式：

- URL 带 `?token=`：现有 CivetWeb embedded 模式，token 只转发到本地 WS。
- URL 不带 token：先调用 Session API，成功后连接无查询参数的 Relay WSS。
- Session API 返回 404：说明这是缺少 token 的 embedded 链接，不误显示
  公网登录框。

邀请凭证只存在登录组件的临时状态中，提交后立即清空，不进入 URL、
`localStorage` 或前端日志。Radar 的地图、玩家、装备和 C4 渲染不分叉。

## 故障行为

- Producer 连接失败：指数退避并在本地菜单显示脱敏错误；采样继续运行。
- Relay 重启：Producer 自动重连，Viewer 会话失效并返回登录页。
- Producer 断开：Viewer 保持页面，3 秒后显示 stale，而不是继续伪装实时。
- Viewer 过慢：只丢弃该 Viewer 的旧帧，不影响 Producer 或其他 Viewer。
- TLS 或证书校验失败：Producer 拒绝连接，不允许降级到明文。
- 配置无效：Relay 启动失败，不使用弱默认 token 或临时公开房间。
- 极端情况下，已完成 WSS 握手但永远不读取数据的恶意服务可能让 Windows
  同步 `WinHttpWebSocketSend` 长时间不返回。它只会占住一个后台 retirement
  worker，不会阻塞 UI；新连接会等待其退出，必要时重启客户端。未来若要做到
  网络调用本身也可硬取消，应把 Producer 迁移到 WinHTTP async completion 模型。

## 部署与扩展边界

当前实现是单节点内存 Relay。适用于一个域名、多个小型私有房间。若未来需
要多节点或大量观看者，应把连接入口与房间扇出拆开，并使用具备 TTL 的共享
状态/消息系统；Session 则接入 OIDC。不能简单启动多个当前 Relay 副本，
否则 Producer、Session 和 Viewer 可能落到不同进程。

正式 Compose 模板位于 `deploy/public-relay/`。生产镜像以非 root UID/GID
65532 运行，根文件系统只读，最终层只包含静态 Go 二进制和构建后的 Radar
前端。Caddy 是唯一映射宿主机 80/443 的容器；Relay 8080 只存在于 internal
bridge。配置作为只读 secret 挂载，Caddy 的证书和 ACME 账户单独持久化。

## 验证矩阵

| 层 | 验证内容 |
|---|---|
| Producer 单元测试 | WSS URL、房间、token、退避和配置边界 |
| Windows 编译 | WinHTTP WebSocket API、Unicode URL、Winhttp.lib 链接 |
| Relay 单元测试 | 配置哈希、常量时间认证、session 生命周期、Origin、限流 |
| Relay 集成测试 | Producer 鉴权、发布、Viewer 登录、首帧、广播、断开、TTL |
| 前端测试 | embedded/relay 模式选择、登录、注销、401/403 和 WSS URL |
| 容器验证 | 非 root Relay、只读文件系统、内部端口、Caddy 配置和健康检查 |
| 人工验收 | 公网 HTTPS、证书失败、Producer 断线、多人观看、凭证轮换 |
