#pragma once

#include <Arduino.h>

#include "app_types.h"

struct PetState
{
  PowerState power = PowerState::Normal;
  FoodState food = FoodState::Hungry;
  MotionMode motion = MotionMode::Null;
  int emotion = 5;
  uint32_t lastUpdateRealMs = 0; // 上轮执行时间
  uint32_t activeMs = 0;         // 清醒时间
  uint32_t lastEmotionDecayActiveMs = 0;
  uint32_t fullEndActiveMs = 0;
  bool hasHeadTouchHistory = false; // 解决开机真空期。
  uint32_t lastHeadTouchActiveMs = 0;
  bool lowLightOngoing = false;
  uint32_t lowLightStartRealMs = 0;
  uint32_t lastAutoMotionCheckActiveMs = 0;
  uint32_t motionStartActiveMs = 0;
  uint32_t motionDurationMs = 0;
};

struct PetInput
{
  bool headButtonPressed = false;
  bool feedGesture = false;
  bool lowLight = false;
  bool displayTogglePressed = false;
  bool hasCommand = false;
  ParsedCommand command;
};

struct PetEvent // 通知事件实例
{
  AppEventType type = AppEventType::None;
  MotionMode motion = MotionMode::Null;
  int value = 0;
};

struct PetEventQueue // 通知事件列表
{
  static constexpr size_t kCapacity = 8;

  PetEvent items[kCapacity];
  size_t count = 0;

  void clear();
  bool push(AppEventType type, MotionMode motion = MotionMode::Null, int value = 0);
};

void petInit(PetState &pet, uint32_t nowMs);
void petUpdate(PetState &pet, const PetInput &input, uint32_t nowMs, PetEventQueue &events);
const char *motionName(MotionMode mode);
uint32_t motionDurationMs(MotionMode mode);
