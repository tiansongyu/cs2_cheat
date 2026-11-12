# 资料与排查

## 学习资料

原始学习文章保留在仓库中，以下链接指向 `main` 分支。它们记录原理与研究过程，其中的地址和偏移不能直接视作当前游戏版本的有效值。

| 主题 | 资料 |
| --- | --- |
| 内存与实体关系 | [CS2 内存结构基础](https://github.com/tiansongyu/cs2_cheat/blob/main/docs/CS2_Memory_Structure_Basics.md) |
| 代码阅读 | [代码说明](https://github.com/tiansongyu/cs2_cheat/blob/main/docs/cheat_code.md) |
| 逆向分析 | [IDA 与 client.dll](https://github.com/tiansongyu/cs2_cheat/blob/main/docs/IDA_Reverse_Engineering_Client_DLL.md) |
| 图形数学 | [3D 投影原理](https://github.com/tiansongyu/cs2_cheat/blob/main/docs/3d_projection_explained.md)、[视图矩阵数学](https://github.com/tiansongyu/cs2_cheat/blob/main/docs/cs2_view_matrix_math.md) |
| Web Radar 设计 | [本地 Radar 设计](https://github.com/tiansongyu/cs2_cheat/blob/main/docs/web-radar-design.md) |
| 公网共享 | [Relay 设计](https://github.com/tiansongyu/cs2_cheat/blob/main/docs/public-radar-relay-design.md)、[Relay 模块说明](https://github.com/tiansongyu/cs2_cheat/blob/main/radar-relay/README.md) |

## 常见问题

### 没有显示覆盖层

检查游戏是否处于全屏窗口化模式，窗口是否完整位于一个显示器中。跨显示器或独占全屏不在支持范围内。再检查 `%TEMP%\cs2-esp.log` 中的启动和进程连接记录。

### Web Radar 页面或地图缺失

先确认 `web-radar/dist` 已构建并随程序完整分发，再运行 `node scripts/validate-bundle.mjs dist` 检查资源。不要只复制 `index.html`；前端和本地覆盖层都需要地图文件。

### 本机链接可用，其他设备无法访问

`127.0.0.1` 仅指向当前设备。可信局域网访问需要 LAN 监听、主机私有 IPv4 地址及完整 token；同时检查 Windows 防火墙。公网共享使用独立 Relay。

### Relay 重启后需要重新登录

这是当前内存会话设计的行为。重启会清除会话和快照，浏览器需要重新建立会话，Producer 需要恢复连接。

### 皮肤参数读回成功但外观没有变化

读回只证明字段值一致。现有 README 建议切换或重新购买武器，并用 **Reapply** 请求重新应用；实际材质刷新仍需离线实机验收。

## 提交问题前

记录构建版本、偏移提交、操作步骤、预期行为和实际现象。可附上菜单中生成的诊断报告；不要提交访问 token、会话 Cookie 或包含私人信息的原始录制。
