#pragma once

#include <Arduino.h>

#include "pet_state.h"

struct SiteCommand
{
  String id;
  String command;
};

void siteInit();
void siteSetEnabled(bool enabled);
bool siteIsEnabled();
void sitePoll(uint32_t nowMs);
bool siteTakeCommand(SiteCommand &out);
void siteAckCommand(const String &id, const String &command, const char *result);
void siteNotifyStateIfNeeded(const PetState &state,
                             const PetEventQueue &events,
                             uint32_t nowMs,
                             bool force);