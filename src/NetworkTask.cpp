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
  constexpr size_t kResponsePreviewLength = 80;

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
  volatile uint32_t gNextSyncMs = 0;

  bool gWifiStarted = false;
  uint32_t gWifiStartAllowedMs = 0;
  uint32_t gNextWifiRetryMs = 0;
  uint32_t gNextWeatherFetchMs = 0;
  uint32_t gSiteRetryDelayMs = kInitialSiteRetryMs;

  bool gHasPendingAck = false;
  NetworkAckMessage gPendingAck;

  bool gHasLatestStatus = false;
  NetworkPetStatus gLatestStatus;

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

  void printHttpIssue(const char *label, int code, const String &response)
  {
    Serial.print('[');
    Serial.print(millis());
    Serial.print(F(" ms] "));
    Serial.print(label);
    Serial.print(F(" HTTP "));
    Serial.print(code);
    if (response.length() > 0)
    {
      Serial.print(F(" body: "));
      Serial.println(response.substring(0, kResponsePreviewLength));
    }
    else
    {
      Serial.println();
    }
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

  const char *powerText(PowerState power)
  {
    return power == PowerState::Sleeping ? "SLEEP" : "NORMAL";
  }

  const char *foodText(FoodState food)
  {
    return food == FoodState::Full ? "FULL" : "HUNGRY";
  }

  void loadPendingAck()
  {
    if (gHasPendingAck || gAckQueue == nullptr)
    {
      return;
    }

    if (xQueueReceive(gAckQueue, &gPendingAck, 0) == pdTRUE)
    {
      gHasPendingAck = true;
    }
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
      gLatestStatus = status;
      gHasLatestStatus = true;
    }
  }

  NetworkPetStatus latestStatusOrDefault(uint32_t nowMs)
  {
    if (gHasLatestStatus)
    {
      return gLatestStatus;
    }

    NetworkPetStatus status;
    status.power = PowerState::Normal;
    status.food = FoodState::Hungry;
    status.motion = MotionMode::Null;
    status.emotion = kEmotionInitial;
    status.remainSeconds = 0;
    status.uptimeMs = nowMs;
    return status;
  }

  bool commandQueueHasRoom()
  {
    return gCommandQueue != nullptr && uxQueueSpacesAvailable(gCommandQueue) > 0;
  }

  bool queueCommand(const char *id, const char *command)
  {
    if (command == nullptr || command[0] == '\0')
    {
      return true;
    }

    if (!commandQueueHasRoom())
    {
      printNetworkLine("Site command queue full");
      return true;
    }

    NetworkRemoteCommand message;
    copyText(message.id, sizeof(message.id), id);
    copyText(message.command, sizeof(message.command), command);
    if (xQueueSend(gCommandQueue, &message, 0) == pdTRUE)
    {
      printNetworkLine("Site command queued");
    }
    return true;
  }

  bool queueCommandFromResponse(JsonDocument &doc)
  {
    JsonVariant commandValue = doc["command"];
    if (commandValue.isNull())
    {
      return true;
    }

    if (commandValue.is<const char *>())
    {
      return queueCommand(doc["id"] | "", commandValue.as<const char *>());
    }

    const char *command = commandValue["text"] | "";
    if (command[0] == '\0')
    {
      command = commandValue["command"] | "";
    }

    return queueCommand(commandValue["id"] | "", command);
  }

  bool handleSyncResponse(const String &response)
  {
    if (response.length() == 0)
    {
      return true;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, response);
    if (error)
    {
      printNetworkLine("Site sync JSON parse failed");
      return false;
    }

    JsonVariant okValue = doc["ok"];
    if (!okValue.isNull() && !okValue.as<bool>())
    {
      printNetworkLine("Site sync rejected");
      return false;
    }

    return queueCommandFromResponse(doc);
  }

  void fillSyncPayload(JsonDocument &doc, const NetworkPetStatus &status)
  {
    doc["device"] = kSmartPetDefaultDevice;
    doc["heartbeat"] = true;
    doc["mode"] = "website";
    doc["uptime_ms"] = status.uptimeMs;

    JsonObject statusObject = doc["status"].to<JsonObject>();
    statusObject["power"] = powerText(status.power);
    statusObject["emotion"] = status.emotion;
    statusObject["food"] = foodText(status.food);
    statusObject["remain"] = status.remainSeconds;
    statusObject["motion"] = motionName(status.motion);

    if (gHasPendingAck)
    {
      JsonObject ackObject = doc["ack"].to<JsonObject>();
      ackObject["id"] = gPendingAck.id;
      ackObject["command"] = gPendingAck.command;
      ackObject["result"] = gPendingAck.result;
    }
    else
    {
      doc["ack"] = nullptr;
    }
  }

  bool sendSyncNow(uint32_t nowMs)
  {
    if (!wifiReady())
    {
      return false;
    }

    drainLatestStatus();
    loadPendingAck();

    JsonDocument doc;
    fillSyncPayload(doc, latestStatusOrDefault(nowMs));

    String payload;
    serializeJson(doc, payload);

    WiFiClientSecure client;
    HTTPClient http;
    if (!beginSiteHttp(http, client, siteEndpoint("/sync")))
    {
      printNetworkLine("Site sync begin failed");
      return false;
    }

    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(payload);
    const String response = http.getString();
    http.end();

    if (code < 200 || code >= 300)
    {
      printHttpIssue("Site sync failed", code, response);
      return false;
    }

    if (!handleSyncResponse(response))
    {
      printHttpIssue("Site sync bad response", code, response);
      return false;
    }

    if (gHasPendingAck)
    {
      gHasPendingAck = false;
    }

    resetSiteRetry();
    printNetworkLine("Site sync sent");
    return true;
  }

  bool processSiteSync(uint32_t nowMs)
  {
    if (!gWebsiteModeEnabled || !timeReached(nowMs, gNextSyncMs))
    {
      return false;
    }

    const bool ok = sendSyncNow(nowMs);
    gNextSyncMs = nowMs + (ok ? kSmartPetSyncIntervalMs : nextSiteRetryDelay());
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
        if (processSiteSync(nowMs))
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
  gNextSyncMs = 0;
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
  gNextSyncMs = millis();
  resetSiteRetry();

  if (!enabled)
  {
    gHasPendingAck = false;
    gHasLatestStatus = false;
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

  gNextSyncMs = millis();
}

void networkTaskSubmitStatus(const NetworkPetStatus &status)
{
  if (!gWebsiteModeEnabled || gStatusQueue == nullptr)
  {
    return;
  }

  xQueueOverwrite(gStatusQueue, &status);
  if (status.force || status.eventTriggered)
  {
    gNextSyncMs = millis();
  }
}

bool networkTaskIsWifiConnected()
{
  return gWifiConnected;
}