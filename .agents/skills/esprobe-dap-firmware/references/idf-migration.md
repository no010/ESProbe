# ESP-IDF Migration Reference

This project was ported from ESP-IDF 4.x patterns to ESP-IDF 5.5.3. Use this when upgrading to future IDF versions or adding new SoC support.

## Critical API Changes (4.x → 5.5.x)

### Network Stack

| Old API (4.x)                  | New API (5.5.x)                                | File              |
|--------------------------------|------------------------------------------------|-------------------|
| `tcpip_adapter_init()`         | `esp_netif_init()`                             | `wifi_handle.c`   |
| `tcpip_adapter_create_default…`| `esp_netif_create_default_wifi_sta()`          | `wifi_handle.c`   |
| `tcpip_adapter_dhcpc_stop()`   | `esp_netif_dhcpc_stop()`                       | `wifi_handle.c`   |
| `tcpip_adapter_set_ip_info()`  | `esp_netif_set_ip_info()`                      | `wifi_handle.c`   |
| `esp_event_loop_init()`        | `esp_event_loop_create_default()`              | `wifi_handle.c`   |
| `ESP_IF_WIFI_STA`              | `WIFI_IF_STA`                                  | `wifi_handle.c`   |
| `IP4_ADDR(&ip, a,b,c,d)`       | `ip.addr = ESP_IP4TOADDR(a,b,c,d)`             | `wifi_handle.c`   |
| `system_event_t`               | `esp_event_base_t` + `int32_t event_id`        | `wifi_handle.c`   |

### GPIO HAL

| Old Pattern                    | New Pattern (IDF 5.5.x)                        | Notes             |
|--------------------------------|------------------------------------------------|-------------------|
| `GPIO.enable_w1ts = …`         | `gpio_ll_output_enable(&GPIO, pin)`            | RISC-V safe       |
| `GPIO.enable_w1tc = …`         | `gpio_ll_output_disable(&GPIO, pin)`           | RISC-V safe       |
| `GPIO.out_w1ts = …`            | `gpio_ll_set_level(&GPIO, pin, 1)`             | Unified           |
| `GPIO.out_w1tc = …`            | `gpio_ll_set_level(&GPIO, pin, 0)`             | Unified           |
| `GPIO.in >> pin`               | `gpio_ll_get_level(&GPIO, pin)`                | Unified           |
| `GPIO.pin[x].pad_driver = 0`   | `gpio_ll_od_disable(&GPIO, pin)`               | ESP32-C3/S3       |
| `GPIO.pin[x].pad_driver = 1`   | `gpio_ll_od_enable(&GPIO, pin)`                | ESP32-C3/S3       |
| `PIN_INPUT_ENABLE()`           | `gpio_ll_input_enable(&GPIO, pin)`             | ESP32-C3/S3       |

### SoC Headers

ESP-IDF 5.5 restructured register headers:

| Old Path (IDF 4.x)                          | New Path (IDF 5.5.x)                          |
|---------------------------------------------|-----------------------------------------------|
| `soc/esp32c3/include/soc/gpio_struct.h`     | `soc/gpio_struct.h` (C3 uses flat path)       |
| `soc/esp32c3/include/soc/dport_access.h`    | `soc/dport_access.h`                          |
| `soc/esp32c3/include/soc/periph_defs.h`     | `soc/periph_defs.h`                           |
| `hal/esp32c3/include/hal/gpio_ll.h`         | `hal/gpio_ll.h`                               |

Note: ESP32 and ESP32-S3 still use `soc/esp32s3/include/soc/gpio_struct.h` style paths.

### Register Struct Field Names

Espressif changed field names in `gpio_struct.h` for IDF 5.5:

| Old Name                              | New Name                        | Context           |
|---------------------------------------|---------------------------------|-------------------|
| `GPIO.func_out_sel_cfg[N].func_sel`   | `GPIO.func_out_sel_cfg[N].out_sel` | `spi_switch.c` |
| `GPIO.func_in_sel_cfg[N].func_sel`    | `GPIO.func_in_sel_cfg[N].in_sel`   | `spi_switch.c` |

Verify with `soc/gpio_struct.h` for your target if build fails with "has no member named".

### Toolchain Incompatibilities

**CSR GPIO (ESP32-C3)**: The `RV_SET_CSR(CSR_GPIO_OUT_USER, …)` inline assembly used in older versions is **not supported** by riscv32-esp-elf-gcc 14.2 in IDF 5.5.3. Replaced with `gpio_ll_set_level()` and `gpio_ll_get_level()`.

## Component Manager

Dependencies are managed via `idf.py add-dependency`:
```powershell
idf.py add-dependency espressif/mdns
```
This creates `main/idf_component.yml` and downloads to `managed_components/`.

## CMake Modernization

All components use `idf_component_register()`:

```cmake
idf_component_register(
    SRCS "source1.c" "source2.c"
    INCLUDE_DIRS "include" "config"
    REQUIRES "driver" "freertos"
    PRIV_REQUIRES "main"
)
```

Avoid legacy `register_component()` or manual `add_library()` patterns.

## sdkconfig.defaults Rules

When adding new WiFi features, maintain this invariant:
```
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM >= CONFIG_ESP_WIFI_RX_BA_WIN / 2
```
Current defaults set both to 32.

## Debugging Build Failures

1. `fatal error: soc/xxx: No such file` → Check `gpio_common.h` include paths for your target.
2. `error: 'func_sel' has no member named` → Update to new field name (`out_sel`/`in_sel`).
3. `implicit declaration of function 'tcpip_adapter_...'` → You are using obsolete network API; migrate to `esp_netif_*`.
4. `undefined reference to 'gpio_matrix_out'` → Use `gpio_ll_iomux_out` / `gpio_ll_iomux_in` or `PIN_FUNC_SELECT` instead.
