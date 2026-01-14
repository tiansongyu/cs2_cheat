# CS2 Entity List 逆向工程文档

## 概述

本文档记录了如何逆向工程 CS2 的 Entity List 结构，找到正确的内存偏移和 stride 值。

## 核心概念

### 什么是 Entity List？

CS2 中所有游戏实体（玩家、武器、道具等）都存储在一个叫做 Entity List 的数据结构中。要实现 ESP（透视）功能，必须正确遍历这个列表。

### 两种实体类型

```
Controller (控制器)          Pawn (实体)
├── 玩家控制逻辑              ├── 3D 位置
├── 队伍信息                  ├── 血量
├── m_hPawn (Handle) ───────> ├── 模型
└── 玩家设置                  └── 碰撞盒
```

**重要**：Controller 通过 `m_hPawn` (Handle) 指向对应的 Pawn！

## 问题背景

从 a2x-dumper 获取的偏移无法正确读取实体数据。主要问题：
- `dwEntityList` 偏移正确，但内部结构解析错误
- 原代码使用 `stride = 0x78`，实际应该是 `0x70`
- 原代码使用 `base offset = 0x08`，实际应该是 `0x10`

## 完整调试过程

### 第一步：确认已知正确的值

使用 Cheat Engine (CE) 确认可以直接读取的值：

```
client.dll + dwLocalPlayerPawn -> 直接读取 LocalPlayerPawn 地址
例如: LocalPlayerPawn = 0x36bc8048000

LocalPlayerPawn + m_iHealth (0x34C) -> 血量 = 100 ✓
```

**关键**：我们有一个"已知正确"的 LocalPlayerPawn 地址可以用来验证！

### 第二步：理解 Handle 机制

从 LocalPlayerController 读取 m_hPawn handle：

```
LocalController + m_hPawn = 0x1d3041c (这是一个 Handle，不是地址！)
```

**Handle 解析公式**：
```cpp
uint32_t handle = 0x1d3041c;
uint32_t index = handle & 0x7FFF;      // 取低 15 位 = 1052
uint32_t chunkIndex = index >> 9;       // 除以 512 = 2
uint32_t indexInChunk = index & 0x1FF;  // 对 512 取余 = 28
```

**图解**：
```
Handle: 0x1d3041c
        └─────┬─────┘
              ↓
    ┌─────────────────┐
    │ index = 1052    │
    └────────┬────────┘
             ↓
    ┌────────┴────────┐
    │                 │
chunkIndex = 2    indexInChunk = 28
(第3个chunk)      (chunk内第29个)
```

### 第三步：暴力搜索 LocalPlayerPawn 指针

在 entity_list 区域内搜索 LocalPlayerPawn 的指针值：

```cpp
// 搜索代码逻辑
uintptr_t entity_list = Read(client.dll + dwEntityList);
uintptr_t localPlayerPawn = 0x36bc8048000; // 已知正确值

// 遍历 entity_list 的前几个 chunk
for (int chunkIdx = 0; chunkIdx < 4; chunkIdx++) {
    uintptr_t chunkPtr = Read(entity_list + 0x10 + 0x8 * chunkIdx);
    if (!chunkPtr) continue;

    // 在 chunk 内搜索
    for (int offset = 0; offset < 0x10000; offset += 8) {
        uintptr_t value = Read(chunkPtr + offset);
        if (value == localPlayerPawn) {
            printf("FOUND at chunk[%d] + 0x%X\n", chunkIdx, offset);
        }
    }
}
```

**结果**：
```
FOUND at chunk[2] + 0xC40
-> chunk addr: 0x36c1e8f0008
```

### 第四步：计算正确的 Stride

已知：
- `indexInChunk = 28` (从 Handle 计算得出)
- `offset = 0xC40` (搜索找到的偏移)

计算 stride：
```
stride = offset / indexInChunk
stride = 0xC40 / 28 = 0x70 (112 字节)
```

**验证**：`28 * 0x70 = 0xC40` ✓

**结论**：正确的 stride 是 `0x70`，不是 `0x78`！

### 第五步：确定正确的 Base Offset

测试不同的 base offset：

```cpp
// 尝试不同的起始偏移
for (int baseOffset = 0x08; baseOffset <= 0x20; baseOffset += 0x08) {
    uintptr_t chunkPtr = Read(entity_list + baseOffset + 0x8 * chunkIndex);
    uintptr_t resolved = Read(chunkPtr + 0x70 * indexInChunk);

    printf("base=0x%X: resolved=0x%llX %s\n",
           baseOffset, resolved,
           (resolved == localPlayerPawn) ? "MATCH!" : "");
}
```

**结果**：
```
base=0x08: resolved=0x36c1faef000      (错误)
base=0x10: resolved=0x36bc8048000 MATCH! (正确!)
base=0x18: resolved=0x0                (错误)
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

## Entity List 内存结构图解

```
┌─────────────────────────────────────────────────────────────────┐
│                    Entity List (dwEntityList)                    │
├─────────────────────────────────────────────────────────────────┤
│ +0x00: [保留/未知]                                               │
│ +0x08: [保留/未知]                                               │
│ +0x10: chunk[0] 指针 ──────┐                                    │
│ +0x18: chunk[1] 指针 ──────┼──┐                                 │
│ +0x20: chunk[2] 指针 ──────┼──┼──┐                              │
│ +0x28: chunk[3] 指针 ──────┼──┼──┼──┐                           │
│ ...                        │  │  │  │                           │
└────────────────────────────┼──┼──┼──┼───────────────────────────┘
                             │  │  │  │
                             ▼  │  │  │
┌────────────────────────────┐  │  │  │
│      Chunk[0] 数组         │  │  │  │
│  (Entity 0 ~ 511)          │  │  │  │
├────────────────────────────┤  │  │  │
│ +0x000: Entity[0] 指针     │  │  │  │
│ +0x070: Entity[1] 指针     │  │  │  │
│ +0x0E0: Entity[2] 指针     │  │  │  │
│ +0x150: Entity[3] 指针     │  │  │  │
│ ...                        │  │  │  │
│ +stride * 511: Entity[511] │  │  │  │
└────────────────────────────┘  │  │  │
                                ▼  │  │
               ┌────────────────┐  │  │
               │   Chunk[1]     │  │  │
               │ (Entity 512~   │  │  │
               │  1023)         │  │  │
               └────────────────┘  │  │
                                   ▼  │
                  ┌────────────────┐  │
                  │   Chunk[2]     │◄─┘  ← LocalPlayerPawn 在这里！
                  │ (Entity 1024~  │      (index=1052, chunk=2, offset=28)
                  │  1535)         │
                  └────────────────┘
```

### 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| Base Offset | `0x10` | chunk 数组起始位置 |
| Chunk Pointer Size | `0x08` | 每个 chunk 指针占 8 字节 |
| Stride | `0x70` | 每个 entity 条目占 112 字节 |
| Entities per Chunk | `512` | 每个 chunk 包含 512 个实体 |

## ESP 遍历代码详解

```cpp
void esp::updateEntities()
{
    // 1. 获取 Entity List 基址
    uintptr_t entity_list = Read(modBase + dwEntityList);

    // 2. 遍历 Controller (玩家索引 1-63)
    for (uint32_t i = 1; i < 64; i++)
    {
        // 3. 计算 Controller 所在的 chunk 和偏移
        //    对于小索引 (1-63)，chunkIndex 始终为 0
        uintptr_t listEntry = Read(entity_list + 0x10 + 0x8 * (i >> 9));
        if (!listEntry) continue;

        // 4. 读取 Controller 指针
        uintptr_t entityController = Read(listEntry + 0x70 * (i & 0x1FF));
        if (!entityController) continue;

        // 5. 从 Controller 获取 Pawn Handle
        uint32_t pawnHandle = Read<uint32_t>(entityController + m_hPawn);
        if (!pawnHandle) continue;

        // 6. 解析 Handle，定位 Pawn
        uint32_t pawnIndex = pawnHandle & 0x7FFF;
        uint32_t pawnChunk = pawnIndex >> 9;        // 除以 512
        uint32_t pawnInChunk = pawnIndex & 0x1FF;   // 对 512 取余

        // 7. 读取 Pawn 所在 chunk
        uintptr_t pawnListEntry = Read(entity_list + 0x10 + 0x8 * pawnChunk);

        // 8. 读取 Pawn 指针
        uintptr_t entity = Read(pawnListEntry + 0x70 * pawnInChunk);

        // 9. 现在可以读取 Pawn 的属性了
        int32_t health = Read<int32_t>(entity + m_iHealth);
        vec3 position = Read<vec3>(entity + m_vOldOrigin);
        // ...
    }
}
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
3. 找到的地址 - m_iHealth = Pawn 地址
4. 在 entity_list 中搜索这些 Pawn 地址
5. 分析多个地址之间的间距，得出 stride

### 方法 3：使用程序自动搜索

编写代码在 entity_list 区域暴力搜索已知的 LocalPlayerPawn 指针：

```cpp
uintptr_t target = localPlayerPawn;
for (int chunkIdx = 0; chunkIdx < 8; chunkIdx++) {
    uintptr_t chunkPtr = Read(entity_list + 0x10 + 0x8 * chunkIdx);
    if (!chunkPtr) continue;

    for (int offset = 0; offset < 0x10000; offset += 8) {
        uintptr_t value = Read(chunkPtr + offset);
        if (value == target) {
            printf("Found at chunk[%d] + 0x%X\n", chunkIdx, offset);
            printf("Calculated stride: 0x%X\n", offset / (index & 0x1FF));
        }
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

### 关键发现

| 发现 | 说明 |
|------|------|
| Entity List chunk 数组从 `+0x10` 开始 | 不是 `+0x08` |
| 每个实体条目间距是 `0x70` 字节 | 不是 `0x78` |
| Handle 的低 15 位是实体索引 | `index = handle & 0x7FFF` |
| 每个 chunk 包含 512 个实体 | `chunkIndex = index >> 9` |

### 调试方法论

```
┌─────────────────────────────────────────────────────────┐
│  1. 找到一个"已知正确"的值                              │
│     (例如通过 dwLocalPlayerPawn 直接读取)               │
└────────────────────────┬────────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────────┐
│  2. 在目标结构中暴力搜索该值                            │
│     (遍历 entity_list 所有可能位置)                     │
└────────────────────────┬────────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────────┐
│  3. 通过找到的位置反推 stride 和 base offset            │
│     stride = offset / indexInChunk                      │
└────────────────────────┬────────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────────┐
│  4. 用多个已知值验证公式正确性                          │
│     (多个玩家、不同 Handle)                             │
└─────────────────────────────────────────────────────────┘
```

### 快速参考公式

```cpp
// 从 Handle 获取 Pawn
uintptr_t GetPawnFromHandle(uintptr_t entityList, uint32_t handle) {
    uint32_t index = handle & 0x7FFF;
    uint32_t chunk = index >> 9;
    uint32_t offset = index & 0x1FF;

    uintptr_t chunkPtr = Read(entityList + 0x10 + 0x8 * chunk);
    return Read(chunkPtr + 0x70 * offset);
}
```

## 致谢

- 偏移量来源：[a2x-dumper](https://github.com/a2x/cs2-dumper)
