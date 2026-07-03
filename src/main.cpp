#include <Arduino.h>

#include "BluetoothManager.h"
#include "WebsiteControl.h"
#include "app_config.h"
#include "command_parser.h"
#include "debug_logger.h"
#include "display_model.h"
#include "extension_manager.h"
#include "hardware_io.h"
#include "motion_plan.h"
#include "pet_state.h"

namespace
{
  PetState pet;
  ControlMode controlMode = ControlMode::BlePhone;
  uint32_t modeNoticeUntilMs = 0;

  bool timeReached(uint32_t nowMs, uint32_t targetMs)
  {
    return static_cast<int32_t>(nowMs - targetMs) >= 0;
  }

  const char *powerLabel(PowerState power)
  {
    return power == PowerState::Sleeping ? "SLEEP" : "NORMAL";
  }

  const char *foodLabel(FoodState food)
  {
    return food == FoodState::Full ? "FULL" : "HUNGRY";
  }

  const char *controlModeLabel(ControlMode mode)
  {
    return mode == ControlMode::Website ? "WEBSITE + WIFI" : "BLE + WEATHER";
  }

  uint32_t fullRemainSeconds(const PetState &state)
  {
    if (state.food != FoodState::Full || state.activeMs >= state.fullEndActiveMs)
    {
      return 0;
    }
    return (state.fullEndActiveMs - state.activeMs) / 1000;
  }

  void notifyBleState(const PetState &state)
  {
    char message[72];
    snprintf(message,
             sizeof(message),
             "S,%s,%d,%s,%lu,%s",
             powerLabel(state.power),
             state.emotion,
             foodLabel(state.food),
             static_cast<unsigned long>(fullRemainSeconds(state)),
             motionName(state.motion));
    bluetoothNotify(message);
  }

  PetInput makePetInput(const HardwareInput &hardwareInput,
                        bool hasCommand,
                        const ParsedCommand &command)
  {
    PetInput input;
    input.headButtonPressed = hardwareInput.headButtonPressed;
    input.feedGesture = hardwareInput.feedGesture;
    input.lowLight = hardwareInput.lowLight;
    input.displayTogglePressed = hardwareInput.displayTogglePressed;
    input.hasCommand = hasCommand;
    input.command = command;
    return input;
  }

  void printModeLine(ControlMode mode)
  {
    Serial.print('[');
    Serial.print(millis());
    Serial.print(F(" ms] Control mode: "));
    Serial.println(controlModeLabel(mode));
  }

  void setControlMode(ControlMode mode, uint32_t nowMs)
  {
    if (mode == controlMode)
    {
      return;
    }

    controlMode = mode;
    modeNoticeUntilMs = nowMs + kModeNoticeMs;

    if (controlMode == ControlMode::Website)
    {
      bluetoothSetEnabled(false);
      websiteControlSetEnabled(true);
    }
    else
    {
      websiteControlSetEnabled(false);
      bluetoothSetEnabled(true);
    }

    printModeLine(controlMode);
  }

  void toggleControlMode(uint32_t nowMs)
  {
    if (controlMode == ControlMode::BlePhone)
    {
      setControlMode(ControlMode::Website, nowMs);
    }
    else
    {
      setControlMode(ControlMode::BlePhone, nowMs);
    }
  }

  bool modeNoticeActive(uint32_t nowMs)
  {
    return !timeReached(nowMs, modeNoticeUntilMs);
  }

  const char *handleRemoteCommand(String text,
                                  PetInput &input,
                                  bool &requestState,
                                  bool notifyBleErrors)
  {
    text.trim();
    if (text.length() == 0)
    {
      if (notifyBleErrors)
      {
        bluetoothNotify("E,ERR,BAD_COMMAND");
      }
      return "BAD_COMMAND";
    }

    if (text.equalsIgnoreCase("touch"))
    {
      if (pet.power == PowerState::Normal)
      {
        input.headButtonPressed = true;
        return "OK";
      }

      if (notifyBleErrors)
      {
        bluetoothNotify("E,IGNORED,SLEEPING");
      }
      return "IGNORED_SLEEPING";
    }

    if (text.equalsIgnoreCase("feed"))
    {
      if (pet.power == PowerState::Normal)
      {
        input.feedGesture = true;
        return "OK";
      }

      if (notifyBleErrors)
      {
        bluetoothNotify("E,IGNORED,SLEEPING");
      }
      return "IGNORED_SLEEPING";
    }

    if (text.equalsIgnoreCase("state"))
    {
      requestState = true;
      return "OK";
    }
    // 以上为远程独有命令，以下为将远程命令合并入串口命令处理。
    ParsedCommand remoteCommand; // 准备一个空命令容器
    if (!commandParseText(text.c_str(), remoteCommand))
    {
      if (notifyBleErrors)
      {
        bluetoothNotify("E,ERR,BAD_COMMAND");
      }
      return "BAD_COMMAND";
    }

    if (input.hasCommand) // 优先处理串口命令
    {
      if (notifyBleErrors)
      {
        bluetoothNotify("E,ERR,BUSY");
      }
      return "BUSY";
    }

    input.hasCommand = true;
    input.command = remoteCommand;
    return "OK";
  }

  void applyOutputs(const PetState &state, uint32_t nowMs)
  {
    if (modeNoticeActive(nowMs))
    {
      hardwareDisplayLines("CONTROL MODE", controlModeLabel(controlMode), "GPIO47 TO SWITCH");
    }
    else
    {
      const DisplayModel display = makeDisplayModel(state);
      if (display.blank)
      {
        hardwareDisplayBlank();
        hardwareSetServoSleepPose();
        return;
      }

      hardwareDisplayLines(display.line1, display.line2, display.line3);
    }

    if (state.power == PowerState::Sleeping)
    {
      hardwareSetServoSleepPose();
      return;
    }

    if (state.motion == MotionMode::Null)
    {
      hardwareSetServoNeutral();
      return;
    }

    const uint32_t elapsedMotionMs = state.activeMs - state.motionStartActiveMs;
    hardwareSetServoAngles(getServoAngles(state.motion, elapsedMotionMs));
  }

} // namespace

void setup()
{
  Serial.begin(kSerialBaud);

  hardwareInit();
  debugInit();
  petInit(pet, millis());
  extensionInit();
  websiteControlInit();
  bluetoothInit();
}

void loop()
{
  const uint32_t nowMs = millis();

  ParsedCommand command;
  const bool hasCommand = commandPoll(command);
  const HardwareInput hardwareInput = hardwarePoll();
  PetInput input = makePetInput(hardwareInput, hasCommand, command);
  bool requestBleState = false;
  bool requestWebsiteState = false;
  bool forceWebsiteState = false; // 三个状态请求标志
  String websiteAckId;
  String websiteAckCommandText; // 执行后回执
  const char *websiteAckResult = nullptr;

  if (hardwareInput.modeTogglePressed)
  {
    toggleControlMode(nowMs);
    forceWebsiteState = controlMode == ControlMode::Website;
  }

  if (controlMode == ControlMode::BlePhone)
  {
    updateBluetooth();

    String bleText;
    if (bluetoothTakeCommand(bleText))
    {
      handleRemoteCommand(bleText, input, requestBleState, true);
    }
  }
  else
  {
    websiteControlPoll(nowMs);

    WebsiteCommand websiteCommand;
    if (websiteControlTakeCommand(websiteCommand))
    {
      websiteAckId = websiteCommand.id;
      websiteAckCommandText = websiteCommand.command;
      websiteAckResult = handleRemoteCommand(websiteCommand.command, input, requestWebsiteState, false);
    }
  }

  extensionBeforePetUpdate(input, nowMs);

  PetEventQueue events;
  petUpdate(pet, input, nowMs, events);

  for (size_t index = 0; index < events.count; ++index)
  {
    debugLogEvent(nowMs, events.items[index], pet);
  }

  applyOutputs(pet, nowMs);
  debugLogStatusIfNeeded(nowMs, pet);
  extensionAfterPetUpdate(pet, events, nowMs);

  if (websiteAckResult != nullptr)
  {
    websiteControlAckCommand(websiteAckId, websiteAckCommandText, websiteAckResult);
  }

  if (controlMode == ControlMode::Website)
  {
    websiteControlNotifyStateIfNeeded(pet, events, nowMs, forceWebsiteState || requestWebsiteState);
  }

  if (requestBleState)
  {
    notifyBleState(pet);
  }
}