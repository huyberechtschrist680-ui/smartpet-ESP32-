#pragma once

#include <Arduino.h>

#include "pet_state.h"

void debugInit();
void debugLogEvent(uint32_t nowMs, const PetEvent &event, const PetState &state);
void debugLogStatusIfNeeded(uint32_t nowMs, const PetState &state);
