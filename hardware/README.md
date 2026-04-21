# ESProbe 硬件

ESProbe 采用立创 EDA 设计，单面 PCB，核心为 ESP32-C3-WROOM-02-N4 模组。

## 主要器件

| 位号 | 型号 | 说明 | 供应商 |
|------|------|------|--------|
| U1 | ESP32-C3-WROOM-02-N4 | 主控模组，4MB Flash | LCSC C2934560 |
| CN1 | HCZZ0451-8 | 1x8P 卧贴连接器，间距 2mm，调试接口 | LCSC C7433654 |
| USB1 | — | Type-C USB 接口，供电 + UART0 下载 | — |
| D1 | XL-1005UBC | 0402 蓝色 LED，WiFi 状态指示 | LCSC C965795 |
| U3 | — | LDO，3.3V 稳压 | — |
| SW1 | — | 轻触按键，复位/下载 | — |

## 调试连接器（CN1）定义

CN1 为 1x8P（实际引出 10pin，含重复 GND）卧贴连接器，间距 2mm：

```
Pin 1  ── VCC     (3.3V 输出至目标)
Pin 2  ── GND
Pin 3  ── IO4     (UART1 TX)
Pin 4  ── IO5     (UART1 RX)
Pin 5  ── IO6     (SWCLK / FSPICLK)
Pin 6  ── IO7     (SWDIO / FSPID)
Pin 7  ── IO2     (nRESET)
Pin 8  ── IO10    (nTRST / TDI，当前 JTAG 已禁用)
Pin 9  ── GND
Pin 10 ── GND
```

> 注意：Pin 7/8 在物理连接器上为第 7、8 脚，对应原理图中的 IO2 和 IO10。

## 设计文件说明

| 文件 | 格式 | 说明 |
|------|------|------|
| `ESProbe.eprj2` | 立创 EDA 专业版 | 原理图 + PCB 项目文件 |
| `Netlist_Main_2026-04-13.json` | JSON | 立创 EDA 导出的网表 |
| `Netlist_Main_2026-04-13.enet` | 立创网表 | 备用网表格式 |
| `3d/*.stl` | STL | 外壳 3D 打印模型 |
| `ESProbe_backup/` | 立创 EDA | 自动备份历史版本 |

## 硬件特性

- **供电**：5V USB Type-C 输入，板载 LDO 降至 3.3V
- **天线**：ESP32-C3-WROOM-02 板载 PCB 天线，保持 PCB 天线区域净空
- **尺寸**：小型化设计，适配 3D 打印外壳
- **下载**：通过 USB Type-C 直连 UART0，支持一键下载（按住 BOOT + 按 RESET）

## 制板建议

1. 使用立创 EDA 打开 `ESProbe.eprj2`，检查 DRC 后导出 Gerber
2. PCB 天线区域禁止铺铜和放置元件
3. 建议板厚 1.0mm 或 1.2mm，适配 3D 外壳
4. CN1 连接器推荐回流焊，注意卧贴方向与外壳开口对齐

## 外壳

`3d/` 目录包含 STL 格式的上下壳模型，使用 FDM 3D 打印（PLA/PETG 均可）。
安装时先将 PCB 放入下壳，再扣合上壳，CN1 连接器从外壳侧面开口露出。
