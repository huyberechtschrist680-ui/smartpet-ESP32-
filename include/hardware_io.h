#pragma once

#include "display_model.h"
#include "motion_plan.h"

struct HardwareInput
{
  bool headButtonPressed = false;
  bool feedGesture = false;
  bool lowLight = false;
  bool displayTogglePressed = false;
  bool modeTogglePressed = false;
};

void hardwareInit();
HardwareInput hardwarePoll();
void hardwareDisplayLines(const char *line1, const char *line2, const char *line3);
void hardwareDisplayBlank();
void hardwareSetServoAngles(ServoAngles angles);
void hardwareSetServoSleepPose();
void hardwareSetServoNeutral();
