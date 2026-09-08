---
id: tgam_display
name: TGAM Display
description: "显示 TGAM1 脑电数据到 ST7735S 屏幕（专注度、放松度、信号质量）"
version: "1.0.0"
---

# TGAM Display Skill

显示 TGAM1 脑电模组的实时数据到 ST7735S 显示屏。

## 功能

- 显示专注度 (Attention) 0-100
- 显示放松度 (Meditation) 0-100
- 显示信号质量 (Signal) 0-100
- 显示 EEG 功率谱（可选）

## 使用方法

```lua
-- 启动显示
display_tgam.start()

-- 停止显示
display_tgam.stop()

-- 手动刷新
display_tgam.refresh()
```

## 接线

| TGAM1 | ESP32-S3 |
|-------|----------|
| VCC | 3.3V |
| GND | GND |
| TXD | GPIO18 (UART1 RX) |
| RXD | GPIO17 (UART1 TX) |

| ST7735S | ESP32-S3 |
|---------|----------|
| VCC | 3.3V |
| GND | GND |
| SCLK | GPIO5 (SPI2) |
| MOSI | GPIO7 (SPI2) |
| CS | GPIO4 |
| DC | GPIO8 |
| RST | GPIO6 |
| BL | GPIO46 |

## 依赖

- `board_manager` - 获取显示参数
- `display` - 屏幕驱动
- `uart` - TGAM1 串口通信
- `delay` - 延时
