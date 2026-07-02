#pragma once

#include <Arduino.h>

#include "display_model.h"

void weatherInit();
void weatherPoll(uint32_t nowMs);
void weatherToggleDisplayPage();
bool weatherDisplayPageActive();
bool weatherFillDisplayModel(DisplayModel &model);
void weatherSetNetworkStatus(const char *status);
void weatherUpdateFromNetwork(float temperatureC,
                              int humidityPercent,
                              float windKmh,
                              const char *condition,
                              uint32_t nowMs);
