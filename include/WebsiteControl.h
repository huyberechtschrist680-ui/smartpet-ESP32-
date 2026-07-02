#pragma once

#include <Arduino.h>

#include "pet_state.h"

struct WebsiteCommand
{
  String id;
  String command;
};

void websiteControlInit();
void websiteControlSetEnabled(bool enabled);
bool websiteControlIsEnabled();
void websiteControlPoll(uint32_t nowMs);
bool websiteControlTakeCommand(WebsiteCommand &out);
void websiteControlAckCommand(const String &id, const String &command, const char *result);
void websiteControlNotifyStateIfNeeded(const PetState &state,
                                       const PetEventQueue &events,
                                       uint32_t nowMs,
                                       bool force);