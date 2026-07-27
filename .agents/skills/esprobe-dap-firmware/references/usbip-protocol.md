# USBIP Protocol Reference

## Overview

The firmware emulates a USB device over TCP using the Linux USB/IP protocol. The host runs a USBIP client (e.g., `usbip` on Linux, or usbip-win on Windows) that attaches to `esprobe_ip:3240`.

## Connection Lifecycle

### Stage 1: Discovery / Attach

The TCP server (`tcp_server.c`) accepts a connection, reads a 4-byte header, then branches:

| Header (ntohl) | Command                | Handler               |
|----------------|------------------------|-----------------------|
| `0x8005`       | `OP_REQ_DEVLIST`       | `attach()` → device list |
| `0x8003`       | `OP_REQ_IMPORT`        | `attach()` → single device info |

Both replies include:
- `usbip_stage1_header` (version 273, status 0)
- `usbip_stage1_response_devlist` (list_size = 1)
- `usbip_stage1_usb_device` (vendor/product IDs from descriptor)
- `usbip_stage1_usb_interface` (class/subclass/protocol)

### Stage 2: URB Emulation

After attach, `usbip_urb_process()` loops reading 48-byte USBIP stage2 headers from the socket.

#### URB Types

| Command                      | Direction | EP  | Handler                        |
|------------------------------|-----------|-----|--------------------------------|
| `USBIP_STAGE2_REQ_SUBMIT`    | OUT       | 1   | `handle_dap_data_request()`    |
| `USBIP_STAGE2_REQ_SUBMIT`    | IN        | 1   | `fast_reply()`                 |
| `USBIP_STAGE2_REQ_SUBMIT`    | —         | 0   | `handleUSBControlRequest()`    |
| `USBIP_STAGE2_REQ_UNLINK`    | —         | —   | `handle_unlink()`              |

#### Data Flow (EP1 OUT → IN)

1. Host sends DAP command payload in `USBIP_CMD_SUBMIT` OUT.
2. `handle_dap_data_request()` copies payload to `dap_dataIN_handle` ringbuffer.
3. `xTaskNotifyGive(kDAPTaskHandle)` wakes `DAP_Thread`.
4. `DAP_Thread` calls `DAP_ProcessCommand()`, writes response to `dap_dataOUT_handle`.
5. Host sends `USBIP_CMD_SUBMIT` IN (ep=1, dir=IN).
6. `fast_reply()` pulls response from `dap_dataOUT_handle` and sends `USBIP_RET_SUBMIT`.

#### Unlink Handling

`USBIP_CMD_UNLINK` aborts pending URBs. The firmware drains stale responses from `dap_dataOUT_handle` to prevent out-of-sync replies that crash the host driver.

## USB Descriptors

Descriptors are hardcoded in `components/USBIP/`:

- `usb_descriptor.c` — Standard USB device/configuration/interface/endpoint descriptors
- `MSOS20_descriptor.c` — Microsoft OS 2.0 descriptor for WinUSB auto-binding

### WinUSB Mode

When `USE_WINUSB == 1`:
- Packet size = 512 bytes (`DAP_PACKET_SIZE`)
- MSOS20 descriptor tells Windows to bind the interface to WinUSB.sys without an INF file
- USB endpoint 1 is used for bulk transfer (instead of HID interrupt)

### Control Requests (EP0)

`handleUSBControlRequest()` in `components/USBIP/usb_handle.c` handles:
- `GET_DESCRIPTOR` (device, config, string, MSOS20)
- `SET_CONFIGURATION`
- Vendor-specific MSOS20 queries

## Adding New Endpoints

Current descriptor defines EP1 (DAP data) and reserves EP3 (SWO, not implemented). To add a new endpoint:

1. Update endpoint descriptor in `usb_descriptor.c`.
2. Add EP number/direction handling in `usbip_urb_process()`.
3. Ensure `send_stage2_submit()` / `send_stage2_submit_data()` populate `data_length` correctly.

## Debugging USBIP Issues

- **"Unknown protocol"** in logs → Host sent unexpected stage1 command; check header bytes with Wireshark.
- **Host driver panic after reconnect** → Likely stale DAP response in ringbuffer; verify `handle_dap_unlink()` drains `dap_dataOUT_handle`.
- **Zero-length URB spam** → Normal for IN endpoints when no data is ready; `fast_reply()` sends empty `RET_SUBMIT`.
