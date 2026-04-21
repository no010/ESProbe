# ESProbe 固件

基于 ESP-IDF 5.5.x 的无线 CMSIS-DAP 固件，目标芯片为 ESP32-C3。

## 环境要求

| 项目 | 版本 / 路径 |
|------|-------------|
| ESP-IDF | v5.5.3 |
| Python | 3.13（由 ESP-IDF 安装程序提供 venv） |
| 芯片目标 | ESP32-C3 |
| 模组 | ESP32-C3-WROOM-02-N4 |

Windows 推荐使用立创/乐鑫提供的 PowerShell 环境快捷方式启动：

```powershell
"C:\Program Files\PowerShell\7\pwsh.exe" -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'}"
```

## 项目结构

```
fw/
├── CMakeLists.txt              # 项目根 CMake，IDF_TARGET=esp32c3
├── sdkconfig.defaults          # 默认 Kconfig（WiFi 缓冲区、TCP 参数等）
├── main/                       # 主应用组件
│   ├── main.c                  # app_main：初始化 WiFi、mDNS、DAP、TCP Server
│   ├── wifi_handle.c           # WiFi STA 模式、静态 IP、事件处理
│   ├── tcp_server.c            # BSD socket TCP 服务器（端口 3240）
│   ├── usbip_server.c          # USBIP Stage1/Stage2 协议处理
│   ├── DAP_handle.c            # DAP 命令环形缓冲 + 工作线程
│   ├── uart_bridge.c           # UART1 <-> TCP 桥接（端口 1234）
│   ├── timer.c                 # DAP 时间戳计数器（ESP32-C3 上 stubbed）
│   ├── dap_configuration.h     # WinUSB、包大小、强制复位等开关
│   └── wifi_configuration.h    # WiFi SSID、静态 IP、功能开关
├── components/
│   ├── DAP/                    # CMSIS-DAP v2.1.0 核心
│   │   ├── config/DAP_config.h # 引脚映射、时钟、能力位
│   │   ├── source/DAP.c        # DAP 命令解析器
│   │   ├── source/SW_DP.c      # SWD 传输（GPIO bit-bang + SPI 高速）
│   │   ├── source/spi_switch.c # SPI2 初始化/去初始化，IOMUX 切换
│   │   ├── source/spi_op.c     # SPI 加速的 SWD 时序操作
│   │   └── source/...          # JTAG_DP.c, SWO.c, swd_host.c 等
│   └── USBIP/                  # USBIP 协议栈 + 描述符
│       ├── usb_descriptor.c    # USB 设备/配置/接口描述符
│       ├── MSOS20_descriptor.c # Microsoft OS 2.0 描述符（WinUSB 免驱）
│       └── usb_handle.c        # USB 控制请求处理
└── managed_components/
    └── espressif__mdns/        # idf.py add-dependency 引入
```

## 构建步骤

```bash
cd fw

# 完整重新构建（推荐首次或修改 sdkconfig 后）
idf.py fullclean
idf.py build

# 烧录并监控串口
idf.py -p COMxx flash monitor
```

烧录偏移：
- Bootloader: `0x0`
- Partition Table: `0x8000`
- App: `0x10000`

## 关键配置

### WiFi 凭证

编辑 `main/wifi_configuration.h`：

```c
static struct {
    const char *ssid;
    const char *password;
} wifi_list[] = {
    {.ssid = "YOUR_SSID", .password = "YOUR_PASSWORD"},
};
```

支持多 AP 轮询，断连后自动切换下一个。

### 静态 IP

同样在 `wifi_configuration.h` 中：

```c
#define USE_STATIC_IP 1
#define DAP_IP_ADDRESS 192, 168, 137, 123
#define DAP_IP_GATEWAY 192, 168, 137, 1
#define DAP_IP_NETMASK 255, 255, 255, 0
```

### 功能开关

| 宏 | 位置 | 默认 | 说明 |
|----|------|------|------|
| `USE_WINUSB` | `dap_configuration.h` | 1 | WinUSB bulk 模式（512B），关闭则回退 HID |
| `USE_SPI_SIO` | `dap_configuration.h` | 1 | MOSI 单线回环，无需物理短接 MISO |
| `USE_UART_BRIDGE` | `wifi_configuration.h` | 1 | UART1 TCP 桥接 |
| `UART_BRIDGE_PORT` | `wifi_configuration.h` | 1234 | 串口桥接 TCP 端口 |
| `USE_MDNS` | `wifi_configuration.h` | 1 | mDNS 服务 `dap.local` |
| `DAP_JTAG` | `DAP_config.h` | 0 | JTAG 已禁用，仅保留 SWD |

## 引脚映射（ESP32-C3）

| 功能 | GPIO | 备注 |
|------|------|------|
| SWCLK | IO6 | FSPICLK，IOMUX 直连，SPI 加速 |
| SWDIO | IO7 | FSPID，IOMUX 直连，SPI 加速 |
| UART1 TX | IO4 | TCP 桥接发送 |
| UART1 RX | IO5 | TCP 桥接接收 |
| nRESET | IO2 | 目标复位，开漏输出 |
| LED | IO0 | WiFi 状态指示灯 |

## 架构说明

### 任务模型

| 任务 | 优先级 | 核心 | 职责 |
|------|--------|------|------|
| `tcp_server` | 14 | 0 | 监听 3240 端口，USBIP Stage1/Stage2 协议 |
| `DAP_Task` | 10 | 0 | 从 ringbuffer 取出 DAP 命令并执行 |
| `uart_server` | 2 | — | UART1 <-> TCP 事件处理 |

### USBIP 数据流

1. 主机发送 `OP_REQ_DEVLIST` / `OP_REQ_IMPORT` → 固件回复设备描述符
2. 进入 URB 循环：
   - **EP1 OUT** → DAP 命令 → `dap_dataIN_handle` → 通知 `DAP_Task`
   - **EP1 IN** ← DAP 响应 ← `dap_dataOUT_handle` ← `fast_reply()`
   - **EP0** → USB 控制请求（WinUSB 描述符等）

### SPI 加速

当调试器请求时钟 ≥ 10MHz 时，`DAP_SWJ_Clock()` 调用 `DAP_SPI_Init()`，将 IO6/IO7 切换到 SPI2 IOMUX 功能，以 40MHz APB 时钟硬件驱动 SWD 时序。低速或协议切换时自动回退到 GPIO bit-bang。

## 已知限制

- **SPI 加速未经验证**：`spi_op.c` 在 ESP32-C3 上的时序尚未用逻辑分析仪验证，如调试不稳定可强制使用 GPIO 模式（时钟 < 10MHz）
- **Timer stubbed**：`get_timer_count()` 在 ESP32-C3 上返回 0，DAP 时间戳功能不可用
- **OTA / WebSocket / KCP 未移植**：MVP 版本仅保留无线 DAP + UART 桥接

## 调试串口输出

ESP32-C3 的 UART0（USB-Serial-JTAG）用于固件日志输出，默认波特率 115200。在 `idf.py monitor` 中可直接查看。
