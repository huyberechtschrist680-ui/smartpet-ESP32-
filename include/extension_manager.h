#pragma once

#include <Arduino.h>

#include "pet_state.h"

void extensionInit();
void extensionBeforePetUpdate(PetInput &input, uint32_t nowMs);
void extensionAfterPetUpdate(const PetState &state, const PetEventQueue &events, uint32_t nowMs);
