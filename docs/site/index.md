# CS2 External ESP

基于 C++20、SDL2 和 Dear ImGui 的 Windows 外部程序学习项目，涵盖游戏覆盖层、固定地图 Radar、浏览器回放和可选的公网 Relay。

本站面向希望了解项目组成、构建过程与代码入口的读者。内容依据仓库代码和现有说明整理，功能兼容性随游戏版本变化；构建成功不代表已完成 Windows 游戏内验收。

```{warning}
本项目仅用于逆向工程和图形渲染学习。按照项目约定，只在使用 `-insecure` 启动的离线环境中运行，禁止用于 VAC 服务器或任何在线游戏。
```

## 从哪里开始

| 你的目标 | 阅读入口 |
| --- | --- |
| 了解功能和当前限制 | [项目概览](overview.md) |
| 在本地构建项目 | [构建与验证](build.md) |
| 理解代码如何组织 | [模块与数据流](modules.md) |
| 区分本地 Radar、Relay 和回放 | [Radar 使用说明](radar.md) |
| 查找学习文章与故障排查 | [资料与排查](resources.md) |
| 修改或发布本站 | [文档维护与发布](publishing.md) |

## 三个主要部分

- **Windows 主程序**：读取游戏状态，组织快照，使用 SDL2 和 ImGui 绘制覆盖层与设置菜单。
- **Web Radar**：使用 React、TypeScript 和 Vite 构建浏览器视图，支持实时快照与本地 NDJSON 回放。
- **Radar Relay**：Go 编写的可选服务，接收主程序的出站连接，并将最新快照分发给授权观看者。

GitHub Pages 托管本站的 HTML、样式、图片和搜索索引。实时 Radar 的 HTTP/WebSocket 服务需要单独运行。

```{figure} ../../img/hack.png
:alt: 项目 README 中的 CS2 External ESP 覆盖层示例截图

仓库提供的程序截图，具体界面以当前构建为准。
```

[查看源码](https://github.com/tiansongyu/cs2_cheat) · [下载发布版本](https://github.com/tiansongyu/cs2_cheat/releases) · [反馈问题](https://github.com/tiansongyu/cs2_cheat/issues)

```{toctree}
:maxdepth: 2
:caption: 项目说明

overview
build
modules
radar
resources
publishing
```
