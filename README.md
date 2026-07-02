# ESP32-S3 Course Project

这是一个面向 ESP32-S3-WROOM-1 系列芯片的大作业协作仓库。当前阶段先完成工程、工具链和协作流程搭建；开发板到手后再补充实际引脚、Flash/PSRAM 型号、上传端口和外设模块。

## 当前基线

- 构建工具：PlatformIO Core 6.1.19
- 默认环境：`esp32-s3-devkitc-1`
- 开发框架：Arduino for ESP32
- 串口波特率：115200
- 当前程序：串口启动自检、芯片信息输出、周期性 uptime/heap 状态输出

`esp32-s3-devkitc-1` 是临时基线配置，适合先做无外设编译和基础串口程序。最终开发板如果是带 PSRAM、不同 Flash 容量或第三方板型，需要按实物修改 `platformio.ini`。

## 常用命令

PowerShell 中可以直接调用插件自带的 PlatformIO Core：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" --version
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor
```

也可以在 VS Code 里打开本文件夹，通过 PlatformIO 侧边栏执行 Build、Upload、Monitor。

## 协作流程

1. 需求和硬件变化写到 `docs/project-log.md`。
2. 新增外设前先在 `docs/hardware-checklist.md` 记录型号、供电、电平、引脚和库依赖。
3. 每完成一个小功能，先保证 `pio run` 通过，再提交代码。
4. 板子到手后先只跑当前自检程序，确认串口输出稳定，再接外设。

## 目录说明

- `platformio.ini`：PlatformIO 环境、板卡和构建配置。
- `include/app_config.h`：项目级常量。
- `src/main.cpp`：当前固件入口。
- `docs/`：项目日志、硬件检查清单和报告素材。
- `test/`：后续放 PlatformIO 单元测试或板上验证说明。
