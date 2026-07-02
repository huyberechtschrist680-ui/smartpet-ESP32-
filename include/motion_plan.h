#pragma once

#include <Arduino.h>

#include "app_types.h"

struct ServoAngles
{
  int left = 0;
  int right = 0;
};

ServoAngles getServoAngles(MotionMode mode, uint32_t elapsedMs);
