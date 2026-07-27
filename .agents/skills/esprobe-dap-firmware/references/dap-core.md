# CMSIS-DAP Core & SPI Acceleration Reference

## Core Files

| File                          | Purpose                                      |
|-------------------------------|----------------------------------------------|
| `components/DAP/source/DAP.c` | CMSIS-DAP command parser and state machine   |
| `components/DAP/source/SW_DP.c` | SWD transfer protocol (bit-bang + SPI fast)  |
| `components/DAP/source/JTAG_DP.c` | JTAG scan chain handling                   |
| `components/DAP/source/DAP_vendor.c` | Vendor command hooks (elaphureLink stubbed) |
| `components/DAP/source/spi_op.c` | SPI-accelerated SWD transfer routines       |
| `components/DAP/source/spi_switch.c` | SPI init/deinit and pin mux switching   |
| `components/DAP/source/swd_host.c` | High-level SWD host (reset, connect)      |

## GPIO Abstraction Layer

`gpio_op.h` provides `__STATIC_INLINE` macros for fast pin access. On ESP32-C3:

```c
GPIO_SET_LEVEL_HIGH(pin)   → gpio_ll_set_level(&GPIO, pin, 1)
GPIO_SET_LEVEL_LOW(pin)    → gpio_ll_set_level(&GPIO, pin, 0)
GPIO_GET_LEVEL(pin)        → gpio_ll_get_level(&GPIO, pin)
SWDIO_OUT_ENABLE()         → gpio_ll_output_enable(&GPIO, PIN_SWDIO_MOSI)
SWDIO_OUT_DISABLE()        → gpio_ll_output_disable(&GPIO, PIN_SWDIO_MOSI)
```

These are used by `SW_DP.c` for software bit-banging and by `DAP_config.h` for JTAG pin setup.

## SPI Acceleration Architecture

### When SPI Mode is Used

`DAP_SPI_Init()` is called when `DAP_SWJ_Clock` sets a frequency that benefits from hardware SPI (typically > 1 MHz). The firmware switches SWCLK/SWDIO pins from GPIO to SPI2 peripheral.

### SPI Configuration (ESP32-C3)

- **Peripheral**: `GPSPI2` (SPI2)
- **Clock**: 40 MHz (APB / 2)
- **Mode**: CPOL=1, CPHA=0 (idle high, sample on rising edge)
- **Bit order**: LSB first (matches SWD protocol)
- **Duplex**: Half-duplex (`doutdin = false`)
- **Buffer**: 64-byte hardware FIFO, no DMA

### Init Sequence (`DAP_SPI_Init`)

1. Enable SPI2 clock and clear reset.
2. Clear any previous GPIO output levels on SWCLK/SWDIO.
3. Route GPIO6/7 to SPI via IOMUX (`PIN_FUNC_SELECT(IO_MUX_GPIO6_REG, FUNC_MTCK_FSPICLK)`).
4. Configure SPI registers (master mode, no CS, clock divider, polarity, bit order).
5. Enable SPI clock gating (`clk_en`, `mst_clk_active`, `mst_clk_sel`).

### Deinit Sequence (`DAP_SPI_Deinit`)

1. Switch pins back to GPIO function.
2. Route pin output through `CPU_GPIO_OUTx_IDX` (GPIO matrix) instead of SPI peripheral.
3. Re-enable GPIO output and input on SWCLK and SWDIO.

### Acquire / Release

During a multi-byte SPI transaction, `DAP_SPI_Acquire()` keeps the clock pin muxed to SPI. `DAP_SPI_Release()` temporarily returns it to GPIO if the transfer routine needs to bit-bang a turnaround cycle.

## SWD Bit-Bang vs SPI Decision

`SW_DP.c` decides at runtime based on clock frequency:
- Low frequency or protocol turnaround phases → GPIO bit-bang via `PIN_SWCLK_TCK_SET/CLR` and `PIN_SWDIO_OUT/IN`.
- High frequency bulk data → `spi_op.c` routines that blast bytes through SPI FIFO.

## Adding SPI Support for a New SoC

1. Define `DAP_SPI` macro (e.g., `SPI2`, `GPSPI2`).
2. Implement `DAP_SPI_Init()`:
   - Enable peripheral clock.
   - Map SWCLK/SWDIO to correct IOMUX functions.
   - Set clock divider for 40 MHz (or target max).
   - Configure CPOL=1, CPHA=0, LSB first.
3. Implement `DAP_SPI_Deinit()`:
   - Return pins to GPIO mode.
   - Ensure output enable and input enable are restored.
4. Implement `DAP_SPI_Acquire()` / `DAP_SPI_Release()` if needed for pin mux toggling.
5. Verify register field names in `spi_struct.h` — they vary significantly between ESP32, ESP32-C3, ESP32-S3, etc.

## JTAG Mode

JTAG is **disabled** (`DAP_JTAG 0`). The project only supports SWD. JTAG-related pins (TDO, TDI, nTRST) are either reused for other functions or left undefined in the DAP protocol layer.

## RTT Support

**RTT (Real-Time Transfer) does not require SWO or extra pins.** It works over standard SWD memory read/write:
1. Target runs RTT code (Segger RTT or open-source alternative)
2. PC connects via USBIP to ESProbe
3. Use **OpenOCD** with its `rtt` command to poll target memory and forward RTT data

No firmware changes are needed for basic RTT support.

## Known Limitations

- **ESP32-C3 SPI untested**: The `spi_op.c` fast path may have timing issues with turnaround cycles. If debugging fails at high SWD frequencies, force GPIO mode by ensuring `DAP_SPI_Init()` is never called (or stub it).
- **No SWO**: `SWO_FUNCTION_ENABLE == 0`. UART SWO and Manchester SWO are not compiled in.
- **No adaptive clocking**: `PIN_SWCLK_TCK_IN()` returns 0; the firmware does not support wait-state detection from the target.
