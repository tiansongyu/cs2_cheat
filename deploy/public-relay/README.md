# 正式公网 Radar Relay 部署

该目录提供单节点生产部署：Caddy 2.11.4 是唯一公网入口，自动申请和续期
HTTPS 证书；Radar Relay 只连接隔离的 Compose 内部网络。游戏电脑只主动建立
出站 WSS，不需要开放任何入站端口。

系统为何采用这套边界见 [Relay 设计文档](../../docs/public-radar-relay-design.md)，
端口、数据流和凭证流见 [可交互架构图](../../docs/public-radar-relay-architecture.html)。

```text
Windows Producer ──WSS──> Caddy :443 ──HTTP/WS──> Radar Relay :8080
浏览器 Viewer   ─HTTPS──> Caddy :443 ──HTTP/WS──> Radar Relay :8080
```

## 安全默认值

- 宿主机只发布 TCP 80 和 443；Relay 的 8080 只有 `expose`，没有端口映射。
- Caddy 和 Relay 都以 UID/GID 65532、只读根文件系统、`cap_drop: ALL` 和
  `no-new-privileges` 运行；容器内监听高位端口。派生 Caddy 镜像固定以
  `caddy:2.11.4-alpine` 为基础，只移除高位端口部署不需要的
  `cap_net_bind_service` 文件 capability。
- Relay 配置通过只读 Compose secret 挂载；配置只含长期 token 的 SHA-256
  哈希，不含可登录的原始 token。
- Caddy 使用固定内部地址 `172.30.67.2`，Relay 只信任该 `/32` 传入的
  `X-Real-IP`。不要把 Relay 再接到其他共享 Docker 网络。
- Caddyfile 故意没有启用 access log。运行日志不会记录 Authorization、
  Cookie、查询参数或请求正文，也不会记录 Radar 快照正文。
- Session、最新快照只存在 Relay 内存中；容器重启后浏览器需要重新登录。
- 每个 Session 最多建立 2 个 Viewer 连接；Relay 拒绝过旧或明显来自未来的
  快照，避免 Producer 重连后把陈旧数据重新广播。
- `/healthz` 和 `/readyz` 不通过公网域名暴露；Compose 和 Caddy 在内部使用。

## 准备条件

1. 一台具有公网 IPv4 的 Linux 服务器，已安装 Docker Engine 和 Compose v2。
2. 小写 ASCII 域名 `radar.example.com` 的 A 记录直接指向该服务器。默认不要启用 CDN/HTTP
   代理，否则登录限流看到的是代理 IP，且需要重新设计可信代理范围。
3. 云安全组和主机防火墙只放行 TCP 80、443；不要放行 8080。
4. 80/443 未被宿主机上的其他 Web 服务占用，服务器时间同步正常。

若存在无效的 AAAA 记录，应先删除；否则 ACME 验证和部分客户端连接可能走向
错误的 IPv6 地址。

## 首次部署

进入本目录后创建本机配置：

```bash
cp .env.example .env
chmod 600 .env
```

编辑 `.env`，至少替换 `RADAR_DOMAIN` 和 `ACME_EMAIL`。域名必须使用小写
ASCII DNS 主机名，只写主机名，不要带 `https://`、端口或路径。生成命令的
域名必须与 `.env` 完全一致；浏览器会规范化 Origin，大小写不一致将导致登录
或 Viewer WebSocket 被 Relay 拒绝。

生成一个房间、Producer token 和 Viewer invite token：

```bash
./generate-room-config.sh radar.example.com team-a 20
```

脚本使用 OpenSSL 生成两个独立的 256-bit token，并只把哈希写入
`secrets/relay-config.json`。原始值临时写入权限为 `0600` 的
`secrets/team-a-credentials.txt`：

- `producer_token` 只配置到运行游戏的 Windows 主程序，不能发给观看者。
- `invite_token` 发给该房间的观看者，用于换取短期 HttpOnly session。
- 把二者保存到密码管理器后，安全删除 plaintext credentials 文件。

配置示意结构见 `relay-config.example.json`。该文件里的占位字符串故意无法
启动服务，避免误把示例当成生产凭证。

Caddy 和 Relay 都以非 root 用户运行，因此先授权 Relay secret，并创建
Caddy 持久化目录：

```bash
sudo chown 65532:65532 secrets/relay-config.json
sudo chmod 0400 secrets/relay-config.json
sudo install -d -m 0700 -o 65532 -g 65532 \
  state/caddy-data state/caddy-config
```

渲染配置并启动：

```bash
docker compose config --quiet
docker compose build --pull
docker compose up -d
docker compose ps
```

Caddy 第一次启动时会通过 80/443 完成 ACME 验证。证书、ACME 账号和自动保存
配置持久化在 `state/caddy-data` 与 `state/caddy-config`，更新容器不会丢失。

Windows 菜单中的公网 Relay 参数应为：

```text
Endpoint: wss://radar.example.com/api/v1/publish
Room:     team-a
Token:    <producer_token>
```

观看者打开：

```text
https://radar.example.com/
```

然后输入房间名和对应的 `invite_token`。邀请 token 不应放进 URL、聊天截图、
浏览器书签或前端存储。

## 验证

```bash
docker compose ps
docker compose logs --tail=100 caddy radar-relay
curl --fail --silent --show-error https://radar.example.com/ >/dev/null
```

预期两个容器均为 `healthy`，HTTPS 页面可以打开。公网请求
`https://radar.example.com/healthz` 应返回 404。检查宿主机监听端口时只应看到
80/443，不应看到 8080：

```bash
docker compose port caddy 8080
docker compose port caddy 8443
docker compose port radar-relay 8080
```

最后一条应为空。再完成一次端到端验收：Producer 显示 connected、观看者成功
登录、Radar 收到实时帧；停掉 Producer 后约 3 秒出现 stale 提示。

## 多房间与凭证轮换

`rooms` 可包含多个房间。每个房间必须使用独立的 Producer token，并可同时
保留最多 16 个 invite token 哈希，以便无中断轮换。生成新 token 时不要把
原始值放在命令参数、JSON 或 Git 中：

```bash
umask 077
token=$(openssl rand -hex 32)
digest=$(printf '%s' "$token" | openssl dgst -sha256 | awk '{print $NF}')
printf 'token=%s\nsha256=%s\n' "$token" "$digest"
unset token digest
```

将原始 token 直接保存到密码管理器，只把 `sha256` 值加入配置。Invite 轮换
顺序为：加入新哈希并重启 Relay、分发新 token、确认迁移、删除旧哈希并再次
重启。Producer 只能配置一个哈希，应安排短暂停机同时更新服务器配置与
Windows Producer。编辑 hash-only 配置后要重新设置 `65532:65532`、`0400`。
Relay 当前不热加载配置：任何修改后都需执行：

```bash
docker compose up -d --force-recreate radar-relay
```

## 运维与备份

- 日常查看 `docker compose ps` 与有界的 Docker operational logs；不要临时
  开启会记录请求 header/body 的反向代理 debug 插件。
- 定期备份 `state/caddy-data` 和 `state/caddy-config`。备份包含 TLS 私钥，
  必须加密并限制访问。
- `secrets/relay-config.json` 只含凭证哈希，但仍应按 secret 管理。原始 token
  只应存在于密码管理器和对应客户端内存中。
- 更新时先备份，然后运行 `docker compose build --pull radar-relay` 和
  `docker compose up -d`。Caddy 镜像固定为 2.11.4，不会自动漂移到其他版本。
- Relay 是单节点内存服务，不要横向扩容多个副本；当前协议没有共享 Session
  或房间状态。需要高可用时必须先引入共享会话和消息层。

## 常见故障

- **Caddy 无法签发证书**：检查 A/AAAA、服务器时间、安全组、80/443 和是否有
  其他服务占用端口。
- **Caddy 无法写入 `/data`**：重新确认两个 state 目录的所有者是
  `65532:65532`，权限为 `0700`。
- **Relay 配置启动失败**：确认 `publicOrigin` 与 `.env` 中域名完全一致，哈希
  恰为 64 个十六进制字符，房间 ID 符合规则。
- **总是登录失败**：Viewer 使用 invite token；Producer 使用 producer token，
  二者不可互换。
- **登录成功但没有帧**：检查 Windows Endpoint、Room、Producer token 以及
  Producer connected 状态；同时确认没有第二个 Producer 占用同一房间。
- **Compose 网络冲突**：`172.30.67.0/24` 被现有网络占用时，需要同步修改
  `compose.yaml` 中子网、Caddy 固定 IP，以及 Relay 配置中的
  `trustedProxyCIDRs`。三处必须保持一致，且只信任 Caddy 的单一 `/32`。
