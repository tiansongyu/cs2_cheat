# CS2 Cheat 内核驱动技术链详解

本文系统讲解 `cs2cheat-driver` 的完整技术链路：从用户态发起一次内存读到驱动里 `MmCopyVirtualMemory` 的每一跳，以及背后的 Windows 内核机制、安全边界、性能权衡。

阅读本文需要 C/C++ 与 Windows 应用层 API（`CreateFile` / `DeviceIoControl`）的基础。无需内核开发经验。

## 目录

1. [系统总览](#1-系统总览)
2. [Windows 内核基础概念回顾](#2-windows-内核基础概念回顾)
3. [用户态：从 ESP 到 IOCTL](#3-用户态从-esp-到-ioctl)
4. [系统调用边界：DeviceIoControl 的旅程](#4-系统调用边界deviceiocontrol-的旅程)
5. [驱动初始化：DriverEntry 与设备对象](#5-驱动初始化driverentry-与设备对象)
6. [IRP 派遣：内核如何接住请求](#6-irp-派遣内核如何接住请求)
7. [跨进程内存读取：MmCopyVirtualMemory 的内幕](#7-跨进程内存读取mmcopyvirtualmemory-的内幕)
8. [进程查找与模块基址](#8-进程查找与模块基址)
9. [批量读 IOCTL 的设计](#9-批量读-ioctl-的设计)
10. [驱动加载机制对比](#10-驱动加载机制对比)
11. [安全边界与防御编程](#11-安全边界与防御编程)
12. [性能模型](#12-性能模型)
13. [可观测性：调试与排错](#13-可观测性调试与排错)
14. [对抗与检测](#14-对抗与检测)
15. [参考资料](#15-参考资料)

---

## 1. 系统总览

整个项目由三个二进制 + 一个共享头组成：

```
┌──────────────────┐  CreateFile/        ┌──────────────────────┐
│ external-cheat-  │  DeviceIoControl    │  cs2cheat-driver.sys │
│ base.exe         │ ──────────────────▶ │ (内核 Ring 0)        │
│ (用户态 Ring 3)  │ ◀────────────────── │                      │
└──────────────────┘  IOCTL 响应          └──────────┬───────────┘
       │                                             │ MmCopyVirtualMemory
       │ ImGui/SDL 渲染                             ▼
       │                                  ┌──────────────────────┐
       ▼                                  │       cs2.exe        │
   屏幕 ESP                                │   (目标 Ring 3 进程)  │
                                          └──────────────────────┘

shared/ioctl_protocol.h   ← 两边共享的协议（IOCTL 码、请求/响应结构）
```

**核心思路**：cheat 程序自己不再 `OpenProcess(cs2.exe)`。所有跨进程读写都通过给驱动发 IOCTL，驱动在内核态用 `MmCopyVirtualMemory` 拷贝。从外部审计角度看，cheat 进程没有任何句柄指向 cs2，难以用句柄扫描类工具直接关联。

## 2. Windows 内核基础概念回顾

理解后续章节需要的几个概念，已经熟的可跳过。

### 2.1 用户态 vs 内核态（Ring 3 / Ring 0）

x86_64 CPU 提供 4 级特权环。Windows 只用两个：

- **Ring 3 用户态**：所有 .exe / .dll，不能直接访问物理内存、I/O 端口、MSR
- **Ring 0 内核态**：`ntoskrnl.exe` 与所有 `.sys` 驱动，特权指令全开

切换通过 `syscall` 指令 + 系统调用号完成，由 SSDT（System Service Descriptor Table）查表分派。

### 2.2 EPROCESS、PEB、CR3

- **EPROCESS**：内核里每个进程的描述符。包含 PID、父进程、句柄表、`DirectoryTableBase`（即 CR3 物理页表根）等
- **PEB**（Process Environment Block）：每个进程的用户态可见结构，记录加载的模块、命令行、环境变量
- **CR3**：x86_64 控制寄存器，存当前进程的 4 级页表物理地址。切换 CR3 即切换虚拟地址空间

跨进程读 cs2 内存的核心技术，本质就是"借用 cs2 的 CR3"或"通过页表手动翻译 cs2 的虚拟地址到物理地址"。`MmCopyVirtualMemory` 内部走前一种。

### 2.3 IRP（I/O Request Packet）

Windows I/O 子系统的通用消息体。任何与设备的交互（读、写、IOCTL）都被 I/O Manager 包装成 IRP，沿"设备栈"逐层下发到驱动。我们的驱动只需要响应：

- `IRP_MJ_CREATE` / `IRP_MJ_CLOSE`：用户态 `CreateFile` / `CloseHandle`
- `IRP_MJ_DEVICE_CONTROL`：用户态 `DeviceIoControl`

### 2.4 设备对象与符号链接

- **DEVICE_OBJECT**：驱动对外的接入点，名字形如 `\Device\cs2cheat`
- **符号链接**：把 `\Device\cs2cheat` 暴露到用户态可见的 `\DosDevices\cs2cheat`，对应路径 `\\.\cs2cheat`，`CreateFileW` 能打开

### 2.5 IRQL（中断请求级）

内核代码可以运行在不同 IRQL 上。本项目所有 IOCTL 处理都在 `PASSIVE_LEVEL`（最低级），可以分页、可以等待。`MmCopyVirtualMemory` 要求 `PASSIVE_LEVEL`，正好匹配。

## 3. 用户态：从 ESP 到 IOCTL

### 3.1 抽象层

`memory.hpp` 定义了一个后端表：

```cpp
struct Backend {
    bool (*read_raw)(uintptr_t address, void* buffer, size_t size);
    bool (*write_raw)(uintptr_t address, const void* buffer, size_t size);
    uintptr_t (*get_proc_id)(const wchar_t* process);
    uintptr_t (*get_module_base)(uintptr_t procID, const wchar_t* module);
    bool (*read_batch)(BatchEntry* entries, size_t count);
};
```

`memory::SetBackend(&kDriverBackend)` 一行换实现。`esp.cpp` / `aimbot.cpp` 仍然写 `memory::Read<uintptr_t>(...)`，编译期模板展开为 `ReadRaw` → 当前后端 → IOCTL。

### 3.2 BatchReader

热点路径（`esp::updateEntities`）每帧对每个敌人要读 5 个字段。直接调 5 次 IOCTL 等于 5 次 syscall，开销叠加。`BatchReader` 把这些读"攒一帧"打包成一次 IOCTL：

```cpp
BatchReader br;
auto h1 = br.queue<int32_t>(addr1);
auto h2 = br.queue<vec3>(addr2);
br.flush();                    // ← 这里才发一次 IOCTL
int32_t v1 = br.get<int32_t>(h1);
vec3    v2 = br.get<vec3>(h2);
```

内部用一个 `std::vector<uint8_t>` 作存储，`queue` 时分配槽位，`flush` 时把所有 `BatchEntry` 一并交给后端。RPM 后端就是循环 `ReadProcessMemory`；驱动后端会打包成 `IOCTL_CS2CHEAT_BATCH_READ`。

### 3.3 调用 DeviceIoControl

`driver_backend.cpp` 里：

```cpp
HANDLE gDriver = CreateFileW(L"\\\\.\\cs2cheat", GENERIC_READ|GENERIC_WRITE, 0,
                             nullptr, OPEN_EXISTING, 0, nullptr);

DeviceIoControl(gDriver,
                IOCTL_CS2CHEAT_READ_MEMORY,
                &req,  sizeof(req),       // 输入缓冲
                buffer, size,             // 输出缓冲
                &bytesReturned,
                nullptr);                 // 不重叠 I/O
```

到这一步，请求已离开用户态。

## 4. 系统调用边界：DeviceIoControl 的旅程

`DeviceIoControl` 是用户态 API，最终走的系统调用是 `NtDeviceIoControlFile`：

```
用户态 (Ring 3)              内核态 (Ring 0)
─────────────────────────────────────────────────
DeviceIoControl
   │
   ▼
ntdll!NtDeviceIoControlFile
   │  syscall + edi=0x07 (示例)
   ▼
═══════════ ring transition ═══════════
   │
   ▼
ntoskrnl!KiSystemCall64
   │
   ▼
ntoskrnl!NtDeviceIoControlFile
   │
   ▼
IoBuildDeviceIoControlRequest      ← 创建 IRP
   │
   ▼
IofCallDriver                       ← 把 IRP 投递到设备栈顶
   │
   ▼
cs2cheat-driver!Cs2DispatchIoctl    ← 我们的派遣函数
```

### 4.1 IOCTL 编码

`IOCTL_CS2CHEAT_READ_MEMORY` 不是随便取的整数。`CTL_CODE` 宏把四个字段打包成 32 位：

```c
CTL_CODE(DeviceType=0x8000, Function=0x802, Method=METHOD_BUFFERED, Access=FILE_ANY_ACCESS)
```

- `DeviceType` ≥ 0x8000 表示厂商自定义，不与微软已分配的冲突
- `Function`：自家驱动内部区分不同操作
- `Method`：决定缓冲区如何传递（见下）
- `Access`：是否要求读 / 写权限

### 4.2 缓冲区传递方式

Windows I/O Manager 提供四种把用户缓冲区"递进"内核的方法。本项目全用 `METHOD_BUFFERED`，最稳：

| 方法 | 输入 | 输出 | 性能 | 安全 |
|------|------|------|------|------|
| `METHOD_BUFFERED` | I/O Manager 在内核分配 `SystemBuffer`，把用户输入拷进来 | 驱动写到 `SystemBuffer`，I/O Manager 完成时拷回用户态 | 一次额外拷贝 | 内核访问的是自己空间，不会因用户态并发改地址而崩 |
| `METHOD_IN_DIRECT` / `OUT_DIRECT` | 锁定用户页（MDL），驱动直接读 | 锁定用户页，驱动直接写 | 零拷贝 | 要小心 IRQL 与 MDL 生命周期 |
| `METHOD_NEITHER` | 给驱动原始用户指针 | 同上 | 最快 | **极易崩溃**：不验证地址 |

`METHOD_BUFFERED` 的语义关键点：**输入缓冲与输出缓冲共享同一块 `SystemBuffer`**。驱动写输出时会覆盖输入。我们在 `HandleBatchRead` 里需要先把输入字段（`output_size`、`pid`、所有 `entries`）快照到栈上，再开始写输出，否则会读到自己写的零。

```cpp
// driver.cpp HandleBatchRead 中
CS2CHEAT_BATCH_ENTRY stack[CS2CHEAT_BATCH_MAX_ENTRIES];
RtlCopyMemory(stack, req->entries, count * sizeof(...));  // 快照
const ULONG outputSize = req->output_size;                // 快照
HANDLE pid = (HANDLE)(ULONG_PTR)req->pid;                 // 快照

PUCHAR outBase = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
RtlZeroMemory(outBase, outputSize);                       // 这里 req->* 已被覆盖
```

### 4.3 长度校验

I/O Manager 把用户传的 `lpInBufferSize` / `lpOutBufferSize` 放进 `IO_STACK_LOCATION.Parameters.DeviceIoControl`，但**不会替我们验证内容**。驱动必须自己确认：

- 输入缓冲长度 ≥ 我们要读的请求结构最小尺寸
- 输出缓冲长度 ≥ 用户声明的 `output_size`
- 用户声明的尺寸不超过 `CS2CHEAT_READ_MAX`（16 MB）

任何一项不满足就返回 `STATUS_BUFFER_TOO_SMALL` 或 `STATUS_INVALID_PARAMETER`。这是内核驱动安全的第一道防线。

## 5. 驱动初始化：DriverEntry 与设备对象

驱动加载时内核调用：

```c
NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING regPath);
```

我们的实现要做四件事：

### 5.1 创建设备对象

```cpp
IoCreateDevice(driver, 0, &g_DeviceName /* L"\Device\cs2cheat" */,
               FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
               &g_DeviceObject);
```

`FILE_DEVICE_SECURE_OPEN` 让 ACL 检查在 `IRP_MJ_CREATE` 阶段做，简化我们的代码。

### 5.2 暴露符号链接

```cpp
IoCreateSymbolicLink(&g_SymlinkName /* L"\DosDevices\cs2cheat" */, &g_DeviceName);
```

之后用户态用 `\\.\cs2cheat` 就能 `CreateFileW` 打开（前缀 `\\.\` 等价 `\DosDevices\`）。

### 5.3 注册派遣表

```cpp
driver->MajorFunction[IRP_MJ_CREATE]         = Cs2DispatchCreateClose;
driver->MajorFunction[IRP_MJ_CLOSE]          = Cs2DispatchCreateClose;
driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Cs2DispatchIoctl;
driver->DriverUnload                         = Cs2DriverUnload;
```

`MajorFunction` 是 28 个槽位的指针数组，对应不同 IRP 类型。没注册的会被 I/O Manager 用默认处理（返回 `STATUS_INVALID_DEVICE_REQUEST`）。

### 5.4 完成设备初始化

```cpp
g_DeviceObject->Flags |= DO_BUFFERED_IO;             // 配合 METHOD_BUFFERED
g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;    // 标记可用
```

清 `DO_DEVICE_INITIALIZING` 是必要步骤：在它清掉之前，I/O Manager 会拒绝任何指向这个设备的 IRP。

### 5.5 NULL-safe（kdmapper 路径）

kdmapper 调用 `DriverEntry(NULL, NULL)`。`IoCreateDevice` 第一个参数不能为 NULL，所以入口检测：

```cpp
if (driver) {
    return Cs2DriverInitialize(driver, regPath);   // sc create 路径
}
// kdmapper：自举一个 driver object
UNICODE_STRING bootName;
RtlInitUnicodeString(&bootName, L"\\Driver\\cs2cheat");
return IoCreateDriver(&bootName, &Cs2DriverInitialize);
```

`IoCreateDriver` 是 `ntoskrnl.exe` 未文档化但稳定多年的导出，内核会用它分配 `DRIVER_OBJECT` 并回调我们的 `Cs2DriverInitialize`，与正常加载路径完全等价。

## 6. IRP 派遣：内核如何接住请求

```cpp
extern "C" NTSTATUS Cs2DispatchIoctl(PDEVICE_OBJECT, PIRP irp)
{
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    switch (sp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_CS2CHEAT_READ_MEMORY:  status = HandleRead(irp, sp); break;
    // ...
    }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}
```

### 6.1 IO_STACK_LOCATION

每个 IRP 沿设备栈下行时都有一个对应的"栈帧"。`IoGetCurrentIrpStackLocation` 拿当前层的，里面有：

- 主功能码（`MajorFunction`）
- 次功能码（IOCTL 时的 `IoControlCode`）
- 参数联合体（IOCTL 时的输入/输出缓冲长度）

### 6.2 完成 IRP

`IoCompleteRequest` 通知 I/O Manager 此 IRP 处理完毕。它会：

1. 设置完成状态
2. 把 `IoStatus.Information`（实际写入字节数）回填给用户态的 `lpBytesReturned`
3. 如果是 `METHOD_BUFFERED`，把 `SystemBuffer` 拷回用户态输出缓冲
4. 唤醒等待此 IRP 完成的用户线程

`IO_NO_INCREMENT` 表示不给等待方加优先级提升（同步 IOCTL 不需要）。

## 7. 跨进程内存读取：MmCopyVirtualMemory 的内幕

整条技术链最核心的一跳。函数原型：

```c
NTKERNELAPI NTSTATUS MmCopyVirtualMemory(
    PEPROCESS SourceProcess,
    PVOID     SourceAddress,
    PEPROCESS TargetProcess,
    PVOID     TargetAddress,
    SIZE_T    BufferSize,
    KPROCESSOR_MODE PreviousMode,
    PSIZE_T   ReturnSize);
```

### 7.1 它做了什么

简化的伪代码：

```
1. 校验：BufferSize > 0，地址范围对齐合理
2. 在源进程的 VAD 树里查 SourceAddress，确认页已 commit、可读
3. 锁定源页（防止被换出或 free）
4. KeStackAttachProcess(SourceProcess) — 切换到源进程地址空间（即换 CR3）
5. 把源数据拷到一个临时内核缓冲（或直接走源 → 目的的两步映射）
6. KeUnstackDetachProcess
7. KeStackAttachProcess(TargetProcess)
8. 把数据拷到 TargetAddress
9. KeUnstackDetachProcess
10. 解锁源页
```

新版 Windows 内部更高效，不一定真的 attach 两次，可能直接通过物理页映射两边的虚拟地址。但语义等价。

### 7.2 为什么不直接 attach + memcpy

确实可以这样写：

```cpp
KeStackAttachProcess(target, &apc);
memcpy(kernelBuffer, userAddress, size);   // 源进程地址空间内
KeUnstackDetachProcess(&apc);
```

**问题**：

- 源进程页可能未驻留：`memcpy` 触发 page-in，但中断 / IRQL 在 attach 后是受限的，`memcpy` 在缺页时调度可能死锁
- SEH（`__try` / `__except`）能捕获访问违例，但代价不低
- attach 两次 + 拷贝中间环节多

`MmCopyVirtualMemory` 替我们处理了所有这些。它是 `NtReadVirtualMemory` / `ReadProcessMemory` 内部最终调用的同一个函数。区别是 RPM 走系统调用从 Ring 3 进来，我们直接在 Ring 0 调，省掉来回切换。

### 7.3 KernelMode vs UserMode

```cpp
MmCopyVirtualMemory(... KernelMode, ...);
```

最后一个参数告诉内部"调用者来自哪个 Ring"。传 `KernelMode` 跳过对地址值的"必须 < 0x7FFFFFFFFFFF"那种用户态合理性检查，因为我们目标地址确实是 cs2 用户态地址（< 用户态上限），但调用者是内核。同时禁用 ProbeForRead/Write 的双重校验。

如果传 `UserMode`，函数会做严格的地址范围验证，对我们这种"内核拷给内核"的场景反而失败。

### 7.4 失败模式

返回值常见 NTSTATUS：

- `STATUS_SUCCESS`：成功，`*ReturnSize == BufferSize`
- `STATUS_PARTIAL_COPY`：部分成功（比如跨页时一页可读一页不可读）
- `STATUS_ACCESS_VIOLATION`：地址未映射
- `STATUS_PROCESS_IS_TERMINATING`：源进程在拷贝中退出

我们的 `Cs2CopyMemory` 把 `STATUS_PARTIAL_COPY` 都当失败，并在批量读里把失败槽位零填，让用户态看到干净的"全零"而不是脏数据。

## 8. 进程查找与模块基址

### 8.1 通过名字找 PID

`Cs2FindPidByName` 用 `ZwQuerySystemInformation`：

```cpp
ZwQuerySystemInformation(SystemProcessInformation, buf, bufLen, &returned);
```

返回链表式结构，每个节点 `SYSTEM_PROCESS_INFORMATION` 包含 `ImageName`（短路径名 UNICODE_STRING）、`UniqueProcessId`、线程信息。我们扫一遍找名字匹配的拿 PID。

**为什么用 Zw 而不是 Nt**：内核里 `ZwXxx` 自动设 `PreviousMode=KernelMode`，会绕开一些用户态参数检查；`NtXxx` 用调用者当前模式。在 IRP 派遣里我们就是内核调，用 `Zw` 更稳。

**为什么不用 PsActiveProcessHead 直接扫链表**：那种方式更隐蔽（不留 trace event），但要硬编码 `EPROCESS.ActiveProcessLinks` 在不同 Windows 版本的偏移。可维护性差。`ZwQuerySystemInformation` 是稳定 API。

### 8.2 通过 PID + 模块名找模块基址

`Cs2GetModuleBase` 走目标进程 PEB：

```cpp
KeStackAttachProcess(target, &apc);   // 切到 cs2 地址空间
PVOID peb = PsGetProcessPeb(target);  // 拿 cs2 的 PEB
// 此时 peb 指针在 cs2 地址空间是可解引用的
__try {
    PLIST_ENTRY head = &localPeb->Ldr->InLoadOrderModuleList;
    for (cur = head->Flink; cur != head; cur = cur->Flink) {
        // 匹配 BaseDllName == "client.dll"
    }
} __except (EXCEPTION_EXECUTE_HANDLER) { ... }
KeUnstackDetachProcess(&apc);
```

PEB 是一个用户态结构，物理上属于 cs2 进程，attach 后我们的 CR3 与 cs2 一致，就能把它当本地指针读。

`__try / __except` 是 SEH（Structured Exception Handling）。万一 cs2 正在销毁、PEB 已被解除映射，访问违例会被这里捕获，不会蓝屏。

### 8.3 用户态 ↔ 驱动协议

- 用户态发 `IOCTL_CS2CHEAT_GET_PID_BY_NAME { wchar_t name[260] }` → 返回 `{ uint32_t pid }`
- 用户态发 `IOCTL_CS2CHEAT_GET_MODULE_BASE { uint32_t pid; wchar_t module[260] }` → 返回 `{ uint64_t base }`

设计上这两个 IOCTL 都用 `METHOD_BUFFERED`，name / module 字段是定长 260 wchar（与 `MAX_PATH` 一致）。定长设计让协议结构简单，反正每帧只调一次（启动期），不在乎几百字节的开销。

## 9. 批量读 IOCTL 的设计

### 9.1 为什么需要

ESP 每帧 ~80 次小读（每个敌人 5 个字段 × ~16 个敌人）。每次 IOCTL：

```
ring transition (Ring 3 → Ring 0): ~100 ns
IRP 创建/派遣/完成:                ~500 ns
DispatchIoctl + MmCopyVirtualMemory: ~500 ns
ring transition (Ring 0 → Ring 3): ~100 ns
合计 ~1.2 µs / IOCTL × 80 = ~100 µs/帧 = 1% CPU @ 60 fps
```

只有 1%，听起来不多，但这是单线程串行调用，会卡数据线程的迭代速度。批量读把 80 次压到 ~10 次，单帧 ~12 µs。

### 9.2 协议

```c
typedef struct {
    uint64_t address;
    uint32_t size;
    uint32_t output_offset;
    int32_t  status;        // 出参：每条记录的 NTSTATUS
} CS2CHEAT_BATCH_ENTRY;

typedef struct {
    uint32_t pid;
    uint32_t entry_count;
    uint32_t output_size;   // 整个输出缓冲的大小
    uint32_t reserved;
    CS2CHEAT_BATCH_ENTRY entries[1];
} CS2CHEAT_BATCH_READ_REQUEST;
```

调用者计算每条记录的 `output_offset`（一般连续累加），和总 `output_size`。驱动收到后：

1. 快照 entries / output_size / pid 到栈
2. `RtlZeroMemory(outBase, outputSize)`
3. 对每条记录，`MmCopyVirtualMemory(target, addr, current, outBase + offset, size)`
4. 失败的 `RtlZeroMemory` 该区段，用 status 字段记录失败原因

### 9.3 上限

`CS2CHEAT_BATCH_MAX_ENTRIES = 256`：栈快照数组就是这么大。再大需要堆分配。

`CS2CHEAT_READ_MAX = 16 MB`：单次 IOCTL 总输出上限。METHOD_BUFFERED 的 SystemBuffer 是 NonPagedPool，太大会撑爆。16 MB 是合理上限。

### 9.4 用户态 BatchReader

封装在 `batch_reader.hpp`，三段式：

```cpp
BatchReader br;
auto h = br.queue<T>(addr);    // 内部 push BatchEntry，分配 storage 槽位
br.flush();                     // 一次 ReadBatch 调用
T v = br.get<T>(h);             // memcpy 出来
```

注意 `queue` 时不能立刻填 `BatchEntry::out` 指针，因为 `storage_` 还在 push_back，可能 realloc。`flush` 时 storage 已稳定，再统一绑定 `out` 指针。

## 10. 驱动加载机制对比

### 10.1 标准加载（Test-Signing）

```bat
bcdedit /set testsigning on   :: 一次性，需重启
sc create cs2cheat type= kernel binPath= C:\path\cs2cheat-driver.sys
sc start cs2cheat             :: 加载，调 DriverEntry(driver_obj, regpath)
```

- 驱动进入 `PsLoadedModuleList`，`!drvobj` 可见
- 屏幕角落有"测试模式"水印
- 重启后驱动需要重新 `sc start`（除非配置自动启动）

### 10.2 kdmapper 手动映射

利用签名漏洞驱动 `iqvw64e.sys`（Intel 网卡驱动 CVE-2015-2291）。流程：

```
1. kdmapper.exe 把 iqvw64e.sys 临时注册并启动（它是签名的，DSE 放行）
2. 通过 iqvw64e 的"任意内核内存读写"原语，分配一段非分页池
3. 把目标 .sys 解析 PE 头，按节区拷进非分页池
4. 修复重定位表、解析 IAT（导入 ntoskrnl 等模块的导出）
5. 直接 call 目标 .sys 的 entry point
6. 卸载 iqvw64e
```

调用 entry 时 `DriverObject = NULL, RegistryPath = NULL`，因为 kdmapper 没有合法的 driver object 给我们。这就是 `DriverEntry` 必须 NULL-safe 的原因。

**优点**：

- 不需要测试签名，无水印
- 驱动不在 `PsLoadedModuleList`，`sc query` / 模块枚举工具看不见

**缺点**：

- HVCI（Hypervisor-protected Code Integrity）开启时 kdmapper 失效。HVCI 会强制所有 EXECUTE 页只能来自 catalog 签名的镜像，手动映射的内存页拿不到执行权
- VBS（Virtualization-Based Security）开启时同上
- 重启后驱动消失（不持久化）
- DriverObject = NULL 意味着没有 PnP 通知、没有 PowerCallback

我们的 `IoCreateDriver` 自举把 NULL 问题修了，剩下的两个是 BYOVD（Bring Your Own Vulnerable Driver）路线的固有限制。

### 10.3 EFI / Bootkit（Valthrun Zenith）

更激进的方案：在 UEFI 阶段加载一个小型 hypervisor，把 Windows 当成 guest 跑。从 Hypervisor 直接读 cs2 内存，绕过 Windows 内核所有安全机制。

复杂度数量级跳一档：要懂 VT-x / EPT、写 hypervisor、考虑 Secure Boot、调试更难（KD 进不去 hv，要专门工具）。本项目暂不走这条路。

## 11. 安全边界与防御编程

驱动一旦蓝屏，整机重启。即使是学习项目也要写得稳。

### 11.1 输入验证（每个 IOCTL 入口）

```cpp
if (sp->Parameters.DeviceIoControl.InputBufferLength < sizeof(REQUEST))
    return STATUS_BUFFER_TOO_SMALL;
```

每条都加。永远不信任用户态。

### 11.2 地址白名单

`Cs2CopyMemory` 拒绝：

```cpp
if ((ULONG_PTR)userAddress < 0x10000) return STATUS_INVALID_PARAMETER;
if (size > CS2CHEAT_READ_MAX)         return STATUS_INVALID_PARAMETER;
```

- < 0x10000：Windows 保留低地址，不会有合法用户对象
- 上限 16 MB：单次拷贝上限，避免拒绝服务

更严格可以加：地址 < `MmHighestUserAddress`（约 0x7FFF_FFFF_FFFF）拒绝任何疑似内核地址，避免被诱导读内核。本项目没强制，但可作为加固方向。

### 11.3 进程白名单

理想情况是只允许读 `cs2.exe`。当前实现允许任何 PID。加固方向：

```cpp
if (target_pid != cached_cs2_pid) return STATUS_ACCESS_DENIED;
```

或在 `IRP_MJ_CREATE` 阶段检查调用方进程，只允许 `external-cheat-base.exe` 打开设备。

### 11.4 引用计数

`PsLookupProcessByProcessId` 增加 EPROCESS 引用，**必须**配对 `ObDereferenceObject`。否则 cs2 退出时其 EPROCESS 不会释放，驱动卸载时会留下泄漏并在某些时机 BSOD。

代码里所有早返回路径都做了释放。

### 11.5 SEH 包裹

读 PEB / Ldr 链表时用 `__try / __except` 兜底。即使 cs2 正在销毁，PEB 解除映射，访问违例会被捕获返回 `STATUS_NOT_FOUND` 而非蓝屏。

### 11.6 IRQL 与等待

所有 IOCTL 处理都在 `PASSIVE_LEVEL`。`MmCopyVirtualMemory`、`KeStackAttachProcess`、`ZwQuerySystemInformation` 都要求 `PASSIVE_LEVEL`。如果在更高 IRQL（比如 DPC）调会立即蓝屏 `IRQL_NOT_LESS_OR_EQUAL`。本项目没加 DPC，安全。

### 11.7 SystemBuffer 共享

如前述，METHOD_BUFFERED 的输入输出缓冲共享。批量读处理里必须先快照所有要用的请求字段，再开始写输出。`HandleBatchRead` 已这样做。

## 12. 性能模型

驱动方案的性能优势**不来自"内核更快"，而来自"批量摊销路径开销"**。这一节把每个微观成本拆开看，并把 RPM、驱动单读、驱动批量三种方案放在一起对比 — 结论里有几个反直觉的点，先看完再下判断。

### 12.1 单次 IOCTL 成本拆解（Win11 23H2，i7-12700 估算）

驱动方案下走完一次 4 字节读的路径：

```
SYSCALL Ring transition         100 ns
ntoskrnl 派遣                    ~200 ns
IoBuildDeviceIoControlRequest    ~150 ns
我们的 DispatchIoctl + 校验       ~50 ns
PsLookupProcessByProcessId       ~300 ns（哈希查表）
MmCopyVirtualMemory（4 字节）   ~500 ns
ObDereferenceObject              ~100 ns
IoCompleteRequest                ~200 ns
SYSRET Ring transition            100 ns
─────────────────────────────────────────
总计                            ~1.7 µs
```

实测可能在 1–3 µs 间。

### 12.2 与 RPM 单次读的对比

`ReadProcessMemory` 走的是同一个 `MmCopyVirtualMemory`，但路径更短：进程已 `OpenProcess` 拿到 EPROCESS 引用，不需要 `PsLookupProcessByProcessId`；不走 IRP，直接是 `NtReadVirtualMemory` 的内核实现。

| 操作 | RPM | 驱动单读 IOCTL |
|------|-----|----------------|
| Ring 切换（syscall） | 1 次 (~100 ns) | 1 次 (~100 ns) |
| ntoskrnl 派遣 | 直接 `NtReadVirtualMemory` | `NtDeviceIoControlFile` → IRP → 我们的派遣 |
| 进程查找 | 句柄 → EPROCESS（O(1) 查表） | `PsLookupProcessByProcessId`（同 O(1)） |
| 实际拷贝 | `MmCopyVirtualMemory` | **同一函数** |
| 单次总耗时 | **~1.0–1.5 µs** | **~1.5–2.5 µs** |

**单看一次小读，驱动方案反而比 RPM 慢约 0.5–1 µs**。这是反直觉的点 — 不少人以为"内核态肯定快"，但我们多了一层 IRP 包装、多了一次 `PsLookupProcessByProcessId`、多了一次 SystemBuffer 拷贝。

驱动方案的性能上限就是 RPM 的性能上限（同一个底层 `MmCopyVirtualMemory`），区别只在路径开销。**只做 Phase 2（IOCTL 替换 RPM 单读）时，性能是退化的**。Phase 4 批量读才是性能拐点。

### 12.3 批量读优势

批量读把 N 条记录一次 IOCTL 处理。`PsLookupProcessByProcessId` / `ObDereferenceObject` / Ring 切换 / IRP 派遣等"路径成本"摊销到 N 条记录上。每条记录只多 `MmCopyVirtualMemory` ~500 ns + 校验 ~50 ns。

```
单次批量读（16 entries）= 200 + 300 + 16*(50+500) + 100 + 200 = ~9.4 µs
对比 16 次单读           = 16 * 1.7 µs                       = ~27 µs
对比 16 次 RPM           = 16 * 1.2 µs                       = ~19 µs
```

| 方案 | 16 个离散字段 | 加速比（vs 16 次 RPM） |
|------|---------------|------------------------|
| 16 次 RPM 单读 | ~19 µs | 1× |
| 16 次驱动单读 IOCTL | ~27 µs | 0.7×（**比 RPM 慢**） |
| **1 次驱动批量 IOCTL** | **~9.4 µs** | **2.0×** |

注意 RPM 也能批量化（连续地址段一次读大块），但 cs2 实体的字段（health/feet/viewOffset/eyeAngles）在结构体里**不连续**，没法合一发。这就是驱动批量协议的真正价值：**地址离散也能合并**。

### 12.4 实际单帧 ESP 负载

`esp::updateEntities` 一帧的实际操作（10 个敌人估算）：

| 路径 | 之前（RPM 单读，所有路径） | 现在（驱动+批量） |
|------|---------------------------|-------------------|
| 视图矩阵读 | 1 次 | 1 次单读 IOCTL |
| 本地玩家 (pos / voffset / eye) | 3 次单读 | **1 次批量 (3 entries)** |
| 每敌人 fast-path（5 字段） | 5 次单读 × 10 = 50 次 | **1 次批量 × 10 = 10 次** |
| 每敌人骨骼链（3 跳指针 + 28 帧） | 4 次 × 10 = 40 次 | 同左（跨指针链没批量化） |
| 慢路径武器 / flash（8 帧 1 次） | 摊销 ~1 次/帧 | 摊销 ~1 次/帧 |
| 世界扫描（4 帧 1 次） | 摊销 ~10 次/帧 | 摊销 ~10 次/帧 |
| **每帧操作数** | **~105 次单读** | **~22 次（其中 11 次是批量）** |
| **每帧总耗时** | **~110–160 µs** | **~30–50 µs** |

**约 3–5× 加速**。10 个敌人是常见值；满人房（30+ 个实体含队友）差距会更大。

### 12.5 数据线程吞吐

`main.cpp` 里数据线程是 `while (running) { esp::updateEntities(); ... }` 死循环，跑得越快敌人状态越新。

| 后端 | 单次 updateEntities 耗时 | 理论最大刷新率 |
|------|--------------------------|----------------|
| RPM 单读（之前） | ~120 µs | ~8 kHz |
| 驱动单读（仅 Phase 2） | ~150 µs | ~6 kHz |
| 驱动 + 批量（Phase 4） | **~40 µs** | **~25 kHz** |

实际不会跑这么高 — cs2 内存数据本身有 tick rate 限制（64-128 Hz），更高频读就是读到同一份数据。但**对鼠标移动响应延迟（aimbot 平滑）等微观时序敏感的功能**，更高的采样率确实更跟手。

更现实的影响是 **CPU 占用**：

| 后端 | 数据线程 CPU 占用（单核） |
|------|---------------------------|
| RPM 单读 | ~30–40% |
| 驱动 + 批量 | **< 5%** |

省下来的 CPU 留给渲染、aimbot 鼠标插值、其他线程。

### 12.6 ESP 帧预算

60 FPS = 16.6 ms / 帧。数据线程独立于渲染线程，可以跑得更频繁。即便 200 Hz 数据采样也只需要 5 ms / 周期，IOCTL 总开销 ~30 µs 占用不到 0.6%。渲染帧率几乎不受影响（渲染线程读的是已缓存的 `enemies` vector，不碰 IOCTL）。

### 12.7 反直觉要点（重要）

- **驱动不是无脑快**：单次小读驱动比 RPM 慢，批量化才赢。Phase 2 单独存在时是性能退化。
- **底层是同一个函数**：`ReadProcessMemory` 与我们驱动里都最终调 `MmCopyVirtualMemory`。"内核读更快"的说法只在批量摊销下成立。
- **渲染帧率没受益**：渲染线程不碰内存读，FPS 与方案无关；性能差异全在数据线程的 CPU 占用与刷新率。
- **缓存友好性**：批量读把 16 个小拷贝集中到一次内核停留，CPU 缓存（L1/L2）被换出的概率小很多；RPM 16 次单读期间每次 ring transition 都污染 cache，整体延迟比裸算术更难预测。
- **绝对值都很小**：3–5× 加速听起来大，但绝对差距是 100 µs 量级，对人眼根本看不出。**真要在线用，主要价值是隐蔽性 + 较低 CPU 占用，"快多少"不是关键**。

### 12.8 三方案对比总览

| 维度 | RPM | 驱动 (Phase 2) | 驱动+批量 (Phase 4) |
|------|-----|----------------|---------------------|
| 单次 4 字节读 | 1.0–1.5 µs | 1.5–2.5 µs ⬇ | 同左 |
| 16 字段批量读 | 19–24 µs | 24–40 µs ⬇ | **9–12 µs ⬆** |
| ESP 单帧 (10 敌人) | 110–160 µs | 150–200 µs ⬇ | **30–50 µs ⬆** |
| 数据线程 CPU 占用 | ~35% | ~40% | **< 5%** |
| 隐蔽性（与性能无关） | 低（句柄扫描可见） | 高 | 高 |
| 实现复杂度 | 极低 | 中 | 中-高 |

**实战建议**：

- 学习目的、想要最简单：RPM 完全够用
- 想体会内核驱动 + 批量化设计：当前方案是合适的练手
- 真要发布 / 长时使用：驱动 + 批量是合理选择，主要价值是隐蔽性 + 较低 CPU 占用

## 13. 可观测性：调试与排错

### 13.1 内核日志

驱动用 `DbgPrintEx` 输出：

```cpp
DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
           "cs2cheat: Read pid=%u addr=%p size=%u status=0x%x\n",
           pid, addr, size, status);
```

用 [DebugView from Sysinternals](https://learn.microsoft.com/sysinternals/downloads/debugview) 以管理员模式 + 勾 *Capture Kernel* 实时看输出。

发布版编译时 `DbgPrintEx` 的 INFO 级别默认被过滤，需要：

```cmd
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter" /v DEFAULT /t REG_DWORD /d 0xFFFFFFFF
```

然后重启。

### 13.2 内核调试器

要看寄存器、堆栈、断点：用 WinDbg + 串口 / 1394 / 网络调试。常用方式（生产/便携笔记本）是配 KDNET：

宿主机 Win11，目标机 Win11：

```cmd
:: 在目标机运行
bcdedit /debug on
bcdedit /dbgsettings net hostip:192.168.1.10 port:50000

:: 在宿主机 WinDbg → Attach to Kernel → Net
```

宿主机看到 `kd>` 提示符就成功了。`!process 0 0 cs2.exe` 看 cs2 EPROCESS，`!devstack \Device\cs2cheat` 看我们的设备栈。

### 13.3 Driver Verifier

调试期强烈推荐开 Driver Verifier：

```cmd
verifier /standard /driver cs2cheat.sys
```

会强制做更多 sanity check：池标签、引用计数、IRQL 切换、IRP 完成。如果驱动有泄漏或非法操作，BSOD 时能给出明确诊断。

发布前关闭：

```cmd
verifier /reset
```

## 14. 对抗与检测

### 14.1 用户态痕迹

迁移到驱动后，cheat 进程不再 `OpenProcess(cs2)`。但仍有：

- `\\.\cs2cheat` 设备句柄（用户态扫描可见，特征明显）
- `external-cheat-base.exe` 进程本身（窗口标题、文件名、imports）

加固方向（本项目未做）：

- 用 GUID 形式设备名 `\Device\{ABCDEF...}`，每次构建不同
- 进程名随机化
- 把 cheat exe 的 imports 也走驱动（即不直接 import `ntdll!NtDeviceIoControlFile`，让驱动给个不一样的 IPC）

### 14.2 内核痕迹

- 标准加载：`PsLoadedModuleList` 可见、`\Driver\cs2cheat` 可见、ETW provider 注册痕迹
- kdmapper：不在 `PsLoadedModuleList`，但 PiDDBCacheTable 有 iqvw64e 的加载记录，反作弊会扫这个

进一步隐藏：

- 加载后从 PiDDBCacheTable 删除 iqvw64e 记录
- 抹掉 `MmUnloadedDrivers` 中的痕迹
- 解钩 KdpDebugRoutine 等被反作弊钩的位置

这些都是攻防游戏永远在演化，不在本项目目标范围内。

### 14.3 行为痕迹

即便代码完全隐身，行为还是露馅：

- 鼠标自动归位轨迹（aimbot 平滑度不够 → 反作弊统计学检测）
- 透视一直识别墙后敌人然后立即对着墙开火
- 反应时间分布异常（人类反应 200-400 ms，aimbot < 50 ms）

行为反检测属于 AI 与启发式范畴，本项目不涉及。

## 15. 参考资料

### 内核开发

- [Windows Internals 第 7 版 上下卷](https://www.microsoftpressstore.com/store/windows-internals-part-1-system-architecture-processes-9780735684188) — Mark Russinovich，必读
- [Windows Kernel Programming](https://leanpub.com/windowskernelprogrammingsecondedition) — Pavel Yosifovich，第二版有 KMDF 与 WDM 完整示例
- [Microsoft Learn: Kernel-Mode Driver Architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/) — 官方文档

### 内核 API

- [MmCopyVirtualMemory（无官方文档，社区逆向汇总）](https://www.unknowncheats.me/forum/anti-cheat-bypass/) — UnknownCheats 论坛常有讨论
- [PsLookupProcessByProcessId 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-pslookupprocessbyprocessid)
- [ZwQuerySystemInformation 文档](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation)

### 加载技术

- [TheCruZ/kdmapper 源码](https://github.com/TheCruZ/kdmapper)
- [Valthrun 项目](https://github.com/Valthrun/valthrun-cs2)
- [Hyperspace bootkit / EFI Mapper（高级）](https://github.com/) — 各种 PoC

### 调试

- [WinDbg Cheatsheet](https://gist.github.com/) — 常用命令速查
- [DebugView for Windows](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview)
- [Microsoft Learn: Debug Universal Windows Drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/)

### 反作弊技术

- [secret.club](https://secret.club) — 反作弊与反逆向研究博客
- [DON.T.WORRY.JUST.RELAX 反作弊系列](https://www.unknowncheats.me/forum/anti-cheat-bypass/)

---

读到这里，你应该能：

- 解释从 `memory::Read<T>(addr)` 一行到 cs2 内存读出的每一跳
- 说出为什么用 `METHOD_BUFFERED` 而不是 `METHOD_NEITHER`
- 知道 kdmapper 与 sc create 路线下 `DriverEntry` 的差异
- 看懂 `Cs2CopyMemory` 里每行的安全意图
- 评估改造方向（缩小白名单、加 ETW 监听、批量写、hypervisor 化）的成本

下一步可以读 `cs2cheat-driver/driver.cpp` 全文，按本文章节对照每个函数的设计。
