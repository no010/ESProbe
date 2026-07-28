---
name: esprobe-dap-firmware
description: ESProbe wireless CMSIS-DAP debug probe firmware for ESP32-C3, based on ESP-IDF 5.5.x. Use when working with the fw/ directory, building/flashing firmware, modifying DAP/SWD/JTAG behavior, adapting GPIO pin mappings, updating WiFi/USBIP stack, porting to new ESP-IDF versions, or debugging SPI acceleration and USBIP protocol issues.
---

# ESProbe DAP Firmware

Wireless CMSIS-DAP debug probe firmware. Exposes a USBIP-over-TCP device on the network so hosts can use standard debug tools (OpenOCD, Keil, etc.) without a physical USB connection.

## Project Layout

```
fw/
├── CMakeLists.txt              # Project root, IDF_TARGET=esp32c3
├── sdkconfig.defaults          # Default Kconfig for ESP32-C3
├── main/                       # Main application component
│   ├── main.c                  # app_main: WiFi, mDNS, DAP, TCP server, web server tasks
│   ├── wifi_handle.c           # STA mode, AP fallback, static IP, event handlers
│   ├── wifi_config_store.c     # NVS persistence for WiFi credentials
│   ├── web_server.c            # HTTP config portal + status + OTA + web terminal (port 80)
│   ├── access_control.c        # Optional PIN gate for debug ports (NVS + IP sessions)
│   ├── tcp_server.c            # BSD socket listener on port 3240
│   ├── usbip_server.c          # USBIP stage1/2 protocol handler
│   ├── DAP_handle.c            # DAP command ringbuffer + worker thread
│   ├── uart_bridge.c           # Optional UART<->TCP bridge
│   ├── led_status.c            # Status LED patterns
│   ├── button_handle.c         # Boot button handling
│   ├── timer.c                 # DAP timestamp via esp_timer (1 MHz, TIMESTAMP_CLOCK)
│   ├── dap_configuration.h     # WinUSB, packet size, reset options
│   └── wifi_configuration.h    # SSID list, static IP, feature toggles
├── components/
│   ├── DAP/                    # CMSIS-DAP core + SPI GPIO switch
│   │   ├── config/DAP_config.h # Pin mapping, clock, capabilities
│   │   ├── include/gpio_op.h   # SoC-specific fast GPIO macros
│   │   ├── include/gpio_common.h # SoC header includes
│   │   ├── source/spi_switch.c # SPI init/deinit for SWD acceleration
│   │   └── source/...          # DAP.c, SW_DP.c, JTAG_DP.c, etc.
│   ├── USBIP/                  # USB descriptor + MSOS20 descriptors
│   └── elaphureLink/           # elaphureLink proxy protocol (handshake + DAP framing over TCP 3240)
└── managed_components/
    └── espressif__mdns/        # Component manager dependency
```

## Build & Flash

Prerequisites (Windows):
- ESP-IDF v5.5.x at `C:\esp\v5.5.4\esp-idf` (currently v5.5.4)
- Launch IDF PowerShell: `& . 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'`
- Target: ESP32-C3-WROOM-02-N4

```powershell
cd fw
idf.py build
idf.py -p COMxx flash monitor
```

Project-owned verification (build + binary check, auto-loads IDF env):

```powershell
pwsh -File fw/scripts/verify_build.ps1
```

Run it after any firmware change; exit code 0 confirms the build produced `fw/build/esprobe_dap.bin`.

Partition offsets: bootloader 0x0, partition table 0x8000, app 0x10000.

## Key Configuration

### Hardware Pin Mapping (ESP32-C3)

Edit `fw/components/DAP/config/DAP_config.h` for pin changes:

| Signal     | GPIO | Note                       |
|------------|------|----------------------------|
| SWCLK      | 6    | SPI CLK (IOMUX direct)     |
| SWDIO      | 7    | SPI MOSI (SIO mode)        |
| UART1 TX   | 4    | TCP bridge TX              |
| UART1 RX   | 5    | TCP bridge RX              |
| nRESET     | 2    | Open-drain, pull-up        |
| nTRST/TDI  | 10   | JTAG disabled, pin reused  |
| LED        | 0    | WiFi status                |

CPU_CLOCK is 160 MHz. Drive capability set to 5mA for SWCLK/SWDIO.

### Feature Toggles

In `fw/main/dap_configuration.h` and `fw/main/wifi_configuration.h`:

| Macro              | Default | Purpose                          |
|--------------------|---------|----------------------------------|
| `USE_WINUSB`       | 1       | WinUSB bulk mode (512B packets)  |
| `USE_SPI_SIO`      | 1       | MOSI only, no MISO loopback      |
| `USE_UART_BRIDGE`  | 1       | UART1 TCP bridge on port 1234    |
| `USE_STATIC_IP`    | 注释掉（DHCP） | 取消注释后启用 192.168.137.123 |
| `USE_MDNS`         | 1       | dap.local                        |
| `USE_OTA`          | 0       | 宏未启用；web_server.c 仍提供 /ota 端点（见 Known Issues） |
| `USE_KCP`          | 0       | Experimental, not supported      |
| `DAP_JTAG`         | 0       | JTAG disabled, SWD only          |

### SDKConfig Critical Values

`fw/sdkconfig.defaults` must maintain:
- `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=32` (must be >= RX_BA_WIN/2)
- `CONFIG_ESP_WIFI_RX_BA_WIN=32`
- `CONFIG_ESP_WIFI_TX_BA_WIN=32`
- `CONFIG_UART_ISR_IN_IRAM=y`

## Common Modifications

### Changing WiFi Credentials
Edit `wifi_list[]` in `fw/main/wifi_configuration.h`. The firmware auto-rotates through entries on disconnect.

### Changing Static IP
Default is DHCP (`USE_STATIC_IP` is commented out in `wifi_configuration.h`). Uncomment it to use the static address baked into `wifi_handle.c` (lines with `ESP_IP4TOADDR`).

### Adding a New ESP Target (e.g., ESP32-C6)
1. Add `CONFIG_IDF_TARGET_ESP32C6` blocks in:
   - `DAP_config.h` (pins, CPU_CLOCK, PORT_JTAG_SETUP, PORT_OFF, nRESET_OUT)
   - `gpio_op.h` (GPIO_FUNCTION_SET, GPIO_SET_DIRECTION_NORMAL_OUT, level macros)
   - `gpio_common.h` (SoC headers)
   - `spi_switch.c` (DAP_SPI_Init, DAP_SPI_Deinit, DAP_SPI_Acquire/Release)
   - `wifi_handle.c` (PIN_LED_WIFI_STATUS)
   - `uart_bridge.c` (UART port/pins)
2. Use `gpio_ll_*` HAL for RISC-V targets (ESP32-C3/C6/H2 style).
3. Verify SPI register struct names in the new SoC TRM; Espressif changes field names between chips.

### Disabling SPI Acceleration (fallback to bit-bang)
In `dap_configuration.h`, there is no single toggle. SPI mode is selected at runtime by `DAP_SPI_Init()` / `DAP_SPI_Deinit()` in `spi_switch.c`. To force GPIO mode, stub out `DAP_SPI_Init()` to only configure GPIO outputs, and ensure `DAP_SPI_Deinit()` is always called before SWD transfers.

### Porting to Newer ESP-IDF
See [references/idf-migration.md](references/idf-migration.md) for the API migration checklist.

## Architecture Notes

### Tasking Model
- `tcp_server_task` (priority 14, core 0): Accepts USBIP connections, parses stage1 headers, then calls `usbip_worker()` which blocks in `usbip_urb_process()`.
- `DAP_Thread` (priority 10, core 0): Processes DAP commands from a ringbuffer. Notified by `xTaskNotifyGive()` from TCP path.
- `uart_bridge_task` (priority 2): Optional UART<->TCP bridge.

### USBIP Protocol Flow
1. Host sends `OP_REQ_DEVLIST` or `OP_REQ_IMPORT` (stage1).
2. Server replies with device descriptor, then enters URB loop (stage2).
3. URBs on EP1 OUT contain DAP command data → pushed to `dap_dataIN_handle` ringbuffer.
4. URBs on EP1 IN request DAP response → `fast_reply()` pulls from `dap_dataOUT_handle`.
5. EP0 handles USB control requests (WinUSB descriptor, MSOS20, etc.).

### SPI vs GPIO Mode for SWD
- `PORT_SWD_SETUP()` calls `DAP_SPI_Deinit()` to ensure pins are in GPIO mode for initial pin state reads.
- When `DAP_SWJ_Clock` sets a high frequency, `DAP_SPI_Init()` switches pins to SPI function for hardware-accelerated transfer.
- `DAP_SPI_Acquire()` / `DAP_SPI_Release()` manage SPI clock pin muxing during transactions.

## Known Issues & Constraints

- **SPI acceleration unverified on ESP32-C3**: `spi_switch.c` compiles but needs logic-analyzer/scope validation against a real target. Fallback GPIO mode works.
- **Timer implemented on C3**: `get_timer_count()` returns `esp_timer_get_time()` microseconds (`TIMESTAMP_CLOCK 1MHz`); wraps every ~71 min, still stubbed to 0 only on targets without esp_timer wiring.
- **JTAG disabled**: `DAP_JTAG 0` in `DAP_config.h`. Only SWD is supported.
- **elaphureLink proxy present**: `components/elaphureLink` implements the elaphureLink handshake/DAP framing and is wired into `tcp_server.c` (mDNS advertises `usbip+elaphurelink`). `DAP_vendor.c` still stubs `ID_DAP_Vendor8`; WebSocket and KCP remain absent.
- **Web OTA functional**: `web_server.c` serves a config portal with `POST /ota`; `sdkconfig.defaults` sets `CONFIG_PARTITION_TABLE_TWO_OTA=y` (otadata + factory + ota_0 + ota_1). If OTA reports "No OTA partition" at runtime, the local `sdkconfig` was generated before this default existed — delete `fw/sdkconfig` and rebuild to regenerate it from defaults.
- **Optional PIN gate**: when a PIN is set via the web portal (`POST /pin`), TCP 3240/1234 only accept clients unlocked through `POST /unlock` (per-IP, until reboot). No PIN = fully open (backward compatible). Plain-HTTP PIN, LAN deterrent only.
- **Web serial terminal**: `GET /terminal` + WebSocket `/ws/uart` (needs `CONFIG_HTTPD_WS_SUPPORT=y`). Single WS client; while connected the TCP 1234 bridge pauses UART RX forwarding (shared-claim in `uart_bridge.c`).
- **CSR GPIO unavailable**: ESP32-C3 CSR instructions (`RV_SET_CSR`) removed; toolchain incompatibility in IDF 5.5.x. Use `gpio_ll_*` instead.
- **Static IP hardcoded**: `wifi_handle.c` embeds IP directly; `wifi_configuration.h` macros not fully used there.

## References

- **Hardware pinout & SoC differences**: [references/hardware-pinout.md](references/hardware-pinout.md)
- **ESP-IDF migration guide (4.x → 5.5.x)**: [references/idf-migration.md](references/idf-migration.md)
- **USBIP protocol & WinUSB descriptors**: [references/usbip-protocol.md](references/usbip-protocol.md)
- **CMSIS-DAP core & SPI acceleration**: [references/dap-core.md](references/dap-core.md)
