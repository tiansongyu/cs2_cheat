# CS2 ESP Tool (SDL2 Version)

## 功能介绍

ESP 透视功能：
- 绿色方框：远距离敌人 (>30米)
- 黄色方框：中距离敌人 (10-30米)
- 红色方框：近距离敌人 (<10米)
- 血条显示：显示敌人当前血量

## 使用方法

1. 启动 CS2 并进入游戏
2. 运行编译后的可执行文件
3. ESP 会自动覆盖在游戏窗口上
4. 按 **F9** 键退出程序

## 编译和运行

### 1. 安装 SDL2

运行 setup 脚本自动下载 SDL2：
```batch
cd external-cheat-base
setup_sdl2.bat
```

或手动下载：
- 下载地址: https://github.com/libsdl-org/SDL/releases
- 下载 `SDL2-devel-x.x.x-VC.zip`
- 解压到 `external-cheat-base/SDL2` 目录

### 2. 编译项目

使用 Visual Studio 2022 打开 `external-cheat-base.sln` 并编译（x64 Release）

### 3. 运行

确保 `SDL2.dll` 在可执行文件同目录下（编译时会自动复制）

## 偏移更新
dw
CS2 更新后可能需要更新内存偏移，运行：
```batch
update_offset.bat
```

或参考: https://github.com/a2x/cs2-dumper/
