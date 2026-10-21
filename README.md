# Multi-I2CTask-driver

基于 STM32F103 的**软件 I2C 多从机任务调度框架**：TIM2 中断驱动的位级软 I2C 引擎 + 「poll 决定入队、update 推进、runtime 调度」的设备契约，一个主循环驱动任意多个从机。

当前实例：GY521 (MPU6050) 六轴采样（100Hz）+ OLED 显示 + USART1 串口回传（仅 usart 分支）。

## 分支

| 分支 | 内容 |
|---|---|
| `master` | vtable 版：设备接口 `I2CDevice` + flash 共享方法表，多实例能力 |
| `design-static` | static 版：任务状态 file-scope 静态化，最简概念，单实例 |
| `usart` | static 版 + USART1 串口回传 + 8N1 修复（功能最全） |

三个分支的调度语义一致（poll 纯谓词 / update 消费标志 / runtime 入队），差别在接口抽象方式，可作 C 语言多态两种写法的对照。

## 硬件

- **主控**：STM32F103C8T6 最小系统板（Blue Pill 类，64K Flash / 20K RAM）
- **调试器**：ST-Link V2（4 线 SWD）
- **传感器**：GY521 (MPU6050)——SCL/PA6、SDA/PA7（软 I2C）、INT/PA1
- **显示**：4 脚 I2C OLED (SSD1306)——PB6/PB7（硬件 I2C1）
- **串口**：USB-TTL (CH340/CH341)——PA9(TX)/PA10(RX)

## 工具链

本项目使用 SPL（标准外设库）V3.6.x，**不是** HAL/CubeMX。

| 工具 | 用途 |
|---|---|
| arm-none-eabi-gcc（≥ 12，需支持 `-std=gnu11`） | 交叉编译 |
| CMake ≥ 3.20 + Ninja | 构建系统（preset 化） |
| OpenOCD ≥ 0.12 | 烧录 / GDB 服务 |
| Python 3（仅标准库） | 串口解码工具 |

### Linux (Arch)

```sh
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-gdb arm-none-eabi-newlib ninja openocd cmake

# USB 调试器权限：安装 udev 规则（覆盖 ST-Link / CMSIS-DAP）
sudo cp openocd/99-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger

# 串口权限：把用户加进 uucp 组（USB-TTL 设备 /dev/ttyUSB* 属 uucp 组）
sudo usermod -aG uucp $USER   # 重新登录生效
```

### Windows

推荐 WSL2（流程同 Linux）；或 MSYS2：

```sh
pacman -S mingw-w64-ucrt-x86_64-arm-none-eabi-gcc mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-openocd
```

ST-Link 在 Windows 无需 udev 规则，装官方驱动即可。

### macOS

```sh
brew install --cask gcc-arm-embedded cmake ninja openocd
```

## 构建 / 烧录

```sh
cmake --preset debug                # 配置（Debug, -O0 -g3）
cmake --build --preset debug        # 构建 → build/debug/stm32f103.elf/.hex/.bin
cmake --build --preset debug --target flash          # 烧录（OpenOCD + ST-Link）
cmake --build --preset debug --target debug-server   # 起 GDB 服务 :3333
```

## 串口回传（usart 分支）

115200 / 8 数据位 / 无校验 / 1 停止位（8N1）。**注意**：CH340/CH341 类 USB-TTL 的 Linux 驱动对奇偶校验位支持有缺陷，本工程已从 8E1 降级为 8N1，完整性由帧级 SUM 校验保证，不要改回带校验的配置。

```sh
python3 tools/serial_decode.py /dev/ttyUSB0   # 帧解码 + 物理量换算，单行原地刷新
```

帧格式：`| 0xAA | 0x55 | LEN | DATA | SUM |`，DATA 为 `ax=... ay=... gz=...` 文本行。

## 换芯片型号

本工程以 STM32F103C8T6（中容量 MD）为准。换型号需要**同时**改四处：

1. `CMakeLists.txt` 的 `DEVICE` 选项和 `STM32F10X_MD` 编译宏（如高容量改 `STM32F10X_HD`）
2. `ld/` 下换成对应 Flash/RAM 尺寸的链接脚本
3. `Start/` 的启动文件（`startup_stm32f10x_md.s` → hd/cl 版本）
4. 引脚规划（各外设宏集中在 `GY521.h` / `soft_i2c.h` / `usart1.h` 头部）
