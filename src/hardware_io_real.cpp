#include "hardware_io.h"

#include "app_config.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <string.h>

namespace
{
  constexpr int kServoPwmHz = 50;
  constexpr size_t kCachedLineLength = 32;

  struct DebouncedButton
  {
    bool lastRawPressed = false;
    bool stablePressed = false;
    uint32_t rawChangedMs = 0;
  };

  Servo leftServo;
  Servo rightServo;
  bool leftServoAttached = false;
  bool rightServoAttached = false;

  Adafruit_SSD1306 oled(kOledWidth, kOledHeight, &Wire, kOledResetPin);
  bool oledReady = false;
  bool oledBlank = true;
  char cachedLine1[kCachedLineLength] = "";
  char cachedLine2[kCachedLineLength] = "";
  char cachedLine3[kCachedLineLength] = "";

  DebouncedButton headButton;
  DebouncedButton displayButton;
  DebouncedButton modeButton;

  volatile int16_t encoderDelta = 0;
  volatile uint8_t encoderLastState = 0;
  int encoderClockwiseTicks = 0;
  uint32_t lastEncoderMoveMs = 0;

  bool pinEnabled(int pin)
  {
    return pin >= 0;
  }

  bool readActiveLowPin(int pin)
  {
    return pinEnabled(pin) && digitalRead(pin) == LOW;
  }

  void initActiveLowButton(int pin, DebouncedButton &button)
  {
    if (!pinEnabled(pin))
    {
      return;
    }

    pinMode(pin, INPUT_PULLUP);
    button.lastRawPressed = readActiveLowPin(pin);
    button.stablePressed = button.lastRawPressed;
    button.rawChangedMs = millis();
  }

  bool readButtonPressedEvent(int pin, DebouncedButton &button, uint32_t nowMs)
  {
    if (!pinEnabled(pin))
    {
      return false;
    }

    const bool rawPressed = readActiveLowPin(pin);
    if (rawPressed != button.lastRawPressed)
    {
      button.lastRawPressed = rawPressed;
      button.rawChangedMs = nowMs;
    }

    if (nowMs - button.rawChangedMs < kInputDebounceMs || rawPressed == button.stablePressed)
    {
      return false;
    }

    button.stablePressed = rawPressed;
    return button.stablePressed;
  }

  uint8_t readEncoderState()
  {
    const uint8_t a = digitalRead(PIN_ENCODER_A) == HIGH ? 1 : 0;
    const uint8_t b = digitalRead(PIN_ENCODER_B) == HIGH ? 1 : 0;
    return static_cast<uint8_t>((a << 1) | b);
  }

  void IRAM_ATTR handleEncoderChange()
  {
    static constexpr int8_t kTransitionTable[16] =
    {
      0, -1, 1, 0,
      1, 0, 0, -1,
      -1, 0, 0, 1,
      0, 1, -1, 0,
    };

    const uint8_t currentState = readEncoderState();
    const uint8_t transition = static_cast<uint8_t>((encoderLastState << 2) | currentState);
    encoderDelta += kTransitionTable[transition];
    encoderLastState = currentState;
  }

  int toPhysicalServoAngle(int relativeAngle)
  {
    return constrain(kServoNeutralDeg + relativeAngle, kServoMinDeg, kServoMaxDeg);
  }

  void attachServo(Servo &servo, int pin, bool &attached)
  {
    if (!pinEnabled(pin))
    {
      return;
    }

    servo.setPeriodHertz(kServoPwmHz);
    servo.attach(pin, kServoMinPulseUs, kServoMaxPulseUs);
    attached = true;
  }

  void writeServo(Servo &servo, bool attached, int physicalAngle)
  {
    if (!attached)
    {
      return;
    }

    servo.write(constrain(physicalAngle, kServoMinDeg, kServoMaxDeg));
  }

  bool readLowLight()
  {
    if (!pinEnabled(PIN_LIGHT_D0))
    {
      return false;
    }

    const bool high = digitalRead(PIN_LIGHT_D0) == HIGH;
    return kLightD0HighMeansLowLight ? high : !high;
  }

  void copyLine(char *target, const char *source)
  {
    if (source == nullptr)
    {
      target[0] = '\0';
      return;
    }

    strncpy(target, source, kCachedLineLength - 1);
    target[kCachedLineLength - 1] = '\0';
  }

  bool displayChanged(const char *line1, const char *line2, const char *line3)
  {
    return oledBlank ||
    strncmp(cachedLine1, line1 != nullptr ? line1 : "", kCachedLineLength) != 0 ||
    strncmp(cachedLine2, line2 != nullptr ? line2 : "", kCachedLineLength) != 0 ||
    strncmp(cachedLine3, line3 != nullptr ? line3 : "", kCachedLineLength) != 0;
  }

  void drawDisplayLines(const char *line1, const char *line2, const char *line3)
  {
    if (!oledReady || !displayChanged(line1, line2, line3))
    {
      return;
    }

    copyLine(cachedLine1, line1);
    copyLine(cachedLine2, line2);
    copyLine(cachedLine3, line3);

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println(cachedLine1);
    oled.setCursor(0, 22);
    oled.println(cachedLine2);
    oled.setCursor(0, 44);
    oled.println(cachedLine3);
    oled.display();
    oledBlank = false;
  }

  void initOled()
  {
    if (!pinEnabled(PIN_OLED_SDA) || !pinEnabled(PIN_OLED_SCL))
    {
      return;
    }

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.setClock(400000);
    oledReady = oled.begin(SSD1306_SWITCHCAPVCC, kOledAddress);
    if (oledReady)
    {
      oled.clearDisplay();
      oled.display();
      oledBlank = true;
    }
  }

  void initEncoder()
  {
    if (!pinEnabled(PIN_ENCODER_A) || !pinEnabled(PIN_ENCODER_B))
    {
      return;
    }

    pinMode(PIN_ENCODER_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B, INPUT_PULLUP);
    encoderLastState = readEncoderState();
    encoderDelta = 0;
    encoderClockwiseTicks = 0;
    lastEncoderMoveMs = millis();
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), handleEncoderChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), handleEncoderChange, CHANGE);
  }

  int16_t consumeEncoderDelta()
  {
    noInterrupts();
    const int16_t delta = encoderDelta;
    encoderDelta = 0;
    interrupts();
    return delta;
  }

  bool readEncoderFeedGesture(uint32_t nowMs)
  {
    const int16_t delta = consumeEncoderDelta();

    if (delta == 0)
    {
      if (encoderClockwiseTicks > 0 && nowMs - lastEncoderMoveMs > kEncoderContinuousTimeoutMs)
      {
        encoderClockwiseTicks = 0;
      }
      return false;
    }

    lastEncoderMoveMs = nowMs;
    const int signedDelta = delta * kEncoderCwSign;
    if (signedDelta > 0)
    {
      encoderClockwiseTicks += signedDelta;
    }
    else
    {
      encoderClockwiseTicks = 0;
      return false;
    }

    if (encoderClockwiseTicks < kEncoderTicksPerRevolution)
    {
      return false;
    }

    encoderClockwiseTicks = 0;
    return true;
  }

}  // namespace

void hardwareInit()
{
  initActiveLowButton(PIN_BUTTON_HEAD, headButton);
  initActiveLowButton(PIN_BUTTON_DISPLAY, displayButton);
  initActiveLowButton(PIN_BUTTON_MODE, modeButton);

  if (pinEnabled(PIN_LIGHT_D0))
  {
    pinMode(PIN_LIGHT_D0, INPUT);
  }

  initEncoder();
  initOled();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  attachServo(leftServo, PIN_SERVO_LEFT, leftServoAttached);
  attachServo(rightServo, PIN_SERVO_RIGHT, rightServoAttached);
  hardwareSetServoNeutral();
}

HardwareInput hardwarePoll()
{
  const uint32_t nowMs = millis();

  HardwareInput input;
  input.headButtonPressed = readButtonPressedEvent(PIN_BUTTON_HEAD, headButton, nowMs);
  input.displayTogglePressed = readButtonPressedEvent(PIN_BUTTON_DISPLAY, displayButton, nowMs);
  input.modeTogglePressed = readButtonPressedEvent(PIN_BUTTON_MODE, modeButton, nowMs);
  input.feedGesture = readEncoderFeedGesture(nowMs);
  input.lowLight = readLowLight();
  return input;
}

void hardwareDisplayLines(const char *line1, const char *line2, const char *line3)
{
  drawDisplayLines(line1, line2, line3);
}

void hardwareDisplayBlank()
{
  if (!oledReady || oledBlank)
  {
    return;
  }

  oled.clearDisplay();
  oled.display();
  oledBlank = true;
  cachedLine1[0] = '\0';
  cachedLine2[0] = '\0';
  cachedLine3[0] = '\0';
}

void hardwareSetServoAngles(ServoAngles angles)
{
  writeServo(leftServo, leftServoAttached, toPhysicalServoAngle(angles.left));
  writeServo(rightServo, rightServoAttached, toPhysicalServoAngle(angles.right));
}

void hardwareSetServoSleepPose()
{
  const int sleepAngle = toPhysicalServoAngle(kServoSleepOffsetDeg);
  writeServo(leftServo, leftServoAttached, sleepAngle);
  writeServo(rightServo, rightServoAttached, sleepAngle);
}

void hardwareSetServoNeutral()
{
  writeServo(leftServo, leftServoAttached, kServoNeutralDeg);
  writeServo(rightServo, rightServoAttached, kServoNeutralDeg);
}
