#include "SiteManager.h"

#include "NetworkTask.h"
#include "app_config.h"

namespace
{
  bool gEnabled = false;
  uint32_t gNextStatusMs = 0;

  bool timeReached(uint32_t nowMs, uint32_t targetMs)
  {
    return static_cast<int32_t>(nowMs - targetMs) >= 0;
  }

  uint32_t fullRemainSeconds(const PetState &state)
  {
    if (state.food != FoodState::Full || state.activeMs >= state.fullEndActiveMs)
    {
      return 0;
    }
    return (state.fullEndActiveMs - state.activeMs) / 1000;
  }

  void printSiteLine(const char *message)
  {
    Serial.print('[');
    Serial.print(millis());
    Serial.print(F(" ms] "));
    Serial.println(message);
  }

}  // namespace

void siteInit()
{
  gEnabled = false;
  gNextStatusMs = 0;
  networkTaskInit();
}

void siteSetEnabled(bool enabled)
{
  if (enabled == gEnabled)
  {
    return;
  }

  gEnabled = enabled;
  gNextStatusMs = 0;
  networkTaskSetWebsiteMode(enabled);
  printSiteLine(enabled ? "Site control enabled" : "Site control disabled");
}

bool siteIsEnabled()
{
  return gEnabled;
}

void sitePoll(uint32_t nowMs)
{
  (void)nowMs;
}

bool siteTakeCommand(SiteCommand &out)
{
  if (!gEnabled)
  {
    return false;
  }

  NetworkRemoteCommand command;
  if (!networkTaskTakeCommand(command))
  {
    return false;
  }

  out.id = command.id;
  out.command = command.command;
  return true;
}

void siteAckCommand(const String &id, const String &command, const char *result)
{
  if (!gEnabled || result == nullptr)
  {
    return;
  }

  networkTaskSubmitAck(id.c_str(), command.c_str(), result);
}

void siteNotifyStateIfNeeded(const PetState &state,
                             const PetEventQueue &events,
                             uint32_t nowMs,
                             bool force)
{
  if (!gEnabled)
  {
    return;
  }

  const bool eventTriggered = events.count > 0;
  const bool periodic = timeReached(nowMs, gNextStatusMs);

  NetworkPetStatus status;
  status.power = state.power;
  status.food = state.food;
  status.motion = state.motion;
  status.emotion = state.emotion;
  status.remainSeconds = fullRemainSeconds(state);
  status.uptimeMs = nowMs;
  status.eventTriggered = eventTriggered;
  status.force = force || periodic;
  networkTaskSubmitStatus(status);

  if (status.force || eventTriggered)
  {
    gNextStatusMs = nowMs + kSmartPetStatusIntervalMs;
  }
}
