# 闪光弹致盲状态指示器功能文档

## 📋 功能概述

闪光弹致盲状态指示器是一个战术辅助系统，实时检测敌人是否被闪光弹致盲，通过在方框顶部显示**椭圆形"眼睛"指示器**，帮助玩家识别最佳击杀时机。

## 🎯 核心功能

### 1. 实时状态检测
- 读取敌人的闪光弹持续时间（`m_flFlashDuration`）
- 读取闪光弹透明度强度（`m_flFlashMaxAlpha`）
- **智能阈值检测**：只有闪光强度 ≥ 50% 才认为被闪（避免误报）

### 2. 视觉指示系统

#### 椭圆形"眼睛"指示器
在敌人方框顶部显示一个填充的椭圆形，类似眼睛的形状：

| 状态 | 颜色 | 含义 |
|------|------|------|
| **正常** | 🔴 红色 | 敌人视野正常 |
| **致盲** | 🟡 黄色 | 敌人被闪光弹致盲 - **绝佳击杀机会！** |

**特点：**
- 简洁直观，不遮挡视野
- 颜色对比强烈，易于识别
- 位置固定在方框上方，便于快速扫视

### 3. 可配置选项

| 选项 | 描述 | 默认值 |
|------|------|--------|
| **Flashbang Eye Indicator** | 启用/禁用椭圆形眼睛指示器 | ✅ 开启 |
| **Normal Eye Color** | 正常状态的眼睛颜色 | 🔴 红色 (255,0,0) |
| **Flashed Eye Color** | 致盲状态的眼睛颜色 | 🟡 黄色 (255,255,0) |

## 🔧 技术实现

### 内存偏移量

```cpp
// 在 C_CSPlayerPawnBase 中
m_flFlashDuration = 0x1610  // float32 - 闪光持续时间
m_flFlashMaxAlpha = 0x160C  // float32 - 闪光最大透明度
m_flFlashBangTime = 0x15FC  // float32 - 闪光时间戳
```

### 读取逻辑（带透明度阈值）

```cpp
// 读取闪光弹持续时间和透明度
float flashDuration = memory::Read<float>(
    entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration
);
float flashMaxAlpha = memory::Read<float>(
    entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashMaxAlpha
);

// 只有闪光强度 >= 50% 才认为被闪（避免边缘闪光误报）
const float FLASH_ALPHA_THRESHOLD = 0.5f;
bool isFlashed = (flashDuration > 0.0f) && (flashMaxAlpha >= FLASH_ALPHA_THRESHOLD);
```

### 渲染逻辑（椭圆形眼睛指示器）

```cpp
if (menu::espFlashIndicator) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // 计算椭圆位置（方框上方）
    float ellipseCenterX = x + w / 2;
    float ellipseCenterY = y - 12;  // 方框上方12像素
    float ellipseRadiusX = w * 0.25f;  // 宽度为方框的25%
    float ellipseRadiusY = 6.0f;       // 固定高度

    // 根据状态选择颜色
    ImU32 eyeColor;
    if (enemy.isFlashed) {
        // 黄色 - 被闪状态
        eyeColor = IM_COL32(
            menu::espFlashColor[0] * 255,
            menu::espFlashColor[1] * 255,
            menu::espFlashColor[2] * 255,
            menu::espFlashColor[3] * 255
        );
    } else {
        // 红色 - 正常状态
        eyeColor = IM_COL32(
            menu::espFlashNormalColor[0] * 255,
            menu::espFlashNormalColor[1] * 255,
            menu::espFlashNormalColor[2] * 255,
            menu::espFlashNormalColor[3] * 255
        );
    }

    // 绘制填充椭圆（使用ImGui的AddEllipseFilled）
    drawList->AddEllipseFilled(
        ImVec2(ellipseCenterX, ellipseCenterY),  // 中心点
        ImVec2(ellipseRadiusX, ellipseRadiusY),  // 半径 (x, y)
        eyeColor,                                 // 颜色
        0.0f,                                     // 旋转角度
        32                                        // 段数（32段足够平滑）
    );
}
```

## 🎮 使用方法

### 菜单设置

1. 按 **F4** 打开菜单
2. 在 "ESP Settings" 中找到：
   - ✅ **Flashbang Eye Indicator** - 启用椭圆形眼睛指示器
   - 🎨 **Normal Eye Color** - 自定义正常状态颜色（默认红色）
   - 🎨 **Flashed Eye Color** - 自定义致盲状态颜色（默认黄色）

### 实战应用

#### 场景1：投掷闪光弹后
1. 投掷闪光弹
2. 观察敌人方框上方的**椭圆形眼睛**
3. 🔴 红色 → 🟡 **黄色** = 敌人被闪！
4. **立即冲锋击杀！**

#### 场景2：队友闪光弹
1. 队友投掷闪光弹
2. 多个敌人的眼睛同时变黄
3. 优先击杀被闪的敌人（黄色眼睛）
4. 利用致盲时间差

#### 场景3：判断闪光效果
1. 投掷闪光弹后
2. 如果眼睛**保持红色** - 闪光弹未命中或强度不足
3. 如果眼睛**变黄** - 闪光弹成功（强度 ≥ 50%）
4. 根据眼睛颜色规划进攻策略

## 📊 闪光弹持续时间

CS2中闪光弹的致盲时间取决于多个因素：

| 因素 | 影响 |
|------|------|
| **距离** | 越近致盲时间越长 |
| **视角** | 正面看闪光弹时间最长 |
| **遮挡** | 墙壁/障碍物会减少效果 |

**典型持续时间：**
- 完美闪光：**3-5秒**
- 侧面闪光：**1-3秒**
- 边缘闪光：**0.5-1秒**

## 🎨 视觉效果示例

```
正常状态：
      ●●●●●      (🔴 红色椭圆眼睛)
    ┌─────────┐  (红色方框)
    │         │
    │ AK-47   │
    │         │
    └─────────┘

被闪状态：
      ●●●●●      (🟡 黄色椭圆眼睛 - 非常醒目！)
    ┌─────────┐  (红色方框)
    │         │
    │ AK-47   │
    │         │
    └─────────┘
```

**优势：**
- 椭圆形状类似眼睛，直观表示"视觉状态"
- 颜色变化明显：红→黄，易于快速识别
- 不遮挡方框内容，保持视野清晰
- 位置固定，扫视时容易注意到

## 💡 战术建议

### 进攻时
1. **投闪后立即推进** - 看到白色方框就冲
2. **优先击杀被闪敌人** - 他们无法还击
3. **利用时间差** - 在致盲结束前完成击杀

### 防守时
1. **观察敌人状态** - 如果敌人被队友闪光，配合击杀
2. **避免被闪** - 看到闪光弹投掷物时转身
3. **反闪光弹** - 敌人投闪时，反投闪光弹

### 团队配合
1. **沟通闪光弹** - 告诉队友你要投闪
2. **观察队友闪光效果** - 通过ESP确认是否成功
3. **协同进攻** - 多人同时利用闪光弹优势

## ⚙️ 配置建议

### 推荐设置
```cpp
espFlashIndicator = true;  // 必须开启
espFlashNormalColor = {1.0, 0.0, 0.0, 1.0};  // 红色 - 正常状态
espFlashColor = {1.0, 1.0, 0.0, 1.0};        // 黄色 - 被闪状态
```

### 颜色方案

#### 正常状态眼睛颜色
| 方案 | 颜色 | RGB | 说明 |
|------|------|-----|------|
| **经典红色** | 🔴 | (255,0,0) | 推荐，表示警戒 |
| **深灰色** | ⚫ | (100,100,100) | 低调，不抢眼 |
| **蓝色** | 🔵 | (0,100,255) | 冷色调 |

#### 被闪状态眼睛颜色
| 方案 | 颜色 | RGB | 说明 |
|------|------|-----|------|
| **亮黄色** | 🟡 | (255,255,0) | 推荐，最醒目 |
| **白色** | ⚪ | (255,255,255) | 高对比度 |
| **橙色** | 🟠 | (255,165,0) | 警告色 |
| **青色** | 🔵 | (0,255,255) | 夜间地图 |

## 🐛 常见问题

### Q: 为什么有时候检测不到闪光弹？
A: 可能原因：
- 闪光弹未命中敌人
- 敌人背对闪光弹
- 敌人被墙壁遮挡
- 内存读取延迟

### Q: 文字提示位置重叠怎么办？
A: 可以关闭 "Show 'FLASHED' Text"，仅使用颜色变化。

### Q: 如何调整提示位置？
A: 修改 `esp.cpp` 中的 `y - 50` 参数来调整文字高度。

## 📝 数据结构

### EnemyInfo 结构
```cpp
struct EnemyInfo {
    vec3 position;
    vec3 headPosition;
    int32_t health;
    float distance;
    std::string weaponName;
    float viewYaw;
    float angleToPlayer;
    float flashDuration;  // 新增：闪光持续时间
    bool isFlashed;       // 新增：是否被闪
};
```

