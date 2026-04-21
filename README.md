# ESProbe

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

ESProbe 是一款基于 **ESP32-C3** 的无线 CMSIS-DAP 调试探针。它通过 WiFi 将 USBIP 协议传输到 PC，使你可以在没有物理 USB 连接的情况下，使用标准调试工具（OpenOCD、Keil、IAR 等）对目标设备进行 SWD 调试。同时内置 UART1 TCP 桥接，方便无线串口调试。

> 本项目固件基于 [wireless-esp8266-dap](https://github.com/windowsair/wireless-esp8266-dap) 移植，适配 ESP-IDF 5.5.x 和 ESP32-C3 硬件平台。

---

## 特性

- **无线 SWD 调试**：通过 WiFi 使用 USBIP 协议，支持 WinUSB 高速传输（512B 包）
- **SPI 硬件加速**：SWD 时钟高达 40MHz（ESP32-C3 SPI2 IOMUX 直接驱动）
- **UART1 TCP 桥接**：独立的 TCP 端口提供无线串口，支持波特率动态切换
- **mDNS 服务**：设备以 `dap.local` 自动发现
- **静态 IP 支持**：默认 `192.168.137.123`，也可使用 DHCP
- **无需 SWO 引脚支持 RTT**：通过标准 SWD 内存读写即可使用 Segger RTT（配合 OpenOCD）
- **低成本硬件**：基于 ESP32-C3-WROOM-02-N4 模组，单面 PCB，立创 EDA 设计

---

## 仓库结构

```
ESProbe/
├── fw/                    # 主固件（ESP-IDF 5.5.x 项目）
│   ├── main/              # 主应用代码
│   ├── components/DAP/    # CMSIS-DAP 核心组件
│   ├── components/USBIP/  # USBIP 协议栈
│   └── build/             # 构建输出
├── hardware/              # 硬件设计文件
│   ├── 3d/                # 外壳 3D 模型
│   ├── *.json             # 立创 EDA 网表
│   └── *.eprj2            # 立创 EDA 项目文件
├── blink/                 # ESP32-C3 LED 示例（验证板子）
├── hello_world/           # ESP32-C3 Hello World 示例
├── bin/                   # OpenOCD 等预编译工具（Windows）
└── README.md
```

---

## 硬件概述

### 主控

- **ESP32-C3-WROOM-02-N4**（乐鑫）
- 160 MHz RISC-V 单核
- 4MB Flash
- 2.4GHz Wi-Fi 802.11 b/g/n + Bluetooth 5 (LE)

### 调试接口（CN1）

10-pin 卧贴连接器（间距 2mm）：

| Pin | 信号      | GPIO | 说明                  |
|-----|-----------|------|-----------------------|
| 1   | VCC       | —    | 3.3V 输出给目标       |
| 2   | GND       | —    | 地                    |
| 3   | UART1 TX  | IO4  | TCP 桥接发送          |
| 4   | UART1 RX  | IO5  | TCP 桥接接收          |
| 5   | SWCLK     | IO6  | SPI CLK（IOMUX）      |
| 6   | SWDIO     | IO7  | SPI MOSI（IOMUX）     |
| 7   | nRESET    | IO2  | 目标复位（开漏）      |
| 8   | nTRST/TDI | IO10 | JTAG 禁用，引脚复用   |
| 9   | GND       | —    | 地                    |
| 10  | GND       | —    | 地                    |

### 状态指示

- **蓝色 LED**（IO0）：WiFi 连接成功后常亮

更多硬件细节参见 [`hardware/README.md`](hardware/README.md)。

---

## 固件概述

### 构建环境

- **ESP-IDF**: v5.5.3
- **目标芯片**: ESP32-C3
- **操作系统**: Windows（Linux/macOS 亦可，需自行配置工具链）

### 快速构建

在 Windows 下使用 ESP-IDF PowerShell：

```powershell
# 打开立创提供的 PowerShell 快捷方式，或手动加载环境
& 'C:\Program Files\PowerShell\7\pwsh.exe' -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'}"

cd fw
idf.py build
idf.py -p COMxx flash monitor
```

详细固件文档参见 [`fw/README.md`](fw/README.md)。

---

## 使用方式

### 1. 连接 WiFi

上电后，设备自动连接 `wifi_configuration.h` 中预配置的 WiFi（默认尝试 `OTA` / `DAP`）。

### 2. 发现设备

- 如果使用静态 IP，直接访问 `192.168.137.123:3240`
- 如果使用 mDNS，通过 `dap.local:3240` 访问

### 3. 在 Windows 上挂载 USBIP

安装 [usbip-win](https://github.com/cezanne/usbip-win) 后：

```powershell
usbip.exe attach -r 192.168.137.123 -b 1-1
```

系统会识别出一个 CMSIS-DAP v2 (WinUSB) 设备，可直接在 Keil、IAR 或 OpenOCD 中使用。

### 4. UART 桥接

使用任意 TCP 客户端连接 `192.168.137.123:1234`：

```powershell
nc 192.168.137.123 1234
```

首次发送的数字字符串会被识别为波特率（如 `115200`），后续数据即为串口透传。

---

## RTT 支持

ESProbe **不需要 SWO 引脚**即可支持 Segger RTT。RTT 通过标准 SWD 内存读写实现：

1. 在目标固件中集成 Segger RTT 代码
2. PC 端启动 OpenOCD，通过 USBIP 连接 ESProbe
3. 使用 OpenOCD RTT 命令读取目标内存缓冲区

```tcl
# OpenOCD 示例
rtt setup 0x20000000 0x1000 "SEGGER RTT"
rtt start
rtt read 0 1024
```

---

## 相关链接

- [ESP32-C3  datasheet](https://documentation.espressif.com/esp32-c3_datasheet_cn.pdf)
- [ESP32-C3 技术参考手册](https://documentation.espressif.com/esp32-c3_technical_reference_manual_cn.pdf)
- [ESP32-C3-WROOM-02-N4 立创商城](https://item.szlcsc.com/datasheet/ESP32-C3-WROOM-02-N4/3281215.html)
- [wireless-esp8266-dap 原项目](https://github.com/windowsair/wireless-esp8266-dap)
- [usbip-win](https://github.com/cezanne/usbip-win)

---

## License

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。

固件中的 CMSIS-DAP 代码版权归 ARM Limited 所有，采用 Apache-2.0 许可证。
