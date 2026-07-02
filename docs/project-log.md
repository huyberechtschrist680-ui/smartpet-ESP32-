# 项目日志

## 2026-06-25

- 建立 PlatformIO + Arduino 工程骨架。
- 使用 `esp32-s3-devkitc-1` 作为 ESP32-S3 课程板的基础配置。
- 记录初始协作流程和硬件信息待确认项。

## 2026-06-26 至 2026-06-30

- 建立桌宠核心状态机：心情、饥饿/吃饱、睡眠、动作和事件队列。
- 拆分命令解析、显示模型、动作曲线、调试日志和硬件适配层。
- 接入真实硬件路径：OLED、双舵机、按钮、编码器和光敏输入。
- 保持核心原则：外设不直接修改 `PetState`，统一通过 `PetInput` 和 `ParsedCommand` 进入 `petUpdate()`。

## 2026-07-01

- 新增轻量 BLE 控制层：`BluetoothManager`。
- 新增手机网页 BLE 控制台：`web/index.html`。
- BLE 文本命令复用 `command_parser`，普通命令仍进入原有状态机路径。
- 针对 BLE 和 WiFi 同时工作时的启动压力，降低 BLE 广播负载并支持运行时关闭 BLE。

## 2026-07-02

- 新增 GPIO47 模式切换按钮。
- 默认模式保持 `BLE + WEATHER`。
- 网站模式切换为 `WEBSITE + WIFI`：关闭 BLE，保留 WiFi，启用网站控制。
- 新增 `NetworkTask`，将 WiFi、HTTPS、网站同步和天气请求放入独立 FreeRTOS 网络任务，避免阻塞主循环。
- 网站通信合并为 `/sync`：一次请求同时上传心跳、状态、ACK，并接收服务器命令。
- 将网站控制门面从 `SiteManager` 重命名为 `WebsiteControl`，使模块名称更贴近职责。
- 重写项目文档，修复早期乱码文档，并补充当前模块结构说明。

## 当前状态

- `pio run` 可编译通过。
- 默认 BLE 控制和网站控制通过 GPIO47 互斥切换。
- 网站服务器地址：`https://pet.liujiapeng.xyz/api/smartpet`。
- 默认设备 ID：`smartpet-01`。
- 网络任务仍使用 HTTPS 请求，实际稳定性需要结合供电、WiFi 信号和服务器响应时间继续测试。