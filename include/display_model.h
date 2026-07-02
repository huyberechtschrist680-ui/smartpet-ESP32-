#pragma once

#include <Arduino.h>

#include "pet_state.h"

constexpr size_t kDisplayLineLength = 32;

struct DisplayModel
{
  bool blank = false;
  char line1[kDisplayLineLength] = "";
  char line2[kDisplayLineLength] = "";
  char line3[kDisplayLineLength] = "";
};

DisplayModel makeDisplayModel(const PetState &pet);
