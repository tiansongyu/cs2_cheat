# cs2cheat-driver

最小内核驱动后端。配套用户态 `external-cheat-base` 使用，提供 IOCTL 形式的进程读写。

## 三件套

```
shared/ioctl_protocol.h          ← 协议（用户态/内核同时引用）
cs2cheat-driver/driver.cpp       ← 驱动（IRP_MJ_DEVICE_CONTROL）
external-cheat-base/src/core/memory/driver_backend.cpp  ← 用户态 DeviceIoControl 后端
```

`memory::SetBackend(&memory::kDriverBackend)` 切换；`InitDriverBackend()` 失败自动保持默认 RPM 后端。`main.cpp` 启动期已挂好。

## 编译

需要安装 **WDK (Windows Driver Kit)**，与 cheat 主项目分别用各自 `.sln`：

```bat
:: 主作弊（Visual Studio 2022）
external-cheat-base.sln  → Release | x64

:: 内核驱动（VS2022 + WDK）
cs2cheat-driver.sln      → Release | x64
:: 产物 cs2cheat-driver\x64\Release\cs2cheat-driver.sys
```

## 加载（两种方式，二选一）

### 方式 A：测试签名（开发期，推荐先打通链路）

需要先开测试签名（一次性，需要重启）：

```bat
bcdedit /set testsigning on
shutdown /r /t 0
```

然后注册并启动驱动：

```bat
sc create cs2cheat type= kernel binPath= C:\full\path\to\cs2cheat-driver.sys
sc start cs2cheat
```

驱动用 WDK 的测试证书签名即可（开发签名）。

加载成功后再启动 `external-cheat-base.exe`，控制台/启动日志能看到驱动被命中（无 `OpenProcess` 调用）。失败时静默回退 RPM。

卸载：

```bat
sc stop cs2cheat
sc delete cs2cheat
```

### 方式 B：kdmapper 手动映射（无需测试签名 / 无水印）

利用签名漏洞驱动 `iqvw64e.sys` 映射未签名 `.sys`。

```bat
:: 任意 PE 形式的 cs2cheat-driver.sys（不需要签名）
kdmapper.exe cs2cheat-driver.sys
```

`DriverEntry` 已做 NULL-safe 改造：检测到 kdmapper 传入的 `DriverObject == NULL` 时，会通过 `IoCreateDriver` 自举一个 driver 对象，再走标准初始化流程。设备名 `\Device\cs2cheat` 与符号链接 `\DosDevices\cs2cheat` 与方式 A 完全一致，用户态无感。

注意：
- HVCI / VBS 必须关闭（kdmapper 利用的 BYOVD 路径不能在 HVCI 下工作）
- 重启后驱动消失，需要重新映射
- 由于不进 `PsLoadedModuleList`，`sc query` 等不可见

## 限制

- 仅 x64
- HVCI / VBS 开启时驱动加载失败，需要关闭
- 与 EAC/BE 等内核反作弊冲突；本项目仅离线 `-insecure` 模式使用
