#pragma once

#include <Arduino.h>

#ifndef APP_ENV_NAME
#define APP_ENV_NAME "unknown"
#endif

#ifndef PET_PIN_OLED_SDA
#define PET_PIN_OLED_SDA 8
#endif

#ifndef PET_PIN_OLED_SCL
#define PET_PIN_OLED_SCL 9
#endif

#ifndef PET_PIN_SERVO_LEFT
#define PET_PIN_SERVO_LEFT 15
#endif

#ifndef PET_PIN_SERVO_RIGHT
#define PET_PIN_SERVO_RIGHT 16
#endif

#ifndef PET_PIN_BUTTON_HEAD
#define PET_PIN_BUTTON_HEAD 17
#endif

#ifndef PET_PIN_BUTTON_DISPLAY
#define PET_PIN_BUTTON_DISPLAY 10
#endif

#ifndef PET_PIN_ENCODER_A
#define PET_PIN_ENCODER_A 18
#endif

#ifndef PET_PIN_ENCODER_B
#define PET_PIN_ENCODER_B 21
#endif

#ifndef PET_PIN_LIGHT_D0
#define PET_PIN_LIGHT_D0 4
#endif

#ifndef PET_PIN_BUTTON_MODE
#define PET_PIN_BUTTON_MODE 47
#endif

#ifndef PET_SERVO_MIN_US
#define PET_SERVO_MIN_US 500
#endif

#ifndef PET_SERVO_MAX_US
#define PET_SERVO_MAX_US 2500
#endif

#ifndef PET_SERVO_MIN_DEG
#define PET_SERVO_MIN_DEG 0
#endif

#ifndef PET_SERVO_MAX_DEG
#define PET_SERVO_MAX_DEG 180
#endif

#ifndef PET_SERVO_NEUTRAL_DEG
#define PET_SERVO_NEUTRAL_DEG 90
#endif

#ifndef PET_SERVO_SLEEP_OFFSET_DEG
#define PET_SERVO_SLEEP_OFFSET_DEG 80
#endif

#ifndef PET_ENCODER_TICKS_PER_REVOLUTION
#define PET_ENCODER_TICKS_PER_REVOLUTION 80
#endif

#ifndef PET_ENCODER_DETENTS_PER_REVOLUTION
#define PET_ENCODER_DETENTS_PER_REVOLUTION 20
#endif

#ifndef PET_ENCODER_CONTINUOUS_TIMEOUT_MS
#define PET_ENCODER_CONTINUOUS_TIMEOUT_MS 1000
#endif

#ifndef PET_ENCODER_CW_SIGN
#define PET_ENCODER_CW_SIGN 1
#endif

#ifndef PET_WIFI_SSID
#define PET_WIFI_SSID "Magic7"
#endif

#ifndef PET_WIFI_PASSWORD
#define PET_WIFI_PASSWORD "ljp070616"
#endif

#ifndef PET_WEATHER_FETCH_INTERVAL_MS
#define PET_WEATHER_FETCH_INTERVAL_MS 600000UL
#endif

#ifndef PET_WEATHER_RETRY_INTERVAL_MS
#define PET_WEATHER_RETRY_INTERVAL_MS 60000UL
#endif

#ifndef PET_WIFI_START_DELAY_MS
#define PET_WIFI_START_DELAY_MS 10000UL
#endif

#ifndef PET_WIFI_TX_POWER_QDBM
#define PET_WIFI_TX_POWER_QDBM 44
#endif

#ifndef PET_BLE_START_DELAY_MS
#define PET_BLE_START_DELAY_MS 2000UL
#endif

#ifndef PET_MODE_NOTICE_MS
#define PET_MODE_NOTICE_MS 1000UL
#endif

#ifndef SMARTPET_API_BASE_URL
#define SMARTPET_API_BASE_URL "https://pet.liujiapeng.xyz/api/smartpet"
#endif

#ifndef SMARTPET_API_TOKEN
#define SMARTPET_API_TOKEN "yichuansuijishu"
#endif

#ifndef SMARTPET_DEFAULT_DEVICE
#define SMARTPET_DEFAULT_DEVICE "smartpet-01"
#endif

#ifndef SMARTPET_COMMAND_POLL_INTERVAL_MS
#define SMARTPET_COMMAND_POLL_INTERVAL_MS 2000UL
#endif

#ifndef SMARTPET_STATUS_INTERVAL_MS
#define SMARTPET_STATUS_INTERVAL_MS 5000UL
#endif

#ifndef SMARTPET_HTTP_TIMEOUT_MS
#define SMARTPET_HTTP_TIMEOUT_MS 2000UL
#endif

#ifndef SMARTPET_RETRY_MAX_MS
#define SMARTPET_RETRY_MAX_MS 30000UL
#endif

constexpr const char *kAppName = "ESP32-S3 Smart Desk Pet";
constexpr uint32_t kSerialBaud = 115200;
constexpr int kEmotionInitial = 5;
constexpr int kEmotionMin = 1;
constexpr int kEmotionMax = 10;

constexpr uint32_t kDebugStatusIntervalMs = 5000;
constexpr uint32_t kEmotionDecayIntervalMs = 30000;
constexpr uint32_t kFullDurationMs = 120000;
constexpr uint32_t kHeadTouchCooldownMs = 10000;
constexpr uint32_t kDarkToSleepMs = 5000;
constexpr uint32_t kAutoMotionCheckIntervalMs = 1000;

constexpr int PIN_OLED_SDA = PET_PIN_OLED_SDA;             // H4-12, GPIO8
constexpr int PIN_OLED_SCL = PET_PIN_OLED_SCL;             // H4-15, GPIO9
constexpr int PIN_SERVO_LEFT = PET_PIN_SERVO_LEFT;         // H4-8, GPIO15
constexpr int PIN_SERVO_RIGHT = PET_PIN_SERVO_RIGHT;       // H4-9, GPIO16
constexpr int PIN_BUTTON_HEAD = PET_PIN_BUTTON_HEAD;       // H4-10, GPIO17
constexpr int PIN_BUTTON_DISPLAY = PET_PIN_BUTTON_DISPLAY; // H4-16, GPIO10
constexpr int PIN_ENCODER_A = PET_PIN_ENCODER_A;           // H4-11, GPIO18
constexpr int PIN_ENCODER_B = PET_PIN_ENCODER_B;           // H5-18, GPIO21
constexpr int PIN_LIGHT_D0 = PET_PIN_LIGHT_D0;             // H4-4, GPIO4
constexpr int PIN_BUTTON_MODE = PET_PIN_BUTTON_MODE;       // GPIO47, mode switch to GND

constexpr int kOledWidth = 128;
constexpr int kOledHeight = 64;
constexpr uint8_t kOledAddress = 0x3C;
constexpr int kOledResetPin = -1;

constexpr uint32_t kInputDebounceMs = 25;
constexpr bool kLightD0HighMeansLowLight = true;

constexpr int kServoMinPulseUs = PET_SERVO_MIN_US;
constexpr int kServoMaxPulseUs = PET_SERVO_MAX_US;
constexpr int kServoMinDeg = PET_SERVO_MIN_DEG;
constexpr int kServoMaxDeg = PET_SERVO_MAX_DEG;
constexpr int kServoNeutralDeg = PET_SERVO_NEUTRAL_DEG;
constexpr int kServoSleepOffsetDeg = PET_SERVO_SLEEP_OFFSET_DEG;

constexpr int kEncoderTicksPerRevolution = PET_ENCODER_TICKS_PER_REVOLUTION;
constexpr int kEncoderDetentsPerRevolution = PET_ENCODER_DETENTS_PER_REVOLUTION;
constexpr uint32_t kEncoderContinuousTimeoutMs = PET_ENCODER_CONTINUOUS_TIMEOUT_MS;
constexpr int kEncoderCwSign = PET_ENCODER_CW_SIGN;

constexpr const char *kWifiSsid = PET_WIFI_SSID;
constexpr const char *kWifiPassword = PET_WIFI_PASSWORD;
constexpr const char *kWeatherCityName = "Beijing";
constexpr uint32_t kWeatherFetchIntervalMs = PET_WEATHER_FETCH_INTERVAL_MS;
constexpr uint32_t kWeatherRetryIntervalMs = PET_WEATHER_RETRY_INTERVAL_MS;
constexpr uint32_t kWifiStartDelayMs = PET_WIFI_START_DELAY_MS;
constexpr int kWifiTxPowerQdbm = PET_WIFI_TX_POWER_QDBM;
constexpr uint32_t kBleStartDelayMs = PET_BLE_START_DELAY_MS;
constexpr uint32_t kModeNoticeMs = PET_MODE_NOTICE_MS;
constexpr const char *kSmartPetApiBaseUrl = SMARTPET_API_BASE_URL;
constexpr const char *kSmartPetApiToken = SMARTPET_API_TOKEN;
constexpr const char *kSmartPetDefaultDevice = SMARTPET_DEFAULT_DEVICE;
constexpr uint32_t kSmartPetCommandPollIntervalMs = SMARTPET_COMMAND_POLL_INTERVAL_MS;
constexpr uint32_t kSmartPetStatusIntervalMs = SMARTPET_STATUS_INTERVAL_MS;
constexpr uint32_t kSmartPetHttpTimeoutMs = SMARTPET_HTTP_TIMEOUT_MS;
constexpr uint32_t kSmartPetRetryMaxMs = SMARTPET_RETRY_MAX_MS;
