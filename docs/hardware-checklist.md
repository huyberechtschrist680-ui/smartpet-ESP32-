# 硬件清单

本清单记录当前固件使用的硬件连接。修改引脚前，请先更新 `include/app_config.h` 中的宏定义，并重新执行 `pio run`。

## 主控

- 芯片/模组：ESP32-S3 系列。
- PlatformIO board：`esp32-s3-devkitc-1`。
- 当前环境：`course_s3_board`。
- 串口波特率：115200。
- 当前 USB 配置：`ARDUINO_USB_CDC_ON_BOOT=0`，`ARDUINO_USB_MODE=0`。

## 当前引脚分配

| 功能 | GPIO | 说明 |
| --- | --- | --- |
| OLED SDA | GPIO8 | I2C 数据线 |
| OLED SCL | GPIO9 | I2C 时钟线 |
| 左舵机 | GPIO15 | SG90 类舵机 PWM |
| 右舵机 | GPIO16 | SG90 类舵机 PWM |
| 摸头按钮 | GPIO17 | `INPUT_PULLUP`，按下接地 |
| 显示切换按钮 | GPIO10 | `INPUT_PULLUP`，按下接地 |
| 编码器 A | GPIO18 | `INPUT_PULLUP`，中断读取 |
| 编码器 B | GPIO21 | `INPUT_PULLUP`，中断读取 |
| 光敏 D0 | GPIO4 | 低光照睡眠判断输入 |
| 模式切换按钮 | GPIO47 | `INPUT_PULLUP`，按下接地，切换 BLE/网站模式 |

## 供电提醒

- 舵机建议使用独立稳定电源，ESP32 和舵机电源必须共地。
- WiFi 和 HTTPS 工作时会有电流峰值，如出现重启，优先检查供电余量和地线。
- BLE 和网站控制目前通过 GPIO47 互斥，避免同时启用造成额外资源压力。

## 验收清单

1. `pio run` 编译通过。
2. 串口输出正常，波特率 115200。
3. OLED 能显示普通状态、天气页和模式切换提示。
4. 摸头按钮能触发摸头输入。
5. 编码器能触发喂食输入。
6. 光敏低光照能触发睡眠规则。
7. GPIO47 能在 `BLE + WEATHER` 和 `WEBSITE + WIFI` 间切换。
8. BLE 模式下手机网页可连接 SmartPet 并发送命令。
9. 网站模式下服务器 `/sync` 能显示在线、状态，并下发命令。