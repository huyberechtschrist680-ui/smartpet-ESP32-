# ESP32-S3 智能桌宠

这是一个基于 PlatformIO + Arduino 的 ESP32-S3 智能桌宠工程。当前固件围绕一个稳定的状态机核心构建，外围功能通过硬件输入、串口命令、BLE 手机网页控制台、网站控制台和天气服务接入。

## 当前功能

- 桌宠状态机：心情、饥饿/吃饱、睡眠、动作和事件队列。
- 真实硬件：OLED 显示、双舵机、摸头按钮、显示切换按钮、GPIO47 模式按钮、旋转编码器、光敏低光照睡眠输入。
- 串口命令：`setemo`、`setful`、`setmot`、`setslp`/`setsleep`。
- BLE 控制：ESP32-S3 作为 BLE Peripheral，手机网页通过 Web Bluetooth 发送文本命令。
- 网站控制：GPIO47 切换到网站模式后关闭 BLE，通过 WiFi 与服务器 `/sync` 接口同步心跳、状态、ACK 和指令。
- 天气显示：网络任务获取天气，天气页通过 OLED 显示缓存结果。

## 运行模式

开机默认模式是 `BLE + WEATHER`：BLE 控制台可用，同时 WiFi 网络任务负责天气缓存。

按下 GPIO47 模式按钮后切换到 `WEBSITE + WIFI`：BLE 被关闭，网站控制启用。再次按下 GPIO47 会切回默认模式。切换时 OLED 会显示当前模式约 1 秒。

## 构建

PowerShell 中可直接运行：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

当前默认环境：

```ini
default_envs = course_s3_board
```

## 目录结构

- `platformio.ini`：PlatformIO 环境、板卡、编译宏和依赖配置。
- `include/app_config.h`：引脚、时间参数、WiFi、BLE 和网站 API 配置。
- `include/app_types.h`：核心枚举和 `ParsedCommand`。
- `include/pet_state.h` / `src/pet_state.cpp`：桌宠状态机核心。
- `include/command_parser.h` / `src/command_parser.cpp`：串口、BLE、网站共用文本命令解析。
- `include/hardware_io.h` / `src/hardware_io_real.cpp`：真实硬件适配层。
- `include/BluetoothManager.h` / `src/BluetoothManager.cpp`：BLE GATT Server 和 Web Bluetooth 命令通道。
- `include/WebsiteControl.h` / `src/WebsiteControl.cpp`：网站控制门面，连接主循环和网络任务。
- `include/NetworkTask.h` / `src/NetworkTask.cpp`：WiFi、网站 `/sync`、天气 HTTP 和 FreeRTOS 网络任务。
- `include/weather_service.h` / `src/weather_service.cpp`：天气缓存和 OLED 天气页数据填充。
- `include/display_model.h` / `src/display_model.cpp`：OLED 三行显示模型。
- `include/motion_plan.h` / `src/motion_plan.cpp`：动作到舵机角度曲线。
- `include/debug_logger.h` / `src/debug_logger.cpp`：串口调试日志。
- `include/extension_manager.h` / `src/extension_manager.cpp`：扩展钩子，目前负责天气页切换。
- `web/index.html`：手机 Web Bluetooth 控制台。
- `docs/`：项目日志、硬件清单、结构说明和报告素材。

## 架构原则

- `pet_state.cpp` 只处理桌宠状态机，不直接访问 BLE、WiFi、OLED 或舵机。
- 所有外部控制最终转换成 `PetInput` 或 `ParsedCommand`，再交给 `petUpdate()`。
- 主循环保持调度职责：读取输入、调用状态机、应用输出、提交远程状态。
- 阻塞式 WiFi/HTTPS 请求放在 `NetworkTask` 中，不进入主循环。
- BLE 回调只缓存短文本命令，不直接操作状态机、OLED 或舵机。