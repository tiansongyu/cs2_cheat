# WDK 安装指南

本文档帮助你把 `cs2cheat-driver.sln` 跑起来。WDK = Windows Driver Kit，是微软官方的内核驱动开发包。本仓库的内核驱动需要用它编译。

> **总耗时**：30–60 分钟，**磁盘占用**：约 6 GB。

## 为什么需要 WDK

`cs2cheat-driver` 是一个 `.sys` 内核模式驱动，包含 `ntifs.h`、`MmCopyVirtualMemory`、`PsLookupProcessByProcessId` 等内核 API 调用。这些头文件和导出库不在普通 Windows SDK 里，只有 WDK 提供。同时它给 Visual Studio 注册了一套叫 `WindowsKernelModeDriver10.0` 的平台工具集，VS 才会知道如何用 `cl.exe` 的 `/kernel` 模式编出可被 Windows 加载的驱动。

## 三件套

按顺序装：

1. **Visual Studio 2022**（用户态主项目也用它）
2. **Windows SDK**（版本号必须与 WDK 完全对应）
3. **Windows Driver Kit (WDK)**

## 步骤 1：Visual Studio 2022

如果你已经能编译 `external-cheat-base.sln`，跳过此步。否则：

下载页：https://visualstudio.microsoft.com/zh-hans/downloads/

下载 Community 版（免费）。安装时**必须勾选**这两个工作负载/组件：

- 工作负载：**使用 C++ 的桌面开发**
- 单个组件：**Windows 11 SDK (最新版)** 或 **Windows 10 SDK**

如果忘了勾，事后补救：开始菜单 → *Visual Studio Installer* → 已安装项目右侧的"修改" → 勾上后点修改。

## 步骤 2：Windows SDK

WDK 与 SDK 必须**版本号完全匹配**。例如 WDK 10.0.22621.x 需要 SDK 10.0.22621.x。**装错版本是新人最常见的坑**。

### 2.1 确定要装哪个版本

打开 WDK 下载页：https://learn.microsoft.com/zh-cn/windows-hardware/drivers/download-the-wdk

页面会写一句类似：

> *"This download requires Windows SDK 10.0.26100.x"*

**记下这个版本号**。

### 2.2 下载并安装匹配版本的 SDK

如果当前显示的 WDK 要求的 SDK 版本就是最新的：

下载页：https://developer.microsoft.com/zh-cn/windows/downloads/windows-sdk/

点 *Download the installer* 一路下一步。

如果要装老版本 SDK（与 WDK 配对），到 SDK 历史归档：
https://developer.microsoft.com/zh-cn/windows/downloads/sdk-archive/

找到与 WDK 同号的 SDK 下载安装。

### 2.3 验证 SDK 安装

```cmd
dir "C:\Program Files (x86)\Windows Kits\10\Include"
```

能看到对应版本的目录（例如 `10.0.26100.0\`）即为成功。

## 步骤 3：Windows Driver Kit

### 3.1 下载

回到 WDK 下载页：https://learn.microsoft.com/zh-cn/windows-hardware/drivers/download-the-wdk

点 *Step 2: Install Windows 11 WDK*（或对应版本），下载 `wdksetup.exe`。

### 3.2 安装

双击 `wdksetup.exe`，全部默认即可。

**最后一步极其重要**：安装完成时会弹一个对话框：

> *"Install Windows Driver Kit Visual Studio extension?"*

或：

> *"Would you like to install the Windows Driver Kit Visual Studio extension?"*

**必须勾选 / 必须点 Install**。这个 VSIX 才是给 Visual Studio 注册 `WindowsKernelModeDriver10.0` 平台工具集的核心组件。如果不装，打开 `cs2cheat-driver.sln` 会立即报错：

```
The build tools for WindowsKernelModeDriver10.0 (Platform Toolset = 'WindowsKernelModeDriver10.0')
cannot be found.
```

### 3.3 错过 VSIX 的补救

如果不小心跳过了 VSIX 步骤，不用重装 WDK：

```cmd
cd "C:\Program Files (x86)\Windows Kits\10\Vsix\VS2022"
```

双击目录里的 `.vsix` 文件，由 VSIX Installer 装进 VS2022。

### 3.4 验证 WDK 安装

```cmd
dir "C:\Program Files (x86)\Windows Kits\10\Include"
```

每个 SDK 版本目录下都有：

- `km\` ← 内核头文件（`ntifs.h`, `wdm.h`, `ntddk.h`）
- `shared\`
- `um\`

如果 `km\` 存在，说明 WDK 头文件已就位。

```cmd
dir "C:\Program Files (x86)\Windows Kits\10\Lib"
```

对应版本目录下应有 `km\x64\ntoskrnl.lib`。

## 步骤 4：编译本仓库的驱动

### 4.1 打开解决方案

```
cd cs2_cheat
:: 双击 cs2cheat-driver.sln
```

### 4.2 验证工具集已识别

VS 顶部菜单栏看：配置应该可以选 `Debug` / `Release`，平台 `x64`。

右键项目 → *属性* → *常规*：

- **Platform Toolset** 应显示 `WindowsKernelModeDriver10.0`
- **Configuration Type** 应显示 `Driver`
- **Target Operating System** 应显示 `Windows 10 / Windows 11`

如果某项是空白或显示找不到，回到 3.3 装 VSIX。

### 4.3 编译

菜单：*生成* → *生成解决方案*（或 `Ctrl+Shift+B`）。

成功后产物在：

```
cs2cheat-driver\x64\Release\cs2cheat-driver.sys
cs2cheat-driver\x64\Release\cs2cheat-driver.pdb
```

第一次编译大约 1–3 分钟。

## 常见错误排错

### 错误 1：找不到 `ntifs.h`

```
fatal error C1083: Cannot open include file: 'ntifs.h'
```

**原因**：SDK 与 WDK 版本不匹配，或 WDK 头文件没装上。

**排查**：

```cmd
dir "C:\Program Files (x86)\Windows Kits\10\Include\10.0.*\km\ntifs.h"
```

如果没结果说明 km 头没装。重装 WDK，安装时确保**没有取消任何组件勾选**。

### 错误 2：平台工具集找不到

```
The build tools for WindowsKernelModeDriver10.0 cannot be found.
```

**原因**：WDK VSIX 没装。回到 3.3。

### 错误 3：链接错误 `unresolved external symbol __imp_MmCopyVirtualMemory`

**原因**：`ntoskrnl.lib` 没被链接进来。一般 WDK 工具集会自动加，如果没加：

项目属性 → *Linker* → *Input* → *Additional Dependencies*，加：

```
ntoskrnl.lib;wdmsec.lib;%(AdditionalDependencies)
```

### 错误 4：`Inf2Cat` 报错或缺少签名

驱动构建会试图为 `.sys` 生成 catalog 文件并签名。Phase 2 我们用测试签名 / kdmapper，这一步可以容忍失败：

项目属性 → *Driver Signing* → *General* → *Sign Mode* 改为 `Off` 或 `Test Sign`。

只要 `.sys` 文件生成出来，加载方式见 `cs2cheat-driver/README.md`。

### 错误 5：`error LNK2019: __security_check_cookie`

**原因**：用了 C++ 异常或某些 CRT 特性。本项目已用 `__try / __except` SEH 而非 C++ 异常，且 vcxproj 里设了 `<ExceptionHandling>false</ExceptionHandling>`。如果你改了代码引入了 `try/throw`，去掉即可。

### 错误 6：装完 SDK 后版本号在 VS 里看不到

VS 没读到新版 SDK：项目属性 → *常规* → *Windows SDK Version*，下拉选具体版本号。如果下拉是空的，重启 VS。

## 离线 / 国内网络环境

微软国内 CDN 有时不稳：

- SDK / WDK 的 ISO 离线包：在 SDK 归档页可下，跑 `WinSdkSetup.exe /layout C:\sdk-offline` 拉离线镜像
- 用代理：在 *设置 → 代理* 里配，或临时给 `wdksetup.exe` 一个全局代理

实在不行，可以从 [Visual Studio Marketplace](https://marketplace.visualstudio.com/) 搜 *Windows Driver Kit*，下载 VSIX 单独装（前提：头文件已经手工拷到 `Windows Kits\10\Include\<version>\km`）。

## 验证整条链路

WDK 装好后，按这个 checklist 走一遍，一次性确认环境完备：

```cmd
:: 1. SDK
dir "C:\Program Files (x86)\Windows Kits\10\Include\10.0.*\um\Windows.h"

:: 2. WDK 内核头
dir "C:\Program Files (x86)\Windows Kits\10\Include\10.0.*\km\ntifs.h"

:: 3. WDK 内核库
dir "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.*\km\x64\ntoskrnl.lib"

:: 4. VS 工具集
dir "C:\Program Files\Microsoft Visual Studio\2022\*\MSBuild\Microsoft\VC\v170\Platforms\x64\PlatformToolsets\WindowsKernelModeDriver10.0"
```

四条全有结果，说明 WDK 编译链路完整。打开 `cs2cheat-driver.sln` 直接编即可。

## 参考链接

- [WDK 官方下载](https://learn.microsoft.com/zh-cn/windows-hardware/drivers/download-the-wdk)
- [SDK 历史版本归档](https://developer.microsoft.com/zh-cn/windows/downloads/sdk-archive/)
- [WDK 入门教程（英文）](https://learn.microsoft.com/en-us/windows-hardware/drivers/gettingstarted/)
- [WDM 驱动模型概念](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/writing-wdm-drivers)
