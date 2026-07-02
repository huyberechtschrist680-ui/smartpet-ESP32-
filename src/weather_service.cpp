#include "weather_service.h"

#include "app_config.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace
{
  constexpr size_t kWeatherTextLength = 16;
  constexpr size_t kWeatherStatusLength = 32;

  bool weatherPageActive = false;
  bool hasWeather = false;
  uint32_t lastWeatherFetchMs = 0;
  float cachedTemperatureC = 0.0f;
  int cachedHumidityPercent = 0;
  float cachedWindKmh = 0.0f;
  char conditionText[kWeatherTextLength] = "WAIT";
  char statusText[kWeatherStatusLength] = "WIFI START";
  portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;

  void copyText(char *target, size_t targetLength, const char *source)
  {
    if (targetLength == 0)
    {
      return;
    }

    if (source == nullptr)
    {
      target[0] = '\0';
      return;
    }

    strncpy(target, source, targetLength - 1);
    target[targetLength - 1] = '\0';
  }

}  // namespace

void weatherInit()
{
  portENTER_CRITICAL(&weatherMux);
  weatherPageActive = false;
  hasWeather = false;
  lastWeatherFetchMs = 0;
  cachedTemperatureC = 0.0f;
  cachedHumidityPercent = 0;
  cachedWindKmh = 0.0f;
  copyText(conditionText, sizeof(conditionText), "WAIT");
  copyText(statusText, sizeof(statusText), "WIFI WAIT");
  portEXIT_CRITICAL(&weatherMux);
}

void weatherPoll(uint32_t nowMs)
{
  (void)nowMs;
}

void weatherToggleDisplayPage()
{
  portENTER_CRITICAL(&weatherMux);
  weatherPageActive = !weatherPageActive;
  portEXIT_CRITICAL(&weatherMux);
}

bool weatherDisplayPageActive()
{
  portENTER_CRITICAL(&weatherMux);
  const bool active = weatherPageActive;
  portEXIT_CRITICAL(&weatherMux);
  return active;
}

bool weatherFillDisplayModel(DisplayModel &model)
{
  bool active = false;
  bool localHasWeather = false;
  uint32_t localLastWeatherFetchMs = 0;
  float temperatureC = 0.0f;
  int humidityPercent = 0;
  float windKmh = 0.0f;
  char condition[kWeatherTextLength];
  char status[kWeatherStatusLength];

  portENTER_CRITICAL(&weatherMux);
  active = weatherPageActive;
  localHasWeather = hasWeather;
  localLastWeatherFetchMs = lastWeatherFetchMs;
  temperatureC = cachedTemperatureC;
  humidityPercent = cachedHumidityPercent;
  windKmh = cachedWindKmh;
  copyText(condition, sizeof(condition), conditionText);
  copyText(status, sizeof(status), statusText);
  portEXIT_CRITICAL(&weatherMux);

  if (!active)
  {
    return false;
  }

  model.blank = false;
  if (localHasWeather)
  {
    snprintf(model.line1, kDisplayLineLength, "%s %s", kWeatherCityName, condition);
    snprintf(model.line2, kDisplayLineLength, "T %.1fC H %d%%", temperatureC, humidityPercent);
    snprintf(model.line3, kDisplayLineLength, "WIND %.1f km/h", windKmh);
    (void)localLastWeatherFetchMs;
    return true;
  }

  snprintf(model.line1, kDisplayLineLength, "WEATHER %s", kWeatherCityName);
  snprintf(model.line2, kDisplayLineLength, "%s", status);
  snprintf(model.line3, kDisplayLineLength, "WAIT API DATA");
  return true;
}

void weatherSetNetworkStatus(const char *status)
{
  portENTER_CRITICAL(&weatherMux);
  copyText(statusText, sizeof(statusText), status);
  portEXIT_CRITICAL(&weatherMux);
}

void weatherUpdateFromNetwork(float temperatureC,
                              int humidityPercent,
                              float windKmh,
                              const char *condition,
                              uint32_t nowMs)
{
  portENTER_CRITICAL(&weatherMux);
  cachedTemperatureC = temperatureC;
  cachedHumidityPercent = humidityPercent;
  cachedWindKmh = windKmh;
  lastWeatherFetchMs = nowMs;
  hasWeather = true;
  copyText(conditionText, sizeof(conditionText), condition);
  copyText(statusText, sizeof(statusText), "OK");
  portEXIT_CRITICAL(&weatherMux);
}
