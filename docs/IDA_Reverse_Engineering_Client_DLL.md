# 使用IDA逆向CS:GO/CS2 Client.dll 技术指南

## 目录
1. [为什么client.dll会提供偏移量](#为什么clientdll会提供偏移量)
2. [为什么要Rebase Image Base为0](#为什么要rebase-image-base为0)
3. [解析网络变量元数据结构](#解析网络变量元数据结构)
4. [特征码（Pattern/Signature）原理](#特征码patternsignature原理)

---

## 为什么client.dll会提供偏移量

### 核心原理

C++类的成员变量在内存中的位置是固定的偏移量。编译后，访问成员变量会变成：

```
基地址 + 偏移量 = 目标数据地址
```

**示例：**
```cpp
int health = player->m_iHealth;  // C++代码
```
```asm
mov eax, [rcx+0x334]  ; 汇编：rcx是player指针，0x334是偏移
```

### 如何获取偏移量

1. **搜索字符串**：在IDA中按 `Shift+F12`，搜索变量名（如 "m_iHealth"）
2. **查看交叉引用**：按 `X` 查看哪些函数使用了这个字符串
3. **分析汇编代码**：在函数中找到访问该变量的指令，读取偏移值
4. **验证准确性**：在多个函数中确认偏移量一致

---

## 为什么要Rebase Image Base为0

### DLL加载机制

#### 什么是Image Base

**Image Base（映像基址）**是PE文件（Windows可执行文件/DLL）在编译时指定的**首选加载地址**。

**PE文件头中的定义：**
```
PE Header → Optional Header → ImageBase
- 32位程序：通常是 0x10000000 或 0x00400000
- 64位程序：通常是 0x140000000 或 0x180000000
```

#### 为什么每次加载的基址可能不同

**原因1：ASLR（地址空间布局随机化）**

ASLR是Windows的安全机制，用于防止缓冲区溢出攻击。

```
第一次运行 CS2:
  client.dll 加载到 → 0x00007FF800000000

第二次运行 CS2:
  client.dll 加载到 → 0x00007FF820000000  ← 地址变了！

第三次运行 CS2:
  client.dll 加载到 → 0x00007FF7E0000000  ← 又变了！
```

**ASLR的工作原理：**
- Windows在加载DLL时，随机选择一个内存地址
- 每次启动程序，DLL的加载地址都不同
- 目的是让攻击者无法预测代码/数据的位置

**原因2：地址冲突**

如果多个DLL的首选基址相同，Windows会自动重定位：

```
情况：两个DLL都想加载到 0x140000000

DLL A (client.dll):  首选基址 0x140000000 → 成功加载
DLL B (engine.dll):  首选基址 0x140000000 → 冲突！
                     → Windows自动重定位到 0x150000000
```

**原因3：内存碎片**

系统内存可能已经被其他进程占用：

```
首选基址 0x140000000 已被占用
  ↓
Windows 查找可用内存区域
  ↓
加载到 0x7FF800000000
```

#### 为什么偏移量不会变

虽然基址每次都变，但**相对偏移量永远不变**：

```
第一次运行：
  基址: 0x7FF800000000
  函数A: 0x7FF800123456
  偏移: 0x123456 ✅

第二次运行：
  基址: 0x7FF820000000
  函数A: 0x7FF820123456
  偏移: 0x123456 ✅ (相同！)
```

### Rebase到0的优势

**对比：**
```
不Rebase:  IDA地址 0x140A12334 - Image Base 0x140000000 = 偏移 0xA12334
Rebase到0: IDA地址 0x00A12334 - Image Base 0x00000000 = 偏移 0xA12334 ✅
```

**优势：**

1. **简化计算**：地址 = 偏移量，无需手动减去基址
2. **避免ASLR混淆**：偏移量永远不变，不受运行时加载地址影响
3. **便于代码使用**：`clientBase + 0xA12334` 直接复制IDA中的地址
4. **统一标准**：所有逆向工程师使用相同的地址表示

### 操作方法

1. IDA菜单：`Edit` → `Segments` → `Rebase program...`
2. 输入新基址：`0`
3. 点击OK

### 实际应用示例

```cpp
// 运行时获取实际基址
uintptr_t clientBase = GetModuleBase("client.dll");
// 可能是 0x7FF800000000 或其他任何地址

// 使用IDA中的偏移量（Rebase到0后）
uintptr_t functionOffset = 0xA12334;

// 计算实际地址
uintptr_t actualAddress = clientBase + functionOffset;
// = 0x7FF800000000 + 0xA12334
// = 0x7FF800A12334 ✅
```

---

## 解析网络变量元数据结构

### 核心概念

CS2使用**网络变量元数据表**来描述需要同步的游戏数据。每个条目包含变量名、类型、偏移量等信息。

### 元数据结构（每条目约32-40字节）

```
+0x00: 网络属性指针 (8字节) → "MNetworkEnable"
+0x08: 变量名指针 (8字节)   → "m_iPawnArmor"
+0x10: 字段类型 (1字节)     → 0x0A (int32)
+0x11: 填充 (7字节)
+0x18: 标志位 (1字节)       → 0xF0
+0x19: 偏移量 (2字节) ⭐    → 小端序存储
+0x1B: 其他字段
```

### 偏移量提取方法

**IDA数据示例：**
```
.data:000000000201F520  dq offset aMIpawnarmor  ; "m_iPawnArmor"
.data:000000000201F528  db 0Ah                  ; 类型
.data:000000000201F530  db 1Ch                  ; 偏移量低字节
.data:000000000201F531  db 09h                  ; 偏移量高字节
```

**计算（小端序）：**
```
偏移量 = (高字节 << 8) | 低字节
       = (0x09 << 8) | 0x1C
       = 0x091C
```

### 快速对照表

| 变量名 | IDA字节 | 计算 | 偏移 |
|--------|---------|------|------|
| m_iPawnArmor | `1Ch, 09h` | `(0x09<<8)\|0x1C` | 0x091C |
| m_angEyeAngles | `D0h, 3Dh` | `(0x3D<<8)\|0xD0` | 0x3DD0 |

---

## 特征码（Pattern/Signature）原理

### 什么是特征码

**特征码（Pattern/Signature）**是一段唯一的字节序列，用于在内存中定位特定的代码或数据。即使游戏更新导致地址变化，特征码仍然可以找到目标位置。

### 为什么需要特征码

游戏每次更新后：
- ✅ **偏移量可能不变**（成员变量位置相对稳定）
- ❌ **绝对地址会变**（代码位置、全局变量地址等）

**解决方案**：使用特征码动态查找地址

### 汇编指令与特征码的关系

#### 示例：查找EntityList地址

假设在IDA中找到这样的汇编代码：

```asm
.text:0000000000147156  mov     rax, cs:qword_24ABF98
.text:000000000014715D  mov     [rsp+0D0h+arg_10], rdi
.text:0000000000147165  mov     rbx, [rax+210h]
.text:000000000014716C  test    rbx, rbx
```

**IDA显示：**
```
地址: 0000000000147156
指令: mov rax, cs:qword_24ABF98
```

其中 `qword_24ABF98` 就是 **EntityList** 的地址（全局变量）。

---

### 汇编指令详解

#### 指令：`mov rax, cs:qword_24ABF98`

让我们逐个分解这条指令的每个部分：

```
mov     rax,    cs:     qword_      24ABF98
 ↓       ↓       ↓         ↓           ↓
操作码  目标寄存器 段寄存器  数据类型    地址/标签
```

**1. `mov` - 操作码（Opcode）**
- 含义：Move（移动/复制数据）
- 功能：将源操作数复制到目标操作数
- 格式：`mov 目标, 源`

**2. `rax` - 目标寄存器**
- 64位通用寄存器（见下文寄存器详解）
- 用于存储数据、函数返回值等

**3. `cs:` - 段寄存器前缀**
- **CS = Code Segment（代码段寄存器）**
- 在x64模式下，段寄存器主要用于兼容性
- 现代x64程序中，`cs:` 通常只是语法标记，实际使用平坦内存模型
- IDA显示 `cs:` 表示这是一个全局变量/代码段中的数据

**4. `qword_` - 数据类型标识**
- **QWORD = Quad Word = 8字节 = 64位**
- IDA的命名约定：
  - `byte_` = 1字节 (8位)
  - `word_` = 2字节 (16位)
  - `dword_` = 4字节 (32位)
  - `qword_` = 8字节 (64位)

**5. `24ABF98` - 地址/标签**
- 这是IDA给全局变量自动生成的标签
- 实际含义：地址 `0x24ABF98` 处的数据
- 在Rebase到0后，这就是偏移量

**完整含义：**
```
将地址 0x24ABF98 处的 8字节数据 读取到 rax 寄存器中
```

---

### x64 寄存器详解

#### 通用寄存器（General Purpose Registers）

x64架构有16个64位通用寄存器：

| 64位 | 32位 | 16位 | 8位高 | 8位低 | 主要用途 |
|------|------|------|-------|-------|---------|
| **RAX** | EAX | AX | AH | AL | 累加器，函数返回值 |
| **RBX** | EBX | BX | BH | BL | 基址寄存器 |
| **RCX** | ECX | CX | CH | CL | 计数器，第1个参数 |
| **RDX** | EDX | DX | DH | DL | 数据寄存器，第2个参数 |
| **RSI** | ESI | SI | - | SIL | 源索引，第3个参数 |
| **RDI** | EDI | DI | - | DIL | 目的索引，第4个参数 |
| **RBP** | EBP | BP | - | BPL | 栈帧基址指针 |
| **RSP** | ESP | SP | - | SPL | 栈顶指针 |
| **R8** | R8D | R8W | - | R8B | 第5个参数 |
| **R9** | R9D | R9W | - | R9B | 第6个参数 |
| **R10** | R10D | R10W | - | R10B | 临时寄存器 |
| **R11** | R11D | R11W | - | R11B | 临时寄存器 |
| **R12** | R12D | R12W | - | R12B | 保留寄存器 |
| **R13** | R13D | R13W | - | R13B | 保留寄存器 |
| **R14** | R14D | R14W | - | R14B | 保留寄存器 |
| **R15** | R15D | R15W | - | R15B | 保留寄存器 |

**寄存器命名规则：**
```
RAX (64位)
 └─ EAX (低32位)
     └─ AX (低16位)
         ├─ AH (高8位)
         └─ AL (低8位)
```

#### 特殊寄存器

| 寄存器 | 全称 | 用途 |
|--------|------|------|
| **RIP** | Instruction Pointer | 指令指针，指向下一条要执行的指令 |
| **RFLAGS** | Flags Register | 标志寄存器，存储CPU状态 |

#### 段寄存器（Segment Registers）

| 寄存器 | 全称 | 用途 |
|--------|------|------|
| **CS** | Code Segment | 代码段 |
| **DS** | Data Segment | 数据段 |
| **SS** | Stack Segment | 栈段 |
| **ES** | Extra Segment | 附加段 |
| **FS** | - | 线程局部存储（TLS） |
| **GS** | - | 线程局部存储（TLS） |

**注意：** 在x64模式下，段寄存器主要用于兼容性，除了FS/GS用于TLS外，其他段寄存器很少使用。

#### 常见寄存器用途示例

```asm
; 函数调用约定（Windows x64）
mov rcx, arg1    ; 第1个参数
mov rdx, arg2    ; 第2个参数
mov r8, arg3     ; 第3个参数
mov r9, arg4     ; 第4个参数
call function
; rax 存储返回值

; 栈操作
push rax         ; 将rax压入栈，rsp -= 8
pop rbx          ; 从栈弹出到rbx，rsp += 8

; 循环计数
mov rcx, 10      ; 循环10次
loop_start:
  ; ... 循环体 ...
  dec rcx        ; rcx--
  jnz loop_start ; 如果rcx != 0，跳转
```

---

### x64 寻址方式

#### 1. **立即数寻址（Immediate Addressing）**

直接使用常量值：

```asm
mov rax, 100        ; rax = 100
mov rbx, 0x1234     ; rbx = 0x1234
```

#### 2. **寄存器寻址（Register Addressing）**

使用寄存器中的值：

```asm
mov rax, rbx        ; rax = rbx
add rcx, rdx        ; rcx = rcx + rdx
```

#### 3. **直接寻址（Direct Addressing）**

直接访问内存地址：

```asm
mov rax, [0x12345678]     ; rax = *(uint64_t*)0x12345678
mov [0x12345678], rbx     ; *(uint64_t*)0x12345678 = rbx
```

#### 4. **寄存器间接寻址（Register Indirect Addressing）**

使用寄存器中的值作为地址：

```asm
mov rax, [rbx]      ; rax = *(uint64_t*)rbx
mov [rcx], rdx      ; *(uint64_t*)rcx = rdx
```

**C++等价代码：**
```cpp
uint64_t* ptr = (uint64_t*)rbx;
rax = *ptr;
```

#### 5. **基址+偏移寻址（Base + Displacement）**

寄存器值 + 常量偏移：

```asm
mov rax, [rbx+0x10]     ; rax = *(uint64_t*)(rbx + 0x10)
mov [rcx+0x20], rdx     ; *(uint64_t*)(rcx + 0x20) = rdx
```

**C++等价代码：**
```cpp
struct Player {
    int health;      // +0x00
    int armor;       // +0x04
    Vector3 pos;     // +0x08
};

Player* player = (Player*)rbx;
rax = player->armor;  // mov rax, [rbx+0x04]
```

#### 6. **基址+索引寻址（Base + Index）**

两个寄存器相加：

```asm
mov rax, [rbx+rcx]      ; rax = *(uint64_t*)(rbx + rcx)
```

**C++等价代码：**
```cpp
uint64_t* array = (uint64_t*)rbx;
int index = rcx;
rax = array[index];
```

#### 7. **基址+索引*比例+偏移（Base + Index*Scale + Displacement）**

最复杂的寻址方式：

```asm
mov rax, [rbx+rcx*8+0x10]
; rax = *(uint64_t*)(rbx + rcx*8 + 0x10)
```

**比例因子（Scale）：** 只能是 1, 2, 4, 8

**C++等价代码：**
```cpp
struct Entity {
    uint64_t data[10];
};

Entity* base = (Entity*)rbx;
int index = rcx;
rax = base->data[index];  // 每个元素8字节
```

#### 8. **RIP相对寻址（RIP-Relative Addressing）** ⭐

x64特有，相对于指令指针的偏移：

```asm
mov rax, [rip+0x1234]
; rax = *(uint64_t*)(rip + 0x1234)
```

**实际例子：**
```asm
; 当前指令地址: 0x147156
mov rax, cs:qword_24ABF98
; 机器码: 48 8B 05 3B 4E 36 02
; 实际是: mov rax, [rip+0x02364E3B]

计算：
  下一条指令: 0x147156 + 7 = 0x14715D
  目标地址: 0x14715D + 0x02364E3B = 0x24ABF98
```

**为什么使用RIP相对寻址？**
- 代码位置无关（Position Independent Code）
- 支持ASLR
- 访问全局变量更高效

---

### 寻址方式对照表

| 寻址方式 | 汇编语法 | C++等价 | 用途 |
|---------|---------|---------|------|
| 立即数 | `mov rax, 100` | `rax = 100` | 常量赋值 |
| 寄存器 | `mov rax, rbx` | `rax = rbx` | 寄存器间传值 |
| 直接 | `mov rax, [0x1234]` | `rax = *(uint64_t*)0x1234` | 访问固定地址 |
| 间接 | `mov rax, [rbx]` | `rax = *ptr` | 指针解引用 |
| 基址+偏移 | `mov rax, [rbx+0x10]` | `rax = obj->member` | 访问结构体成员 |
| 基址+索引 | `mov rax, [rbx+rcx]` | `rax = array[i]` | 数组访问 |
| 完整 | `mov rax, [rbx+rcx*8+0x10]` | `rax = obj->array[i]` | 复杂数据结构 |
| RIP相对 | `mov rax, [rip+0x1234]` | `rax = globalVar` | 访问全局变量 |

### 特征码的构成

#### 1. **查看机器码（Opcode）**

汇编指令会被编译成机器码（字节序列）。在IDA中查看：

**方法**：在汇编指令上按 `Space` 或 `Tab` 切换到十六进制视图

**实际示例（真实数据）：**
```
地址        机器码                    汇编指令
147156      48 8B 05 3B 4E 36 02      mov rax, cs:qword_24ABF98
14715D      48 89 BC 24 F0 00 00 00   mov [rsp+0D0h+arg_10], rdi
147165      48 8B 98 10 02 00 00      mov rbx, [rax+210h]
14716C      48 85 DB                  test rbx, rbx
```

**完整的十六进制数据：**
```
48 8B 05 3B 4E 36 02 48 89 BC 24 F0 00 00 00 48
8B 98 10 02 00 00 48 85 DB 0F 84 E2 00 00 00 66
66 66 0F 1F 84 00 00 00 00 00
```

#### 2. **特征码格式**

```
48 8B 05 ?? ?? ?? ?? 48 89 BC 24 ?? ?? ?? ?? 48 8B 98 10 02 00 00
```

**解释：**
- `48 8B 05` - 固定的操作码（mov rax, [rip+offset]）
- `?? ?? ?? ??` - 通配符（地址偏移，会变化）
- `48 89 BC 24` - 固定的操作码（mov [rsp+offset], rdi）
- `?? ?? ?? ??` - 通配符（栈偏移，可能变化）
- `48 8B 98 10 02 00 00` - 固定的操作码（mov rbx, [rax+210h]）

**为什么使用通配符？**
- 地址偏移在每次编译/更新后会变化
- 但指令的操作码（opcode）通常保持不变
- 通配符 `??` 表示"任意字节"，跳过可变部分
- `10 02 00 00` (0x210) 是固定的偏移量，不需要通配符

### 特征码计算公式

#### 公式1：RIP相对寻址（x64常见）

```asm
mov rax, cs:qword_24ABF98
```

**机器码：** `48 8B 05 3B 4E 36 02`

**x64 RIP相对寻址公式：**
```
目标地址 = 当前指令地址 + 指令长度 + 偏移量
```

**详细计算（真实数据）：**
```
当前指令地址: 0x0000000000147156
指令长度:     7 字节 (48 8B 05 3B 4E 36 02)
偏移量:       0x02364E3B (小端序: 3B 4E 36 02)

步骤1: 计算下一条指令地址
       0x0000000000147156 + 7 = 0x000000000014715D

步骤2: 加上偏移量
       0x000000000014715D + 0x02364E3B = 0x0000000024ABF98

结果: EntityList 地址 = 0x24ABF98 ✅
```

**重要说明：**
- **RIP（Instruction Pointer）**：x64架构的指令指针寄存器
- **相对寻址**：地址是相对于下一条指令的偏移
- **下一条指令地址** = 当前指令地址 + 指令长度
- **小端序**：`3B 4E 36 02` 读取为 `0x02364E3B`

#### 公式2：绝对寻址（x86常见）

```asm
mov eax, dword_12345678
```

**机器码：** `A1 78 56 34 12`

**计算：**
```
目标地址 = 78 56 34 12 (小端序)
         = 0x12345678
```

### 实战案例：提取EntityList特征码

#### 步骤1：在IDA中找到访问EntityList的代码

假设你已经知道EntityList的地址是 `0x24ABF98`（通过字符串搜索或其他方法）。

**方法：**
1. 在IDA中按 `G`，跳转到 `0x24ABF98`
2. 按 `X` 查看交叉引用（Xref）
3. 找到一个访问该地址的函数

#### 步骤2：分析汇编代码

找到这样的代码：

```asm
.text:0000000000147156  mov     rax, cs:qword_24ABF98
.text:000000000014715D  mov     [rsp+0D0h+arg_10], rdi
.text:0000000000147165  mov     rbx, [rax+210h]
.text:000000000014716C  test    rbx, rbx
```

#### 步骤3：提取机器码

在IDA中选中这几行代码，按 `Space` 切换到十六进制视图：

```
147156: 48 8B 05 3B 4E 36 02
14715D: 48 89 BC 24 F0 00 00 00
147165: 48 8B 98 10 02 00 00
14716C: 48 85 DB
```

#### 步骤4：构造特征码

**方法1：短特征码（仅关键指令）**
```
48 8B 05 ?? ?? ?? ?? 48 89 BC 24
```

**方法2：长特征码（包含更多上下文，推荐）**
```
48 8B 05 ?? ?? ?? ?? 48 89 BC 24 ?? ?? ?? ?? 48 8B 98 10 02 00 00
```

**为什么这样构造？**
- `48 8B 05` - mov rax, [rip+offset] 的固定操作码
- `?? ?? ?? ??` - 偏移量会变，用通配符
- `48 89 BC 24` - mov [rsp+offset], rdi 的固定操作码
- `?? ?? ?? ??` - 栈偏移可能变，用通配符
- `48 8B 98 10 02 00 00` - mov rbx, [rax+210h] 固定（0x210是常量）

**为什么 `10 02 00 00` 不用通配符？**
- 这是 `[rax+210h]` 中的 `0x210` 偏移量
- 这是访问 EntityList 结构体的固定成员偏移
- 除非游戏内部结构改变，否则这个值不会变

#### 步骤5：计算EntityList地址

在代码中扫描到特征码后，需要计算实际地址：

```cpp
// 假设找到特征码的地址
uintptr_t patternAddress = 0x0000000000147156;

// 读取偏移量（小端序，4字节）
// +3 是跳过操作码 48 8B 05，从第4个字节开始读取
int32_t offset = *(int32_t*)(patternAddress + 3);
// offset = 0x02364E3B

// 计算下一条指令地址
uintptr_t instructionEnd = patternAddress + 7;  // 指令长度7字节
// instructionEnd = 0x000000000014715D

// 计算目标地址（RIP相对寻址）
uintptr_t entityListAddress = instructionEnd + offset;
// entityListAddress = 0x000000000014715D + 0x02364E3B
//                   = 0x0000000024ABF98 ✅

printf("EntityList 地址: 0x%llX\n", entityListAddress);
```

**验证：**
```
当前指令: 0x147156
指令长度: 7
偏移量:   0x02364E3B (从机器码 3B 4E 36 02 读取)
计算:     0x147156 + 7 + 0x02364E3B = 0x24ABF98 ✅
```

### 特征码的优缺点

#### ✅ 优点

1. **适应游戏更新**：即使代码位置变化，特征码仍能找到
2. **自动化**：可以编写扫描器自动查找
3. **稳定性**：编译器生成的代码模式相对稳定

#### ❌ 缺点

1. **可能失效**：编译器优化或代码重构会改变特征
2. **扫描开销**：需要遍历内存查找匹配
3. **误匹配**：特征码太短可能匹配到错误位置

### 如何选择好的特征码

#### 原则

1. **足够长**：至少10-15字节，避免误匹配
2. **足够独特**：包含特殊的指令组合
3. **足够稳定**：避免使用容易变化的代码段
4. **包含上下文**：不仅仅是目标指令，还包括前后的代码

#### 示例对比

**❌ 不好的特征码（太短）：**
```
48 8B 05 ?? ?? ?? ??
```
- 太常见，可能匹配到很多地方
- `mov rax, [rip+offset]` 在代码中出现频率很高

**✅ 好的特征码（包含上下文）：**
```
48 8B 05 ?? ?? ?? ?? 48 89 BC 24 ?? ?? ?? ?? 48 8B 98 10 02 00 00
```
- 包含完整的逻辑：读取EntityList → 保存寄存器 → 访问成员[+0x210]
- 更独特，不容易误匹配
- 包含特定的偏移量 `10 02 00 00` (0x210) 作为特征

---

## 总结

### 核心知识点

| 概念 | 说明 | 稳定性 |
|------|------|--------|
| **偏移量（Offset）** | 类成员变量的相对位置 | ⭐⭐⭐⭐⭐ 非常稳定 |
| **绝对地址（Address）** | 全局变量/函数的内存地址 | ⭐ 每次更新都变 |
| **特征码（Pattern）** | 用于查找绝对地址的字节序列 | ⭐⭐⭐⭐ 相对稳定 |

### 工作流程

```
1. 使用IDA分析client.dll
   ↓
2. Rebase到0，简化地址计算
   ↓
3. 查找偏移量（成员变量）
   ↓
4. 提取特征码（全局变量/函数）
   ↓
5. 在代码中使用特征码扫描
   ↓
6. 计算实际地址
```

### 实际应用

```cpp
// 1. 使用特征码查找EntityList
uintptr_t FindEntityList() {
    // 使用真实的特征码
    const char* pattern = "48 8B 05 ?? ?? ?? ?? 48 89 BC 24 ?? ?? ?? ?? 48 8B 98 10 02 00 00";
    uintptr_t address = PatternScan(pattern);

    if (!address) {
        printf("特征码未找到！\n");
        return 0;
    }

    // 2. 计算实际地址（RIP相对寻址）
    // +3 跳过操作码 48 8B 05
    int32_t offset = *(int32_t*)(address + 3);

    // 下一条指令地址 = 当前地址 + 指令长度(7)
    uintptr_t instructionEnd = address + 7;

    // 目标地址 = 下一条指令地址 + 偏移量
    uintptr_t entityList = instructionEnd + offset;

    printf("特征码地址: 0x%llX\n", address);
    printf("偏移量: 0x%X\n", offset);
    printf("EntityList: 0x%llX\n", entityList);

    return entityList;
}

// 3. 使用偏移量读取玩家数据
int GetPlayerHealth(uintptr_t playerPtr) {
    return *(int*)(playerPtr + 0x334);  // m_iHealth偏移
}

// 4. 完整示例
void Example() {
    // 查找EntityList
    uintptr_t entityList = FindEntityList();

    // 读取第一个实体
    uintptr_t entity = *(uintptr_t*)(entityList + 0x10);

    // 读取实体的血量
    int health = GetPlayerHealth(entity);

    printf("玩家血量: %d\n", health);
}
```
