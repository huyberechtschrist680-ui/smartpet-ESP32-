#pragma once

#include <Arduino.h>

enum class PowerState : uint8_t
{
  Normal,
  Sleeping,
};

enum class FoodState : uint8_t
{
  Hungry,
  Full,
};

enum class MotionMode : uint8_t
{
  Null = 0,
  Play = 1,
  Idle = 2,
  Tire = 3,
  Forward = 4,
};

enum class CommandType : uint8_t
{
  None,
  SetEmotion,
  SetFull,
  SetMotion,
  SetSleep,
};

enum class AppEventType : uint8_t // 通知事件类型
{
  None,
  HeadTouch,
  Feed,
  MotionTriggered,
  SleepOn,
  SleepOff,
  Error,
};

enum class ControlMode : uint8_t
{
  BlePhone,
  Website,
};

struct ParsedCommand
{
  CommandType type = CommandType::None;
  bool valid = false;
  int intValue = 0;
  bool boolValue = false;
  MotionMode motionValue = MotionMode::Null;
};
