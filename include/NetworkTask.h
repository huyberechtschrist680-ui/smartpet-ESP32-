#pragma once

#include <Arduino.h>

#include "app_types.h"

struct NetworkRemoteCommand
{
  char id[48];
  char command[80];
};

struct NetworkPetStatus
{
  PowerState power = PowerState::Normal;
  FoodState food = FoodState::Hungry;
  MotionMode motion = MotionMode::Null;
  int emotion = 0;
  uint32_t remainSeconds = 0;
  uint32_t uptimeMs = 0;
  bool eventTriggered = false;
  bool force = false;
};

void networkTaskInit();
void networkTaskSetWebsiteMode(bool enabled);
bool networkTaskWebsiteModeEnabled();
bool networkTaskTakeCommand(NetworkRemoteCommand &out);
void networkTaskSubmitAck(const char *id, const char *command, const char *result);
void networkTaskSubmitStatus(const NetworkPetStatus &status);
bool networkTaskIsWifiConnected();
