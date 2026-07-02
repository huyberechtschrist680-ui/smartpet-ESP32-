#include "debug_logger.h"

#include "app_config.h"
#include "display_model.h"

namespace
{
  uint32_t lastStatusMs = 0;

  void printTimestamp(uint32_t nowMs)
  {
    Serial.printf("[%lu ms] ", static_cast<unsigned long>(nowMs));
  }

}  // namespace

void debugInit()
{
}

void debugLogEvent(uint32_t nowMs, const PetEvent &event, const PetState &state)
{
  (void)state;

  switch (event.type)
  {
  case AppEventType::HeadTouch:
    printTimestamp(nowMs);
    Serial.println("Head Touch");
    break;
  case AppEventType::Feed:
    printTimestamp(nowMs);
    Serial.println("Feed");
    break;
  case AppEventType::MotionTriggered:
    printTimestamp(nowMs);
    Serial.printf("Motion %d\n", static_cast<int>(event.motion));
    break;
  case AppEventType::SleepOn:
    printTimestamp(nowMs);
    Serial.println("Sleep Mode On");
    break;
  case AppEventType::SleepOff:
    printTimestamp(nowMs);
    Serial.println("Sleep Mode Off");
    break;
  case AppEventType::Error:
    printTimestamp(nowMs);
    Serial.println("Error");
    break;
  case AppEventType::None:
  default:
    break;
  }
}

void debugLogStatusIfNeeded(uint32_t nowMs, const PetState &state)
{
  if (state.power == PowerState::Sleeping)
  {
    lastStatusMs = nowMs;
    return;
  }

  if (nowMs - lastStatusMs < kDebugStatusIntervalMs)
  {
    return;
  }

  lastStatusMs = nowMs;
  const DisplayModel model = makeDisplayModel(state);

  printTimestamp(nowMs);
  Serial.println(model.line1);
  printTimestamp(nowMs);
  Serial.println(model.line2);
  printTimestamp(nowMs);
  Serial.println(model.line3);
}
