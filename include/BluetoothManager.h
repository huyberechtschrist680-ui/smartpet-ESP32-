#pragma once

#include <Arduino.h>

void bluetoothInit();
void updateBluetooth();
void bluetoothSetEnabled(bool enabled);
bool bluetoothIsEnabled();
bool bluetoothTakeCommand(String &out);
void bluetoothNotify(const String &message);
bool bluetoothIsConnected();