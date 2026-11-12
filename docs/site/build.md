# 构建与验证

以下命令从仓库根目录执行，除非步骤中明确切换目录。文档站有独立的 Python 构建环境，见[文档维护与发布](publishing.md)。

## Windows 主程序

准备 Windows 10/11 x64、Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载和 Windows SDK。Web Radar 构建使用 Node.js 22.12 或更高的兼容版本；CI 使用 Node 22。

```powershell
git clone https://github.com/tiansongyu/cs2_cheat.git
cd cs2_cheat
cd web-radar
npm ci
npm test
npm run build
node scripts/validate-bundle.mjs dist
cd ..
```

用 Visual Studio 打开 `external-cheat-base.sln`，选择 `Release | x64` 并构建。项目文件使用 C++20，Visual Studio 2022 对应 v143 工具集。

MSBuild 会复制已有的 `web-radar/dist`。发布时保持该目录与可执行文件的配套布局和完整内容；本地地图覆盖层也会读取其中的地图资源。

## Docker 构建

在仓库根目录运行：

```bash
docker build --target artifacts -t cs2-cheat-build .
```

根目录 Dockerfile 执行前端、C++ 和 Go 测试，并交叉编译 Windows 程序。产物留在镜像内；此命令不会自动在宿主机创建发布目录。

仅构建 Relay 生产镜像：

```bash
docker build -f radar-relay/Dockerfile -t cs2-radar-relay:local .
```

## 分模块验证

| 检查对象 | 命令或入口 | 验证内容 |
| --- | --- | --- |
| Web Radar | 在 `web-radar/` 执行 `npm test`、`npm run build` | 前端测试与 TypeScript/Vite 构建 |
| 前端产物 | 在 `web-radar/` 执行 `node scripts/validate-bundle.mjs dist` | 地图路径、尺寸和归属元数据 |
| Go Relay | 在 `radar-relay/` 执行 `go test -mod=vendor -race ./...` | 会话、协议、房间和限流等行为 |
| Go 静态检查 | 在 `radar-relay/` 执行 `go vet -mod=vendor ./...` | Go 代码静态检查 |
| 地图目录 | `python3 scripts/generate_fixed_map_catalog.py --check` | 生成目录是否同步 |
| 皮肤目录 | `python3 scripts/generate_skin_catalog.py --check` | 固定来源数据与生成结果 |
| C++ 回归 | `.github/workflows/build.yaml`、根目录 `Dockerfile` | 视口、计时、快照模型、换皮和服务测试 |

Go 版本以 `radar-relay/go.mod` 的 `go` 和 `toolchain` 字段为准。检查 C++ 时沿用仓库 CI 中的编译参数，避免遗漏平台宏和链接依赖。

## 实机验收

构建完成后，在允许的离线环境中检查窗口对齐、菜单打开/关闭、退出、游戏重启后重连，以及 Radar 的连接与地图显示。内存写入相关功能另行验证参数读回和恢复；皮肤功能还需要检查实际材质变化。
