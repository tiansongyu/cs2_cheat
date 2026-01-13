# CS2 Entity List 逆向工程文档

## 概述

本文档记录了如何逆向工程 CS2 的 Entity List 结构，找到正确的内存偏移和 stride 值。

## 问题背景

从 a]2-dumper 获取的偏移无法正确读取实体数据。主要问题：
- `dwEntityList` 偏移正确，但内部结构解析错误
- 原代码使用 `stride = 0x78`，实际应该是 `0x70`
- 原代码使用 `base offset = 0x10`（部分正确）

## 调试过程

### 第一步：确认已知正确的值

使用 Cheat Engine (CE) 确认可以直接读取的值：

```
dwLocalPlayerPawn -> 直接读取 LocalPlayerPawn 地址
LocalPlayerPawn + 0x34C -> m_iHealth (血量 = 100) ✓
```

**关键**：我们有一个"已知正确"的 LocalPlayerPawn 地址可以用来验证。

### 第二步：获取 Handle 信息

从 LocalPlayerController 读取 m_hPawn handle：

```
LocalController + m_hPawn = 0x1d3041c (handle)
```

Handle 解析：
- `index = handle & 0x7FFF = 1052`
- `chunkIndex = index >> 9 = 2` (除以 512)
- `indexInChunk = index & 0x1FF = 28` (对 512 取余)

### 第三步：自动搜索 LocalPlayerPawn 指针

在 entity_list 区域内搜索 LocalPlayerPawn 的指针值：

```cpp
// 搜索代码逻辑
for (int offset = 0; offset < 0x100; offset += 8) {
    uintptr_t value = mem.read(entity_list + offset);
    if (value == localPlayerPawn) {
        printf("FOUND at entity_list + 0x%X\n", offset);
    }
}
```

**结果**：
```
FOUND at [entity_list+0x20] + 0xC40
-> chunk addr: 0x36c1e8f0008
```

### 第四步：计算正确的 Stride

已知：
- `indexInChunk = 28`
- `offset = 0xC40`

计算 stride：
```
stride = offset / indexInChunk
stride = 0xC40 / 28 = 0x70
```

**结论**：正确的 stride 是 `0x70`，不是 `0x78`！

### 第五步：确定正确的 Base Offset

测试不同的 base offset：

```cpp
for (int baseOffset = 0x08; baseOffset <= 0x20; baseOffset += 0x08) {
    uintptr_t chunkPtr = mem.read(entity_list + baseOffset + 0x8 * chunkIndex);
    uintptr_t resolved = mem.read(chunkPtr + 0x70 * indexInChunk);
    if (resolved == localPlayerPawn) {
        printf("MATCH with base=0x%X\n", baseOffset);
    }
}
```

**结果**：
```
base=0x08: resolved=0x36c1faef000      (错误)
base=0x10: resolved=0x36bc8048000 MATCH! (正确!)
```

## 最终正确公式

```cpp
// 从 entity index 获取实体指针
uintptr_t GetEntityByIndex(uintptr_t entity_list, uint32_t index) {
    uint32_t chunkIndex = index >> 9;      // 除以 512
    uint32_t indexInChunk = index & 0x1FF; // 对 512 取余
    
    // 关键公式
    uintptr_t listEntry = mem.read(entity_list + 0x10 + 0x8 * chunkIndex);
    uintptr_t entity = mem.read(listEntry + 0x70 * indexInChunk);
    
    return entity;
}

// 从 handle 获取实体
uintptr_t GetEntityFromHandle(uintptr_t entity_list, uint32_t handle) {
    uint32_t index = handle & 0x7FFF;
    return GetEntityByIndex(entity_list, index);
}
```

## Entity List 内存结构

```
entity_list (dwEntityList)
├── +0x00: [未知]
├── +0x08: [未知]  
├── +0x10: chunk[0] 指针 -> 包含 entity 0-511
├── +0x18: chunk[1] 指针 -> 包含 entity 512-1023
├── +0x20: chunk[2] 指针 -> 包含 entity 1024-1535
└── ...

chunk[n] 结构:
├── +0x00: entity[0] 指针
├── +0x70: entity[1] 指针
├── +0xE0: entity[2] 指针
└── ... (stride = 0x70)
```

## 使用 Cheat Engine 查找偏移的方法

### 方法 1：搜索已知指针值

1. 用 `dwLocalPlayerPawn` 直接获取 LocalPlayerPawn 地址
2. 在 Memory View 中，搜索这个地址值（小端序字节）
3. 搜索范围设为 entity_list 附近
4. 找到后，计算相对于 entity_list 的偏移

### 方法 2：通过血量找多个 Pawn

1. 添加多个 bot，给每个造成不同伤害（如 87, 73, 65 血）
2. 在 CE 中搜索这些血量值（4字节整数）
3. 找到的地址 - 0x34C = Pawn 地址
4. 在 entity_list 中搜索这些 Pawn 地址
5. 分析多个地址之间的间距，得出 stride

### 方法 3：使用程序自动搜索

编写代码在 entity_list 区域暴力搜索已知的 LocalPlayerPawn 指针：

```cpp
uintptr_t target = localPlayerPawn;
for (int offset = 0; offset < 0x10000; offset += 8) {
    uintptr_t value = mem.read(entity_list + offset);
    if (value == target) {
        printf("Found at offset 0x%X\n", offset);
    }
}
```

## Cheat Engine 详细使用步骤

### 步骤 1：附加进程
1. 打开 Cheat Engine
2. 点击左上角的电脑图标（Select Process）
3. 找到 `cs2.exe` 并选择

### 步骤 2：找到 client.dll 基址
1. View -> Memory Regions
2. 找到 `client.dll` 的基址（通常是 0x7FFxxxxxxx）
3. 记下这个基址

### 步骤 3：定位 Entity List
1. 用 a2x-dumper 获取 `dwEntityList` 偏移
2. 在 CE 中按 Ctrl+G（Go to Address）
3. 输入 `client.dll + dwEntityList`

### 步骤 4：搜索已知指针
1. 先用其他方式（如 `dwLocalPlayerPawn`）获取一个已知的 Pawn 地址
2. 用 "Array of Bytes" 搜索这个地址（小端序）
3. 例如地址 0x36bc8048000 -> 搜索 `00 80 04 8C FB 36 00 00`

### 步骤 5：计算偏移
1. 找到匹配的地址
2. 计算：(找到的地址 - entity_list 基址) = 偏移
3. 通过多个实体计算 stride

## 原错误分析

| 参数 | 错误值 | 正确值 | 影响 |
|------|--------|--------|------|
| Base Offset | 0x08 | 0x10 | 指向错误的 chunk 数组 |
| Stride | 0x78 | 0x70 | 计算 indexInChunk 偏移错误 |

错误公式：`listEntry + 0x78 * indexInChunk` → 偏移越来越大，读到垃圾数据
正确公式：`listEntry + 0x70 * indexInChunk` → 精确读取每个实体

## 总结

关键发现：
1. **Entity List 的 chunk 数组从 +0x10 开始**，不是 +0x08
2. **每个实体条目间距是 0x70 字节**，不是 0x78
3. **Handle 的低 15 位是实体索引**
4. **每个 chunk 包含 512 个实体**

调试方法论：
1. 先确认一个"已知正确"的值（如 LocalPlayerPawn）
2. 用暴力搜索找到该值在目标结构中的位置
3. 通过计算反推出 stride 和 base offset
4. 用多个已知值验证公式的正确性

