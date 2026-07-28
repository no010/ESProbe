# ESProbe 固件 —— AI 编码助手指南

本文档面向 AI 编码助手。阅读本文档前，请勿假设任何关于本项目的先验知识。

> **与 Skill 的分工（canonical 声明）**：本文档负责编码风格、架构惯例与协作约定；技术事实（IDF 版本、引脚映射、功能开关默认值、验证流程）以 `.agents/skills/esprobe-dap-firmware/SKILL.md` 为准；两者冲突时以工作区实际代码/配置为最终真值。

---

## 项目概述

ESProbe 是一个基于 ESP-IDF 的**无线 CMSIS-DAP 调试探针固件**，目标芯片为 **ESP32-C3**（模组 ESP32-C3-WROOM-02-N4，4 MB Flash）。它将 ESP32-C3 转变为一个通过 WiFi 连接的 ARM SWD 调试适配器，主机端通过网络上的 **USBIP 协议**（TCP 3240 端口）访问该调试器。此外，固件还提供 **UART1 ↔ TCP 桥接**（TCP 1234 端口），用于与目标设备的串口通信。

核心能力：
- 无线 CMSIS-DAP v2.1.0，通过 USBIP over WiFi 工作
- SWD 调试，支持 SPI 硬件加速（时钟 ≥ 10 MHz 时自动切换）
- WinUSB bulk 模式（512 字节包），可编译时回退到 HID 模式
- mDNS 服务发现（`dap.local`）
- 多 AP WiFi 轮询，断连后自动切换
- 可选静态 IP

---

## 技术栈

| 项目 | 版本 / 详情 |
|------|-------------|
| ESP-IDF | v5.5.4 |
| 目标芯片 | ESP32-C3（RISC-V，单核，160 MHz） |
| 模组 | ESP32-C3-WROOM-02-N4 |
| RTOS | FreeRTOS（ESP-IDF 内置） |
| CMSIS-DAP 核心 | v2.1.0（ARM 参考实现，已适配） |
| 网络协议 | USBIP（虚拟 USB 设备 over TCP）、lwIP、mDNS |
| 语言 | C |
| 构建系统 | CMake + Ninja（ESP-IDF `project.cmake`） |
| 托管组件 | `espressif/mdns` v1.11.0 |

---

## 构建与烧录命令

项目使用标准 ESP-IDF 构建流程；另有一个项目自有的构建验证脚本 `scripts/verify_build.ps1`（自动加载 IDF 环境 → `idf.py build` → 校验产物），**每次固件改动后应运行它作为验证门**：

```powershell
pwsh -File fw/scripts/verify_build.ps1   # exit 0 = 验证通过
```

```bash
# 完整重新构建（推荐在首次构建或修改 sdkconfig 后使用）
idf.py fullclean
idf.py build

# 烧录并监控串口输出（将 COMxx 替换为实际串口号）
idf.py -p COMxx flash monitor
```

**构建输出：**
- 固件：`build/esprobe_dap.bin`
- Bootloader：`build/bootloader/bootloader.bin`
- 分区表：`build/partition_table/partition-table.bin`

**烧录偏移：**
- Bootloader：`0x0`
- Partition Table：`0x8000`
- App：`0x10000`

**调试串口：**
ESP32-C3 的 UART0（内置 USB-Serial-JTAG）用于日志输出，波特率 **115200**。

**注意：** 项目中没有单元测试、CI/CD 流水线或 Docker 配置。自动化验证仅限 `scripts/verify_build.ps1` 的编译级冒烟；行为验证依赖实际硬件。

---

## 代码组织与模块划分

```
fw/
├── CMakeLists.txt              # 根 CMake：设置 IDF_TARGET=esp32c3
├── sdkconfig.defaults          # 默认 Kconfig（编译优化、WiFi/LWIP 参数等）
├── main/                       # 主应用组件
│   ├── main.c                  # app_main：初始化 NVS、WiFi、DAP、Timer、mDNS、Web 服务器、创建任务
│   ├── wifi_handle.c/h         # WiFi STA 模式、多 AP 轮询、AP 回退、静态 IP
│   ├── wifi_config_store.c/h   # WiFi 凭证的 NVS 持久化
│   ├── web_server.c/h          # HTTP 配置门户 + 状态 + OTA + Web 串口终端（端口 80）
│   ├── access_control.c/h      # 可选 PIN 门控（NVS 存储 + IP 会话白名单）
│   ├── led_status.c/h          # 状态 LED 模式
│   ├── button_handle.c/h       # Boot 按键处理
│   ├── tcp_server.c/h          # BSD socket TCP 监听器（端口 3240）
│   ├── usbip_server.c/h        # USBIP Stage1/Stage2 协议状态机
│   ├── DAP_handle.c/h          # Ringbuffer 解耦层：USBIP 网络线程与 DAP 执行线程
│   ├── uart_bridge.c/h         # UART1 <-> TCP netconn 桥接（端口 1234）
│   ├── timer.c/h               # DAP 时间戳：esp_timer 微秒计数（TIMESTAMP_CLOCK 1MHz）
│   ├── dap_configuration.h     # WinUSB、包大小、SPI SIO、强制复位开关
│   ├── wifi_configuration.h    # WiFi 凭证、静态 IP、mDNS、UART 桥接开关
│   └── Kconfig.projbuild       # 仅添加 USE_WEBSOCKET_DAP（当前未使用）
├── components/
│   ├── DAP/                    # CMSIS-DAP v2.1.0 核心 + ESP32 GPIO/SPI 适配
│   │   ├── config/DAP_config.h # 引脚映射、能力位、CPU 时钟、内联 GPIO 操作
│   │   ├── include/            # DAP.h、gpio_op.h、spi_op.h、swo.h 等
│   │   └── source/             # DAP.c、SW_DP.c、JTAG_DP.c、SWO.c、spi_op.c、
│   │                             spi_switch.c、swd_host.c、DAP_vendor.c
│   ├── USBIP/                  # USBIP 协议栈 + 虚拟 USB 描述符
│   │   ├── usbip_defs.h        # USBIP 报文结构体定义
│   │   ├── usb_descriptor.c/h  # 设备/配置/接口/端点/HID 描述符
│   │   ├── usb_handle.c/h      # USB 控制请求处理（EP0）
│   │   └── MSOS20_descriptor.c/h # Microsoft OS 2.0 描述符（WinUSB 自动安装）
│   └── elaphureLink/           # elaphureLink 代理协议（握手 + DAP 帧，接入 tcp_server）
├── scripts/
│   ├── verify_build.ps1        # 构建验证门（编译 + 产物校验）
│   └── test_uart_bridge.py     # UART 桥接手动测试（需真机）
└── managed_components/
    └── espressif__mdns/        # 由 idf.py add-dependency 引入的 mDNS 组件
```

---

## 配置系统

本项目采用三层配置方式。修改配置时，请确认改在正确的层级：

### 1. ESP-IDF Kconfig（sdkconfig / sdkconfig.defaults）
由 `idf.py menuconfig` 或 `sdkconfig.defaults` 控制：
- 编译优化：**Performance**（`CONFIG_COMPILER_OPTIMIZATION_PERF=y`）
- 蓝牙：**禁用**（节省内存）
- CPU 频率：160 MHz
- Flash 模式：DIO，80 MHz，4 MB
- WiFi：AMPDU TX/RX 开启，32 BA 窗口，64 动态 TX/RX 缓冲
- LWIP：主机名 `esprobe`，TCP MSS=1440，SND/WND buf=5744
- UART ISR 放在 IRAM 中（低延迟）
- Main task 栈大小：3584 字节
- 分区表：`CONFIG_PARTITION_TABLE_TWO_OTA=y`（otadata + factory + ota_0 + ota_1，支撑 Web OTA）

### 2. 项目 Kconfig（main/Kconfig.projbuild）
仅添加一个布尔选项 `USE_WEBSOCKET_DAP`，当前代码中未引用。

### 3. 头文件编译时配置（开发者直接编辑）

| 头文件 | 关键宏 | 默认值 | 说明 |
|--------|--------|--------|------|
| `main/dap_configuration.h` | `USE_WINUSB` | 1 | 1=WinUSB bulk 模式，0=HID 模式 |
| | `USE_SPI_SIO` | 1 | 1=MOSI 单线回环，无需物理短接 MISO |
| | `USE_USB_3_0` | 0 | 0=USB 2.0（端点大小 512），1=USB 3.0（1024） |
| | `DAP_PACKET_SIZE` | 512 | WinUSB 下 512，HID 下 255 |
| | `USE_FORCE_SYSRESETREQ_AFTER_FLASH` | 0 | 是否在 DAP_ResetTarget 时强制写 SYSRESETREQ |
| `main/wifi_configuration.h` | `wifi_list[]` | — | 多组 WiFi SSID/密码，断连后自动轮询 |
| | `USE_STATIC_IP` | 注释掉 | 是否启用静态 IP |
| | `USE_MDNS` | 1 | 启用 mDNS 服务发现 |
| | `USE_UART_BRIDGE` | 1 | 启用 UART1 TCP 桥接 |
| | `UART_BRIDGE_PORT` | 1234 | 串口桥接端口 |
| `components/DAP/config/DAP_config.h` | `PIN_SWCLK` | IO6 | SWCLK 引脚（FSPICLK，支持 IOMUX） |
| | `PIN_SWDIO` | IO7 | SWDIO 引脚（FSPID） |
| | `PIN_nRESET` | IO2 | 目标复位，开漏输出 |
| | `DAP_JTAG` | 0 | JTAG 已禁用，仅 SWD |
| | `CPU_CLOCK` | 160 MHz | 用于 SWJ 时钟计算 |

**重要：** `wifi_configuration.h` 通常包含用户私有 WiFi 凭证，注意避免将其意外提交到版本控制。

---

## 编码风格指南

### 缩进与括号
- `main/` 和 `components/USBIP/`：**4 空格缩进**，函数定义通常使用 Allman 风格（大括号换行）。
- `components/DAP/`：**2 空格缩进**，K&R 风格大括号。这是因为该目录直接源自 ARM CMSIS-DAP 上游代码，保留了其原有风格。
- 项目中**没有** `.clang-format`、`.editorconfig` 等自动格式化配置文件。

### 命名规范

| 类别 | 规范 | 示例 |
|------|------|------|
| 函数 | `main/` 和 `USBIP/` 中使用 `snake_case`；`DAP/` 中使用 `CamelCase`/`PascalCase`（上游风格） | `tcp_server_task`、`DAP_ProcessCommand` |
| 全局变量 | `snake_case`，关键全局变量常见 **`k` 前缀** | `kSock`、`kDAPTaskHandle` |
| 局部变量 | `snake_case` | `packetSize`、`dap_respond` |
| 结构体/typedef | **`_t` 后缀** | `usbip_stage2_header_t`、`DAP_Data_t` |
| 枚举 | **`_t` 后缀** | `usbip_server_state_t` |
| 宏 / `#define` | **全大写下划线分隔** | `USE_WINUSB`、`DAP_PACKET_SIZE`、`PIN_SWCLK` |
| 头文件包含保护 | 混合使用 `__NAME_H__`、`_NAME_H_` | `__DAP_HANDLE_H__`、`_UART_BRIDGE_H_` |

### 注释风格
- 部分文件使用 Doxygen 风格文件头（`@file`、`@brief`、`@version`、`@date`、`@copyright`）。
- 行内注释混合使用 `//` 和 `/* ... */`。
- 部分 TODO/FIXME 标记使用 `////TODO:` 或 `//FIXME:`。
- 少数注释使用中文。
- `DAP_config.h` 中有较完善的 Doxygen API 文档。

### 日志输出
- 混合使用 `os_printf()`（项目自定义的 `vprintf` 包装，带 `\r\n` 换行）和 ESP-IDF 标准宏 `ESP_LOGI` / `ESP_LOGW` / `ESP_LOGE`。
- 使用 `ESP_LOG*` 时遵循 ESP-IDF 惯例定义 `static const char *TAG = "...";`。

---

## 运行时架构与并发模型

### FreeRTOS 任务

| 任务名 | 优先级 | 亲和核心 | 职责 |
|--------|--------|----------|------|
| `tcp_server` | 14 | 0 | 监听 TCP 3240，运行 USBIP Stage1/Stage2 协议解析 |
| `DAP_Task` | 10 | 0 | 从 Ringbuffer 取出 DAP 命令，执行 `DAP_ProcessCommand()` |
| `uart_server` | 2 | 无指定 | lwIP netconn 事件循环，处理 UART1 TCP 桥接 |

### 核心数据流（USBIP）

1. 主机连接 TCP 3240。
2. **Stage1**：主机发送 `OP_REQ_DEVLIST` / `OP_REQ_IMPORT`；固件回复虚拟 USB 描述符。
3. **Stage2 URB 循环**：
   - **EP1 OUT**（主机 → 设备）：收到 DAP 命令 → `handle_dap_data_request()` → 写入 `dap_dataIN_handle` Ringbuffer → 通知 `DAP_Task`。
   - **DAP_Task**：取出命令，调用 `DAP_ProcessCommand()`，将响应写入 `dap_dataOUT_handle` Ringbuffer，递增 `dap_respond` 计数。
   - **EP1 IN**（设备 → 主机）：`fast_reply()` 从 `dap_dataOUT_handle` 取出响应，以 `RET_SUBMIT` 发回。
   - **EP0**：USB 控制请求（枚举、WinUSB 描述符获取等）由 `handleUSBControlRequest()` 同步处理。

### 同步机制
- **Ring Buffers**（`xRingbufferCreate`）：用于 DAP 命令/响应的生产者-消费者解耦。
- **Mutex**（`xSemaphoreCreateMutex`）：保护共享计数器 `dap_respond`。
- **Task Notifications**（`xTaskNotifyGive` / `ulTaskNotifyTake`）：轻量级信号通知 DAP 任务有新命令。
- **Event Groups**（`xEventGroupCreate`）：WiFi 获取 IP 的事件等待。
- **Queues**（`xQueueCreate`）：UART bridge 的 netconn 事件分发。

### SPI 加速
当调试器请求 SWD 时钟 ≥ 10 MHz 时，`DAP_SWJ_Clock()` 调用 `DAP_SPI_Init()`，将 GPIO6/7 切换到 SPI2（GPSPI2）IOMUX 功能，以 40 MHz APB 时钟硬件驱动 SWD。低速或协议切换时自动回退到 GPIO bit-bang。

---

## 错误处理惯例

- **ESP-IDF API**：普遍使用 `ESP_ERROR_CHECK()` 包装，失败时触发断言并重启。例如：
  ```c
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ```
- **网络代码**：手动检查返回值，`if (ret <= 0)` 配合 `goto cleanup;` 模式。通过 `os_printf()` 输出 `errno` 诊断信息。
- **资源分配失败**：若 Ringbuffer/Mutex 创建失败，DAP 任务会打印错误并调用 `vTaskDelete(NULL)` 自我终止。
- **断言**：少量使用 `assert()`（如 `assert(sta_netif);`）。

---

## 多目标适配说明

代码库保留了多芯片适配能力，通过 `#ifdef CONFIG_IDF_TARGET_*` 条件编译支持 ESP8266、ESP32、ESP32-C3 和 ESP32-S3。但当前项目的根 `CMakeLists.txt` 已固定：

```cmake
set(IDF_TARGET "esp32c3")
```

因此实际编译目标只能是 ESP32-C3。若需移植回其他芯片，需修改根 `CMakeLists.txt` 并重新验证。

### GPIO 访问差异
- ESP8266/ESP32：直接寄存器操作。
- ESP32-C3/S3：使用 `gpio_ll_*` HAL 层。

---

## 安全与部署注意事项

1. **WiFi 凭证硬编码**：`main/wifi_configuration.h` 中的 `wifi_list[]` 包含明文 SSID/密码。请勿将包含真实凭证的文件提交到公共仓库。
2. **无加密传输**：USBIP 和 UART 桥接数据通过明文 TCP 传输，未使用 TLS。仅在受信任的本地网络环境中使用。
3. **访问控制**：默认无鉴权；可在 Web 门户设置 PIN（`POST /pin`），设置后 TCP 3240/1234 仅接受通过 `POST /unlock` 解锁的客户端 IP（重启后失效）。PIN 以明文 HTTP 传输，仅作为受信局域网的防误触措施，非加密安全。
4. **Watchdog**：Task WDT 已启用（9000 ms），但 Interrupt WDT 已禁用。高优先级任务（`tcp_server` prio 14）必须避免长时间阻塞，否则可能触发 Task WDT。
5. **SPI 加速未经验证**：`spi_op.c` 在 ESP32-C3 上的时序尚未用逻辑分析仪验证。如调试不稳定，可在 `dap_configuration.h` 中禁用 `USE_SPI_SIO` 或让调试器请求 < 10 MHz 时钟以强制使用 GPIO 模式。
6. **固件升级**：`web_server.c` 提供 Web 配置门户与 `POST /ota` 端点，`sdkconfig.defaults` 已配置 `CONFIG_PARTITION_TABLE_TWO_OTA=y`（otadata + factory + ota_0 + ota_1），OTA 功能完整可用。若运行时报“No OTA partition”，说明本地 `sdkconfig` 是在该默认值加入前生成的陈旧文件——删掉 `fw/sdkconfig` 重新构建即可。

---

## 已知限制

- `get_timer_count()` 在 ESP32-C3 上已用 `esp_timer_get_time()` 实现（微秒级，约 71 分钟回绕），DAP 时间戳可用。
- SPI 加速时序在 ESP32-C3 上未经验证。
- Web OTA 已可用（two_ota 分区表）；Web 串口终端需 `CONFIG_HTTPD_WS_SUPPORT=y`（已入 defaults）；WebSocket DAP、KCP 仍未移植。
- 自动化验证：`scripts/verify_build.ps1`（编译级）+ `test_host/test_protocol.c`（主机端协议单测）+ GitHub Actions CI；行为验证仍依赖实际硬件。
