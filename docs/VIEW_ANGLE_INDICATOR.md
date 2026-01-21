# 敌人朝向指示器功能文档

## 📋 功能概述

敌人朝向指示器是一个智能视觉提示系统，通过分析敌人的视角方向和玩家位置，实时显示敌人是否正在看向你，帮助玩家判断是否已被发现。

## 🎯 核心功能

### 1. 实时角度计算
- 读取敌人的视角yaw角度（`m_angEyeAngles`）
- 计算从敌人位置到玩家位置的方向向量
- 计算敌人视角与玩家方向的夹角
- 角度范围：0° - 180°

### 2. 智能颜色指示

| 角度范围 | 颜色 | 含义 | 危险等级 |
|---------|------|------|---------|
| 0° - 45° | 🔴 **红色** | 敌人正面向你 | ⚠️⚠️⚠️ 极度危险！ |
| 45° - 90° | 🟠 **橙色** | 敌人部分朝向你 | ⚠️⚠️ 可能被发现 |
| 90° - 135° | 🟡 **黄色** | 敌人侧面对你 | ⚠️ 注意观察 |
| 135° - 180° | 🟢 **绿色** | 敌人背对你 | ✅ 相对安全 |

### 3. 视觉指示器

**箭头方向：**
- **向下箭头** ⬇️：敌人正在看向你（角度 < 90°）
- **向上箭头** ⬆️：敌人背对你（角度 ≥ 90°）

**位置：**
- 显示在敌人方框顶部上方
- 位于武器名称下方

## 🔧 技术实现

### 内存读取

```cpp
// 偏移量
m_angEyeAngles = 0x3DF0  // QAngle类型 (pitch, yaw, roll)

// 读取敌人视角
vec3 eyeAngles = memory::Read<vec3>(entity + m_angEyeAngles);
float enemyYaw = eyeAngles.y;  // Yaw是Y分量
```

### 角度计算公式

```cpp
// 1. 计算从敌人到玩家的yaw角度
float yawToPlayer = atan2(playerY - enemyY, playerX - enemyX) * (180/π)

// 2. 计算角度差
float angleDiff = abs(normalize(enemyYaw - yawToPlayer))

// 3. 角度归一化到 -180° ~ 180°
float normalize(float angle) {
    while (angle > 180) angle -= 360;
    while (angle < -180) angle += 360;
    return angle;
}
```

### 渲染逻辑

```cpp
// 根据角度选择颜色
if (angleToPlayer < 45°)       -> RED (255, 0, 0)
else if (angleToPlayer < 90°)  -> ORANGE (255, 165, 0)
else if (angleToPlayer < 135°) -> YELLOW (255, 255, 0)
else                            -> GREEN (0, 255, 0)

// 绘制三角形箭头
if (angleToPlayer < 90°) {
    // 向下箭头（敌人看向你）
    DrawTriangle(center, top-left, top-right)
} else {
    // 向上箭头（敌人背对你）
    DrawTriangle(top, bottom-left, bottom-right)
}
```

## 🎮 使用方法

### 菜单设置

1. 按 **F4** 打开菜单
2. 在 "ESP Settings" 中找到：
   - ✅ **View Direction** - 启用/禁用朝向指示器
   - ✅ **Show Angle Degrees** - 显示具体角度数值（可选）

### 实战应用

#### 场景1：进攻时
- 🟢 **绿色箭头向上**：敌人背对你，可以偷袭
- 🔴 **红色箭头向下**：敌人正看着你，立即寻找掩护！

#### 场景2：防守时
- 🟡 **黄色箭头**：敌人在侧面，可能即将转向
- 🟠 **橙色箭头**：敌人正在转向你，准备交火

#### 场景3：侦察时
- 观察多个敌人的朝向
- 找到背对你的敌人优先击杀
- 避开正面朝向你的敌人

## 📊 性能优化

- 每帧只计算一次角度
- 使用快速三角函数（atan2）
- 颜色判断使用简单的if-else
- 箭头使用ImGui的高效三角形绘制

## 🔍 调试信息

启用 "Show Angle Degrees" 后，会在箭头下方显示具体角度：
```
  ⬇️
 45°
```

这对于：
- 理解角度计算
- 调试功能
- 精确判断敌人朝向

非常有用。

## ⚙️ 配置选项

### menu.hpp 中的设置

```cpp
inline bool espViewAngle = true;      // 默认开启
inline bool espViewAngleText = false; // 默认不显示角度文字
```

### 自定义修改

如果想调整角度阈值，修改 `esp.cpp` 中的判断条件：

```cpp
// 当前阈值：45°, 90°, 135°
if (enemy.angleToPlayer < 45.0f)       // 红色
else if (enemy.angleToPlayer < 90.0f)  // 橙色
else if (enemy.angleToPlayer < 135.0f) // 黄色
else                                    // 绿色

// 可以调整为更敏感或更宽松的阈值
```

## 🐛 常见问题

### Q: 箭头颜色不准确？
A: 确保游戏中没有使用第三人称视角，该功能基于第一人称视角计算。

### Q: 箭头位置重叠？
A: 如果启用了武器显示，箭头会自动显示在武器名称下方。可以关闭武器显示或调整位置。

### Q: 性能影响？
A: 角度计算非常轻量，对性能影响可忽略不计（< 0.1ms per frame）。

## 🔗 相关文件

- `external-cheat-base/src/features/esp.cpp` - 核心实现
- `external-cheat-base/src/features/esp.hpp` - 数据结构
- `external-cheat-base/src/features/menu.hpp` - 菜单选项
- `external-cheat-base/generated/client_dll.hpp` - 偏移量定义

## 📝 更新日志

### v1.0 (2026-01-21)
- ✅ 实现基础朝向检测
- ✅ 智能颜色指示系统
- ✅ 箭头方向指示
- ✅ 可选角度数值显示
- ✅ 菜单集成

