#include "pet_state.h"

#include "app_config.h"

namespace
{
  int clampEmotion(int value)
  {
    if (value < kEmotionMin)
    {
      return kEmotionMin;
    }
    if (value > kEmotionMax)
    {
      return kEmotionMax;
    }
    return value;
  }

  bool timeReached(uint32_t nowMs, uint32_t targetMs)
  {
    return static_cast<int32_t>(nowMs - targetMs) >= 0;
  }

  void setMotion(PetState &pet, MotionMode mode, PetEventQueue *events)
  {
    pet.motion = mode;
    pet.motionStartActiveMs = pet.activeMs;
    pet.motionDurationMs = motionDurationMs(mode);

    if (events != nullptr && mode != MotionMode::Null)
    {
      events->push(AppEventType::MotionTriggered, mode, static_cast<int>(mode));
    }
  }

  void setFull(PetState &pet)
  {
    pet.food = FoodState::Full;
    pet.fullEndActiveMs = pet.activeMs + kFullDurationMs;
    pet.lastEmotionDecayActiveMs = pet.activeMs;
  }

  void setHungry(PetState &pet)
  {
    pet.food = FoodState::Hungry;
    pet.fullEndActiveMs = pet.activeMs;
    pet.lastEmotionDecayActiveMs = pet.activeMs;
  }

  void enterSleep(PetState &pet, PetEventQueue &events)
  {
    if (pet.power == PowerState::Sleeping)
    {
      return;
    }

    pet.power = PowerState::Sleeping;
    pet.motion = MotionMode::Null;
    pet.motionDurationMs = 0;
    pet.lowLightOngoing = false;
    events.push(AppEventType::SleepOn);
  }

  void exitSleep(PetState &pet, PetEventQueue &events)
  {
    if (pet.power == PowerState::Normal)
    {
      return;
    }

    pet.power = PowerState::Normal;
    pet.motion = MotionMode::Null;
    pet.motionDurationMs = 0;
    pet.lowLightOngoing = false;
    events.push(AppEventType::SleepOff);
  }

  bool handleSleepCommand(PetState &pet, const PetInput &input, PetEventQueue &events)
  {
    if (!input.hasCommand || !input.command.valid || input.command.type != CommandType::SetSleep)
    {
      return false;
    }

    if (input.command.boolValue)
    {
      enterSleep(pet, events);
    }
    else
    {
      exitSleep(pet, events);
    }
    return true;
  }

  void updateLowLightSleep(PetState &pet, const PetInput &input, uint32_t nowMs, PetEventQueue &events)
  {
    if (!input.lowLight)
    {
      pet.lowLightOngoing = false;
      return;
    }

    if (!pet.lowLightOngoing)
    {
      pet.lowLightOngoing = true;
      pet.lowLightStartRealMs = nowMs;
      return;
    }

    if (nowMs - pet.lowLightStartRealMs >= kDarkToSleepMs)
    {
      enterSleep(pet, events);
    }
  }

  void updateFoodAndEmotion(PetState &pet)
  {
    if (pet.food == FoodState::Full)
    {
      if (timeReached(pet.activeMs, pet.fullEndActiveMs))
      {
        setHungry(pet);
      }
      else
      {
        pet.lastEmotionDecayActiveMs = pet.activeMs;
        return;
      }
    }

    while (pet.food == FoodState::Hungry &&
           pet.activeMs - pet.lastEmotionDecayActiveMs >= kEmotionDecayIntervalMs)
    {
      pet.lastEmotionDecayActiveMs += kEmotionDecayIntervalMs;
      pet.emotion = clampEmotion(pet.emotion - 1);
    }
  }

  void updateMotionEnd(PetState &pet)
  {
    if (pet.motion == MotionMode::Null || pet.motionDurationMs == 0)
    {
      return;
    }

    if (pet.activeMs - pet.motionStartActiveMs >= pet.motionDurationMs)
    {
      pet.motion = MotionMode::Null;
      pet.motionDurationMs = 0;
    }
  }

  void updateAutomaticMotion(PetState &pet, bool suppressAutoMotion, PetEventQueue &events)
  {
    while (pet.activeMs - pet.lastAutoMotionCheckActiveMs >= kAutoMotionCheckIntervalMs) // 补偿卡顿
    {
      pet.lastAutoMotionCheckActiveMs += kAutoMotionCheckIntervalMs;

      if (suppressAutoMotion || pet.motion != MotionMode::Null) // 避免动作冲突
      {
        continue;
      }

      if (random(30) == 0)
      {
        setMotion(pet, pet.emotion >= 5 ? MotionMode::Idle : MotionMode::Tire, &events);
      }
    }
  }

  void applyNormalCommand(PetState &pet, const ParsedCommand &command, PetEventQueue &events) // 处理常态命令。
  {
    switch (command.type)
    {
    case CommandType::SetEmotion:
      pet.emotion = clampEmotion(command.intValue);
      break;
    case CommandType::SetFull:
      if (command.boolValue)
      {
        setFull(pet);
      }
      else
      {
        setHungry(pet);
      }
      break;
    case CommandType::SetMotion:
      setMotion(pet, command.motionValue, &events);
      break;
    case CommandType::SetSleep:
    case CommandType::None:
      break;
    }
  }

  void handleFeedGesture(PetState &pet, PetEventQueue &events)
  {
    setFull(pet);
    events.push(AppEventType::Feed);
  }

  void handleHeadTouch(PetState &pet, PetEventQueue &events)
  {
    if (pet.hasHeadTouchHistory &&
        pet.activeMs - pet.lastHeadTouchActiveMs < kHeadTouchCooldownMs)
    {
      return;
    }

    pet.hasHeadTouchHistory = true;
    pet.lastHeadTouchActiveMs = pet.activeMs;
    pet.emotion = clampEmotion(pet.emotion + 1);
    events.push(AppEventType::HeadTouch);
    setMotion(pet, MotionMode::Play, &events);
  }

} // namespace

void PetEventQueue::clear()
{
  count = 0;
}

bool PetEventQueue::push(AppEventType type, MotionMode motion, int value)
{
  if (count >= kCapacity)
  {
    return false;
  }

  PetEvent event;
  event.type = type;
  event.motion = motion;
  event.value = value;
  items[count++] = event;
  return true;
}

void petInit(PetState &pet, uint32_t nowMs)
{
  pet.power = PowerState::Normal;
  pet.food = FoodState::Hungry;
  pet.motion = MotionMode::Null;
  pet.emotion = kEmotionInitial;
  pet.lastUpdateRealMs = nowMs;
  pet.activeMs = 0;
  pet.lastEmotionDecayActiveMs = 0;
  pet.fullEndActiveMs = 0;
  pet.hasHeadTouchHistory = false;
  pet.lastHeadTouchActiveMs = 0;
  pet.lowLightOngoing = false;
  pet.lowLightStartRealMs = nowMs;
  pet.lastAutoMotionCheckActiveMs = 0;
  pet.motionStartActiveMs = 0;
  pet.motionDurationMs = 0;
}

void petUpdate(PetState &pet, const PetInput &input, uint32_t nowMs, PetEventQueue &events)
{
  events.clear(); // 清空上轮事件队列。

  const uint32_t elapsedRealMs = nowMs - pet.lastUpdateRealMs;
  if (pet.power == PowerState::Normal)
  {
    pet.activeMs += elapsedRealMs;
  }
  pet.lastUpdateRealMs = nowMs;

  if (pet.power == PowerState::Sleeping)
  {
    if (handleSleepCommand(pet, input, events))
    {
      return;
    }

    if (input.headButtonPressed)
    {
      exitSleep(pet, events);
    }
    return;
  }

  if (handleSleepCommand(pet, input, events))
  {
    return;
  }

  updateLowLightSleep(pet, input, nowMs, events);
  if (pet.power == PowerState::Sleeping)
  {
    return;
  }

  updateFoodAndEmotion(pet);
  updateMotionEnd(pet);

  const bool suppressAutoMotion = input.headButtonPressed || input.feedGesture || input.hasCommand;

  if (input.hasCommand)
  {
    if (!input.command.valid)
    {
      events.push(AppEventType::Error);
    }
    else
    {
      applyNormalCommand(pet, input.command, events);
    }
  }

  if (input.feedGesture)
  {
    handleFeedGesture(pet, events);
  }

  if (input.headButtonPressed)
  {
    handleHeadTouch(pet, events);
  }

  updateAutomaticMotion(pet, suppressAutoMotion, events);
}

const char *motionName(MotionMode mode)
{
  switch (mode)
  {
  case MotionMode::Null:
    return "NULL";
  case MotionMode::Play:
    return "PLAY";
  case MotionMode::Idle:
    return "IDLE";
  case MotionMode::Tire:
    return "TIRE";
  case MotionMode::Forward:
    return "FOWD";
  default:
    return "NULL";
  }
}

uint32_t motionDurationMs(MotionMode mode)
{
  switch (mode)
  {
  case MotionMode::Play:
    return 2000;
  case MotionMode::Idle:
    return 4000;
  case MotionMode::Tire:
    return 3000;
  case MotionMode::Forward:
    return 4000;
  case MotionMode::Null:
  default:
    return 0;
  }
}
