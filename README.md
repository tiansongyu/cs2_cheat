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

本项目通过 GitHub Actions **每小时自动检测** CS2 游戏更新，自动同步最新偏移量并编译发布，**无需手动更新，永不过期**。

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

## ✨ 功能特性

| 分类 | 功能 |
|------|------|
| **ESP** | 方框透视、血条、武器显示、距离、射线、墙后虚线提示 |
| **Aimbot** | 自动瞄准、FOV 调节、平滑度、RCS 后座力补偿 |
| **Triggerbot** | 自动扳机、延迟设置 |
| **雷达** | 独立雷达覆盖、敌人位置/朝向显示 |
| **界面** | ImGui 图形菜单、实时调整所有设置 |

## 🖼️ 截图

![Demo](https://github.com/tiansongyu/cs2_cheat/blob/main/img/hack.png)

## 📖 使用说明

### 环境要求
- Windows 10/11
- CS2 使用 **全屏窗口化** 模式
- 使用 `-insecure` 参数启动 CS2

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

## 🛠️ 编译

```bash
git clone https://github.com/tiansongyu/cs2_cheat.git
cd cs2_cheat
# 使用 Visual Studio 2022 打开 external-cheat-base.sln
# 选择 Release | x64 配置编译
```

## 📜 许可证

MIT License

## 🙏 致谢

- [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper) - 偏移量来源
- [libsdl-org/SDL](https://github.com/libsdl-org/SDL) - SDL2 图形库
- [ocornut/imgui](https://github.com/ocornut/imgui) - ImGui 界面库
