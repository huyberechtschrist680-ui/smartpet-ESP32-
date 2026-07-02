# Hardware Checklist

开发板到手后，先补齐这些信息，再改代码或接外设。

## Board

- 完整开发板名称：
- 芯片/模组标识：
- Flash 容量：
- PSRAM 容量：
- USB 连接方式：USB-UART / Native USB / 两者都有
- 供电方式：
- 板载 LED/RGB LED GPIO：
- BOOT/RESET 按键位置：

## First Validation

1. 只连接 USB，不接外设。
2. 运行 `pio device list`，记录实际串口名。
3. 运行 `pio run`，确认固件可以编译。
4. 运行 `pio run -t upload`，上传当前自检程序。
5. 运行 `pio device monitor`，确认能看到启动报告和周期性状态输出。

不要只凭系统已有 COM 口判断开发板已经连接；以实际插拔变化和串口输出为准。

## Peripheral Record

| Module | Interface | Power | GPIO | Library | Notes |
| --- | --- | --- | --- | --- | --- |
| TBD | TBD | TBD | TBD | TBD | TBD |
