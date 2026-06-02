<p align="center">
  <img width="80%" src="docs/v0/emo-robot.png" alt="Cyber Robot Emo">
</p>

## Cyber Robot Emo 固件（基于 xiaozhi-esp32 框架）

本仓库是一个面向 **ESP32-S3 双足机器人板（`cyber-robot-emo`）** 的固件工程，用于运行基于 MCP 的语音交互与设备控制能力（屏幕/灯光/舵机/电源等）。

### 上游关系与致谢（开源合规）

- **框架来源**：本项目的工程组织方式、通信与设备端 MCP 框架沿用并二次开发自上游项目 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)。

### 主要功能

- **语音交互**：离线唤醒 + 与服务端协议交互（具体协议/能力以工程配置为准）
- **显示**：LCD/LVGL 表情与状态展示
- **灯光**：WS2812 分区控制与状态灯效
- **运动控制**：4 路舵机动作（预置动作 + 自编程序列）
- **电源/电量**：电池电量计等外设支持（见板级实现）
- **设备端 MCP 工具**：用于在大模型侧调用设备能力

### 支持的硬件（本仓库仅维护这一块）

- **板级**：`cyber-robot-emo`
- **主控**：ESP32-S3
- **显示**：ST7789 SPI LCD（240×240）
- **音频**：ES7210（I2S ADC）+ ES8311（I2S DAC）+ PA（用于语音链路，不包含音乐播放器）
- **摄像头**：DVP 摄像头接口（如启用）
- **灯光**：68×WS2812 RGB LED
- **电源**：CW2015 电量计（I2C）
- **舵机**：4 路（左腿/右腿/左脚/右脚）

### 快速开始

#### 1) 环境准备

- 安装 ESP-IDF（建议 5.4+；本工程已在 5.5.x 版本链路上验证）
- 推荐在 Linux 环境构建

#### 2) 配置板级

通过 `menuconfig` 选择板型（名称以工程 Kconfig 为准）并配置网络/协议等参数：

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

#### 3) 编译

```bash
idf.py build
```

#### 4) 烧录

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### 设备端 MCP 工具（与 `cyber-robot-emo` 板级相关）

动作控制通过 MCP 工具暴露（以板级注册实现为准，建议从 `main/boards/cyber-robot-emo/emo_controller.cc` 查看最新列表），常见工具包括：

- `self.emo.action`：预置动作
- `self.emo.servo_sequences`：自编程舵机序列
- `self.emo.stop`：停止动作并复位
- `self.emo.set_trim` / `self.emo.get_trims`：舵机微调校准
- `self.emo.get_status`：返回 `moving/idle`
- `self.battery.get_level`：电池电量与充电状态
- `self.emo.get_ip`：获取设备 IP

### 板级文件与引脚定义

- **板级目录**：`main/boards/cyber-robot-emo/`
- **引脚/硬件宏定义**：`main/boards/cyber-robot-emo/config.h`
- **板级入口与外设初始化**：`main/boards/cyber-robot-emo/emo_robot.cc`
- **动作与舵机轨迹层**：`main/boards/cyber-robot-emo/emo_movements.cc`
- **灯光控制**：`main/boards/cyber-robot-emo/cyber_led.cc`


### 目录结构（摘要）

- `main/`：应用主逻辑、协议、显示、音频服务、MCP 服务器等
- `main/boards/cyber-robot-emo/`：本仓库唯一维护的板级实现
- `partitions/`：分区表（如需 OTA/资源分区调整可在此维护）
- `components/` / `managed_components/`：依赖组件

### 许可协议

本项目使用 **MIT License**，见 `LICENSE`。

### 第三方声明

本项目沿用并二次开发自上游 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)。仓库中还包含若干第三方组件与其各自的许可文件（例如 `LICENSE.txt`），使用与分发时请一并遵守对应许可条款。
