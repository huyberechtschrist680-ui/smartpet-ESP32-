# 软件结构说明

本文只说明当前文件组织和模块职责，不描述新的功能需求。

## 数据流

主循环的核心数据流是：

```text
硬件/串口/BLE/网站
        |
        v
PetInput 或 ParsedCommand
        |
        v
petUpdate()
        |
        v
PetState + PetEventQueue
        |
        v
显示、舵机、日志、远程状态同步
```

所有外部控制都应通过 `PetInput` 或 `ParsedCommand` 进入状态机，不应直接修改 `PetState`。

## 核心层

- `app_types.h`：全局枚举和命令结构。
- `pet_state.h/.cpp`：桌宠状态机，只处理状态变化和事件。
- `command_parser.h/.cpp`：文本命令解析，供串口、BLE 和网站共用。

## 调度层

- `main.cpp`：系统入口和主循环调度。
- `extension_manager.h/.cpp`：扩展钩子，目前用于天气页切换。

## 硬件层

- `hardware_io.h`：硬件抽象接口。
- `hardware_io_real.cpp`：真实硬件实现，包括按钮、编码器、光敏、OLED 和舵机。
- `motion_plan.h/.cpp`：将动作模式转换为舵机角度。
- `display_model.h/.cpp`：将 `PetState` 转换为 OLED 三行显示内容。

## 通信层

- `BluetoothManager.h/.cpp`：BLE GATT Server，负责手机 Web Bluetooth 文本命令通道。
- `WebsiteControl.h/.cpp`：网站控制门面，负责把主循环中的状态和 ACK 交给网络任务，并从网络任务取回网站命令。
- `NetworkTask.h/.cpp`：独立 FreeRTOS 网络任务，负责 WiFi、网站 `/sync` 和天气 HTTP 请求。
- `weather_service.h/.cpp`：天气缓存和天气页数据填充。联网请求不在此文件中执行。

## 文档和前端

- `web/index.html`：BLE 手机网页控制台。
- `docs/project-log.md`：开发日志。
- `docs/hardware-checklist.md`：硬件和引脚清单。
- `docs/report-outline.md`：课程报告提纲。

## 命名约定

- `BluetoothManager` 表示 BLE 设备侧管理模块。
- `WebsiteControl` 表示网站控制门面，不直接表达底层 HTTP 细节。
- `NetworkTask` 表示运行在 FreeRTOS 任务中的网络执行层。
- `weather_service` 当前保留原文件名，但职责已变为天气缓存和显示页数据。

## 后续只做结构优化时的边界

可以移动文件、重命名模块、拆分大文件、补文档，但不应改变：

- `PetState` 字段含义。
- `petUpdate()` 的状态机语义。
- 串口命令语义。
- BLE/网站命令进入 `ParsedCommand` 或 `PetInput` 的路径。
- 睡眠规则、按钮、编码器、光敏、OLED、舵机输出行为。