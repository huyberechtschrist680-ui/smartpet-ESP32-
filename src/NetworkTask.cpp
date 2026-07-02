#include "NetworkTask.h"

#include "app_config.h"
#include "pet_state.h"
#include "weather_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

namespace
{
  constexpr uint32_t kWifiRetryIntervalMs = 15000;
  constexpr uint32_t kInitialSiteRetryMs = 2000;
  constexpr uint32_t kWeatherHttpTimeoutMs = 1500;
  constexpr uint32_t kNetworkIdleDelayMs = 100;
  constexpr uint32_t kNetworkAfterWorkDelayMs = 50;
  constexpr uint32_t kNetworkTaskStackWords = 16384;
  constexpr UBaseType_t kNetworkTaskPriority = 1;
  constexpr BaseType_t kNetworkTaskCore = 0;
  constexpr size_t kCommandQueueLength = 2;
  constexpr size_t kAckQueueLength = 2;
  constexpr size_t kStatusQueueLength = 1;

  constexpr const char *kWeatherApiUrl =
      "https://api.open-meteo.com/v1/forecast?latitude=39.9042&longitude=116.4074&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=Asia%2FShanghai";

  struct NetworkAckMessage
  {
    char id[48];
    char command[80];
    char result[24];
  };

  TaskHandle_t gTaskHandle = nullptr;
  QueueHandle_t gCommandQueue = nullptr;
  QueueHandle_t gAckQueue = nullptr;
  QueueHandle_t gStatusQueue = nullptr;

  volatile bool gWebsiteModeEnabled = false;
  volatile bool gWifiConnected = false;

  bool gWifiStarted = false;
  uint32_t gWifiStartAllowedMs = 0;
  uint32_t gNextWifiRetryMs = 0;
  uint32_t gNextWeatherFetchMs = 0;
  uint32_t gNextCommandPollMs = 0;
  uint32_t gSiteRetryDelayMs = kInitialSiteRetryMs;

  bool gHasPendingAck = false;
  NetworkAckMessage gPendingAck;
  uint32_t gNextAckMs = 0;

  bool gHasPendingStatus = false;
  NetworkPetStatus gPendingStatus;
  uint32_t gNextStatusRetryMs = 0;

  bool timeReached(uint32_t nowMs, uint32_t targetMs)
  {
    return static_cast<int32_t>(nowMs - targetMs) >= 0;
  }

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

  void printNetworkLine(const char *message)
  {
    Serial.print('[');
    Serial.print(millis());
    Serial.print(F(" ms] "));
    Serial.println(message);
  }

  void resetSiteRetry()
  {
    gSiteRetryDelayMs = kInitialSiteRetryMs;
  }

  uint32_t nextSiteRetryDelay()
  {
    const uint32_t delayMs = gSiteRetryDelayMs;
    gSiteRetryDelayMs = min(gSiteRetryDelayMs * 2, kSmartPetRetryMaxMs);
    return delayMs;
  }

  bool wifiReady()
  {
    const bool connected = WiFi.status() == WL_CONNECTED;
    gWifiConnected = connected;
    return connected;
  }

  void startWifi(uint32_t nowMs)
  {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    esp_wifi_set_max_tx_power(static_cast<int8_t>(kWifiTxPowerQdbm));
    WiFi.begin(kWifiSsid, kWifiPassword);

    gWifiStarted = true;
    gNextWifiRetryMs = nowMs + kWifiRetryIntervalMs;
    weatherSetNetworkStatus("WIFI CONNECTING");
    printNetworkLine("WiFi connecting");
  }

  void ensureWifi(uint32_t nowMs)
  {
    if (!timeReached(nowMs, gWifiStartAllowedMs))
    {
      weatherSetNetworkStatus("WIFI WAIT");
      return;
    }

    if (wifiReady())
    {
      return;
    }

    weatherSetNetworkStatus("WIFI CONNECTING");
    if (!gWifiStarted || timeReached(nowMs, gNextWifiRetryMs))
    {
      startWifi(nowMs);
    }
  }

  String siteEndpoint(const char *path)
  {
    String url = kSmartPetApiBaseUrl;
    url += path;
    return url;
  }

  String authHeaderValue()
  {
    String value = "Bearer ";
    value += kSmartPetApiToken;
    return value;
  }

  void addAuthHeader(HTTPClient &http)
  {
    if (strlen(kSmartPetApiToken) > 0)
    {
      http.addHeader("Authorization", authHeaderValue());
    }
  }

  bool beginSecureHttp(HTTPClient &http,
                       WiFiClientSecure &client,
                       const String &url,
                       uint32_t timeoutMs)
  {
    client.setInsecure();
    http.setTimeout(timeoutMs);
    return http.begin(client, url);
  }

  bool beginSiteHttp(HTTPClient &http, WiFiClientSecure &client, const String &url)
  {
    if (!beginSecureHttp(http, client, url, kSmartPetHttpTimeoutMs))
    {
      return false;
    }

    addAuthHeader(http);
    return true;
  }

  bool postSiteJson(const String &url, const String &payload)
  {
    if (!wifiReady())
    {
      return false;
    }

    WiFiClientSecure client;
    HTTPClient http;
    if (!beginSiteHttp(http, client, url))
    {
      return false;
    }

    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(payload);
    const String response = http.getString();
    http.end();

    if (code != HTTP_CODE_OK)
    {
      return false;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, response);
    if (error)
    {
      return false;
    }

    return doc["ok"] | false;
  }

  const char *powerText(PowerState power)
  {
    return power == PowerState::Sleeping ? "SLEEP" : "NORMAL";
  }

  const char *foodText(FoodState food)
  {
    return food == FoodState::Full ? "FULL" : "HUNGRY";
  }

  bool loadPendingAck()
  {
    if (gHasPendingAck || gAckQueue == nullptr)
    {
      return gHasPendingAck;
    }

    if (xQueueReceive(gAckQueue, &gPendingAck, 0) != pdTRUE)
    {
      return false;
    }

    gHasPendingAck = true;
    gNextAckMs = millis();
    return true;
  }

  bool sendAckNow(uint32_t nowMs)
  {
    JsonDocument doc;
    doc["device"] = kSmartPetDefaultDevice;
    doc["id"] = gPendingAck.id;
    doc["command"] = gPendingAck.command;
    doc["result"] = gPendingAck.result;

    String payload;
    serializeJson(doc, payload);

    const bool ok = postSiteJson(siteEndpoint("/ack"), payload);
    if (ok)
    {
      gHasPendingAck = false;
      resetSiteRetry();
    }
    else
    {
      gNextAckMs = nowMs + nextSiteRetryDelay();
    }

    printNetworkLine(ok ? "Site ack sent" : "Site ack failed");
    return ok;
  }

  bool processSiteAck(uint32_t nowMs)
  {
    if (!gWebsiteModeEnabled || !loadPendingAck())
    {
      return false;
    }

    if (!timeReached(nowMs, gNextAckMs))
    {
      return false;
    }

    sendAckNow(nowMs);
    return true;
  }

  bool siteAckPending()
  {
    if (gHasPendingAck)
    {
      return true;
    }

    if (gAckQueue == nullptr)
    {
      return false;
    }

    return uxQueueMessagesWaiting(gAckQueue) > 0;
  }

  bool commandQueueHasRoom()
  {
    return gCommandQueue != nullptr && uxQueueSpacesAvailable(gCommandQueue) > 0;
  }

  bool fetchSiteCommand(uint32_t nowMs)
  {
    if (!gWebsiteModeEnabled || siteAckPending() || !commandQueueHasRoom())
    {
      return false;
    }

    if (!timeReached(nowMs, gNextCommandPollMs))
    {
      return false;
    }

    if (!wifiReady())
    {
      gNextCommandPollMs = nowMs + kSmartPetCommandPollIntervalMs;
      return false;
    }

    String url = siteEndpoint("/command?device=");
    url += kSmartPetDefaultDevice;

    WiFiClientSecure client;
    HTTPClient http;
    if (!beginSiteHttp(http, client, url))
    {
      gNextCommandPollMs = nowMs + nextSiteRetryDelay();
      return true;
    }

    const int code = http.GET();
    const String response = http.getString();
    http.end();

    if (code != HTTP_CODE_OK)
    {
      gNextCommandPollMs = nowMs + nextSiteRetryDelay();
      return true;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, response);
    if (error || !(doc["ok"] | false))
    {
      gNextCommandPollMs = nowMs + nextSiteRetryDelay();
      return true;
    }

    const char *command = doc["command"] | "";
    resetSiteRetry();
    gNextCommandPollMs = nowMs + kSmartPetCommandPollIntervalMs;
    if (command[0] == '\0')
    {
      return true;
    }

    NetworkRemoteCommand message;
    copyText(message.id, sizeof(message.id), doc["id"] | "");
    copyText(message.command, sizeof(message.command), command);
    if (xQueueSend(gCommandQueue, &message, 0) == pdTRUE)
    {
      printNetworkLine("Site command queued");
    }
    else
    {
      printNetworkLine("Site command queue full");
    }
    return true;
  }

  void drainLatestStatus()
  {
    if (gStatusQueue == nullptr)
    {
      return;
    }

    NetworkPetStatus status;
    while (xQueueReceive(gStatusQueue, &status, 0) == pdTRUE)
    {
      gPendingStatus = status;
      gHasPendingStatus = true;
    }
  }

  bool sendStatusNow(uint32_t nowMs)
  {
    JsonDocument doc;
    doc["device"] = kSmartPetDefaultDevice;
    doc["mode"] = "website";
    doc["power"] = powerText(gPendingStatus.power);
    doc["emotion"] = gPendingStatus.emotion;
    doc["food"] = foodText(gPendingStatus.food);
    doc["remain"] = gPendingStatus.remainSeconds;
    doc["motion"] = motionName(gPendingStatus.motion);
    doc["uptime_ms"] = gPendingStatus.uptimeMs;

    String payload;
    serializeJson(doc, payload);

    const bool ok = postSiteJson(siteEndpoint("/status"), payload);
    if (ok)
    {
      gHasPendingStatus = false;
      resetSiteRetry();
    }
    else
    {
      gNextStatusRetryMs = nowMs + nextSiteRetryDelay();
    }

    printNetworkLine(ok ? "Site status sent" : "Site status failed");
    return ok;
  }

  bool processSiteStatus(uint32_t nowMs)
  {
    if (!gWebsiteModeEnabled || siteAckPending())
    {
      return false;
    }

    drainLatestStatus();
    if (!gHasPendingStatus || !timeReached(nowMs, gNextStatusRetryMs))
    {
      return false;
    }

    sendStatusNow(nowMs);
    return true;
  }

  const char *weatherCodeName(int code)
  {
    if (code == 0)
    {
      return "CLEAR";
    }
    if (code == 1 || code == 2)
    {
      return "PARTLY";
    }
    if (code == 3)
    {
      return "CLOUD";
    }
    if (code == 45 || code == 48)
    {
      return "FOG";
    }
    if (code >= 51 && code <= 57)
    {
      return "DRIZZLE";
    }
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))
    {
      return "RAIN";
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)
    {
      return "SNOW";
    }
    if (code >= 95 && code <= 99)
    {
      return "STORM";
    }
    return "UNKNOWN";
  }

  bool fetchWeather(uint32_t nowMs)
  {
    if (!timeReached(nowMs, gNextWeatherFetchMs))
    {
      return false;
    }

    if (!wifiReady())
    {
      return false;
    }

    weatherSetNetworkStatus("FETCHING");

    WiFiClientSecure client;
    HTTPClient http;
    if (!beginSecureHttp(http, client, kWeatherApiUrl, kWeatherHttpTimeoutMs))
    {
      weatherSetNetworkStatus("HTTP BEGIN FAIL");
      gNextWeatherFetchMs = nowMs + kWeatherRetryIntervalMs;
      return true;
    }

    const int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
      char status[32];
      snprintf(status, sizeof(status), "HTTP %d", httpCode);
      weatherSetNetworkStatus(status);
      http.end();
      gNextWeatherFetchMs = nowMs + kWeatherRetryIntervalMs;
      return true;
    }

    const String payload = http.getString();
    http.end();

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
      weatherSetNetworkStatus("JSON PARSE FAIL");
      gNextWeatherFetchMs = nowMs + kWeatherRetryIntervalMs;
      return true;
    }

    JsonObject current = doc["current"];
    if (current.isNull())
    {
      weatherSetNetworkStatus("NO CURRENT DATA");
      gNextWeatherFetchMs = nowMs + kWeatherRetryIntervalMs;
      return true;
    }

    const float temperatureC = current["temperature_2m"] | 0.0f;
    const int humidityPercent = current["relative_humidity_2m"] | 0;
    const float windKmh = current["wind_speed_10m"] | 0.0f;
    const int weatherCode = current["weather_code"] | -1;

    weatherUpdateFromNetwork(temperatureC,
                             humidityPercent,
                             windKmh,
                             weatherCodeName(weatherCode),
                             nowMs);
    gNextWeatherFetchMs = nowMs + kWeatherFetchIntervalMs;
    return true;
  }

  void networkTaskLoop(void *parameter)
  {
    (void)parameter;

    for (;;)
    {
      const uint32_t nowMs = millis();
      bool didWork = false;

      ensureWifi(nowMs);

      if (wifiReady())
      {
        if (processSiteAck(nowMs))
        {
          didWork = true;
        }
        else if (fetchSiteCommand(nowMs))
        {
          didWork = true;
        }
        else if (processSiteStatus(nowMs))
        {
          didWork = true;
        }
        else if (fetchWeather(nowMs))
        {
          didWork = true;
        }
      }

      vTaskDelay(pdMS_TO_TICKS(didWork ? kNetworkAfterWorkDelayMs : kNetworkIdleDelayMs));
    }
  }

}  // namespace

void networkTaskInit()
{
  if (gTaskHandle != nullptr)
  {
    return;
  }

  gCommandQueue = xQueueCreate(kCommandQueueLength, sizeof(NetworkRemoteCommand));
  gAckQueue = xQueueCreate(kAckQueueLength, sizeof(NetworkAckMessage));
  gStatusQueue = xQueueCreate(kStatusQueueLength, sizeof(NetworkPetStatus));
  if (gCommandQueue == nullptr || gAckQueue == nullptr || gStatusQueue == nullptr)
  {
    printNetworkLine("Network queues init failed");
    return;
  }

  gWifiStartAllowedMs = millis() + kWifiStartDelayMs;
  gNextWeatherFetchMs = 0;
  gNextCommandPollMs = 0;
  gNextStatusRetryMs = 0;
  resetSiteRetry();

  const BaseType_t created = xTaskCreatePinnedToCore(networkTaskLoop,
                                                    "NetworkTask",
                                                    kNetworkTaskStackWords,
                                                    nullptr,
                                                    kNetworkTaskPriority,
                                                    &gTaskHandle,
                                                    kNetworkTaskCore);
  if (created != pdPASS)
  {
    gTaskHandle = nullptr;
    printNetworkLine("Network task init failed");
    return;
  }

  printNetworkLine("Network task started");
}

void networkTaskSetWebsiteMode(bool enabled)
{
  if (enabled == gWebsiteModeEnabled)
  {
    return;
  }

  gWebsiteModeEnabled = enabled;
  gNextCommandPollMs = millis();
  gNextStatusRetryMs = 0;

  if (!enabled)
  {
    gHasPendingAck = false;
    gHasPendingStatus = false;
    if (gCommandQueue != nullptr)
    {
      xQueueReset(gCommandQueue);
    }
    if (gAckQueue != nullptr)
    {
      xQueueReset(gAckQueue);
    }
    if (gStatusQueue != nullptr)
    {
      xQueueReset(gStatusQueue);
    }
  }

  printNetworkLine(enabled ? "Website network enabled" : "Website network disabled");
}

bool networkTaskWebsiteModeEnabled()
{
  return gWebsiteModeEnabled;
}

bool networkTaskTakeCommand(NetworkRemoteCommand &out)
{
  if (!gWebsiteModeEnabled || gCommandQueue == nullptr)
  {
    return false;
  }

  return xQueueReceive(gCommandQueue, &out, 0) == pdTRUE;
}

void networkTaskSubmitAck(const char *id, const char *command, const char *result)
{
  if (!gWebsiteModeEnabled || gAckQueue == nullptr || result == nullptr)
  {
    return;
  }

  NetworkAckMessage message;
  copyText(message.id, sizeof(message.id), id);
  copyText(message.command, sizeof(message.command), command);
  copyText(message.result, sizeof(message.result), result);

  if (xQueueSend(gAckQueue, &message, 0) != pdTRUE)
  {
    printNetworkLine("Site ack queue full");
  }
}

void networkTaskSubmitStatus(const NetworkPetStatus &status)
{
  if (!gWebsiteModeEnabled || gStatusQueue == nullptr)
  {
    return;
  }

  xQueueOverwrite(gStatusQueue, &status);
}

bool networkTaskIsWifiConnected()
{
  return gWifiConnected;
}
