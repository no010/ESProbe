# Hardware Pinout Reference

## ESProbe ESP32-C3 Target Pin Mapping

All debug signals route to the 10-pin ARM Cortex Debug Connector (CN1) unless noted.

| CN1 Pin | Signal    | GPIO | IOMUX Function      | Direction | Drive | Note                     |
|---------|-----------|------|---------------------|-----------|-------|--------------------------|
| 1       | VCC       | —    | —                   | Power out | —     | 3.3V to target           |
| 2       | SWDIO     | 7    | FSPID (FUNC_MTDO_FSPID) | I/O   | 5mA   | SPI MOSI, SIO mode       |
| 3       | UART1 TX  | 4    | GPIO                | Output    | —     | TCP bridge TX            |
| 4       | UART1 RX  | 5    | GPIO                | Input     | —     | TCP bridge RX            |
| 5       | SWCLK     | 6    | FSPICLK (FUNC_MTCK_FSPICLK) | Output | 5mA | SPI CLK           |
| 6       | GND       | —    | —                   | —         | —     |                          |
| 7       | nRESET    | 2    | GPIO                | OD out    | —     | Active low, pull-up      |
| 8       | nTRST/TDI | 10   | GPIO                | OD out    | —     | JTAG disabled, reused    |
| 9       | NC        | —    | —                   | —         | —     |                          |
| 10      | GND       | —    | —                   | —         | —     |                          |

## LED

| Function        | GPIO | Active | Control Location                |
|-----------------|------|--------|----------------------------------|
| WiFi status     | 0    | High   | `wifi_handle.c` event handler   |

## Cross-SoC Pin Comparison

| Signal     | ESP8266 | ESP32 | ESP32-C3 | ESP32-S3 |
|------------|---------|-------|----------|----------|
| SWCLK      | 14      | 14    | 6        | 12       |
| SWDIO      | 12/13   | 12/13 | 7        | 11       |
| UART1 TX   | —       | 23    | 4        | —        |
| UART1 RX   | —       | 22    | 5        | —        |
| nRESET     | 5       | 26    | 2        | 13       |
| nTRST      | 0       | 25    | 10       | 14       |
| LED        | 15      | 27    | 0        | 4        |

## ESP32-C3 IOMUX Details

When using SPI acceleration, pins must be routed through IOMUX (not GPIO matrix) for full-speed timing:

- **GPIO6 (SWCLK)**: `FUNC_MTCK_FSPICLK` — IO_MUX direct connect for SPI2 clock
- **GPIO7 (SWDIO)**: `FUNC_MTDO_FSPID` — IO_MUX direct connect for SPI2 MOSI

In GPIO fallback mode, pins are switched back via `PIN_FUNC_SELECT(IO_MUX_GPIOx_REG, FUNC_MTCK_GPIO6)` (or equivalent GPIO function).

## Drive Strength

`DAP_SETUP()` sets drive capability to `GPIO_DRIVE_CAP_0` (5mA) for SWCLK and SWDIO on ESP32-C3/S3. Increase if signal integrity is poor at higher frequencies, but watch for overshoot on long traces.

## Adding a New Board Variant

If creating a derivative board with different pins:

1. Update `PIN_*` macros in `components/DAP/config/DAP_config.h`.
2. Ensure the new SWCLK/SWDIO pins support IOMUX SPI2 if SPI acceleration is desired.
3. Update `PORT_JTAG_SETUP()` if the new TDO pin requires special handling (e.g., RTC pin on ESP8266).
4. Verify `DAP_SPI_Init()` uses correct `IO_MUX_GPIOx_REG` and `FUNC_*` constants.
