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
#include <stdlib.h>
#include <string.h>

namespace
{
  // 本文件把 WiFi/HTTP 这类可能阻塞的工作放到后台 FreeRTOS 任务中执行。
  // 主循环只通过队列提交状态、取网站命令和提交回执，避免直接等待网络。
  constexpr uint32_t kWifiRetryIntervalMs = 15000;
  constexpr uint32_t kInitialWebsiteRetryMs = 2000;
  constexpr uint32_t kNetworkIdleDelayMs = 100;
  constexpr uint32_t kNetworkAfterWorkDelayMs = 50;
  constexpr uint32_t kNetworkTaskStackWords = 16384;
  constexpr UBaseType_t kNetworkTaskPriority = 1;
  constexpr BaseType_t kNetworkTaskCore = 0;
  constexpr size_t kCommandQueueLength = 2;
  constexpr size_t kAckQueueLength = 2;
  constexpr size_t kStatusQueueLength = 1;
  constexpr size_t kResponsePreviewLength = 80;

  constexpr const char *kAmapWeatherApiUrl =
      "https://restapi.amap.com/v3/weather/weatherInfo";

  // 网站命令执行后的回执消息。
  struct NetworkAckMessage
  {
    char id[48];
    char command[80];
    char result[24];
  };

  TaskHandle_t gTaskHandle = nullptr;

  // 三个队列是主循环和网络任务之间的交汇点。
  QueueHandle_t gCommandQueue = nullptr;
  QueueHandle_t gAckQueue = nullptr;
  QueueHandle_t gStatusQueue = nullptr;

  volatile bool gWebsiteModeEnabled = false;
  volatile bool gWifiConnected = false;
  volatile uint32_t gNextSyncMs = 0;

  // WiFi 的启动和重试节奏独立管理，避免开机瞬间或断线时频繁冲击系统。
  bool gWifiStarted = false;
  uint32_t gWifiStartAllowedMs = 0;
  uint32_t gNextWifiRetryMs = 0;
  uint32_t gNextWeatherFetchMs = 0;
  uint32_t gWebsiteRetryDelayMs = kInitialWebsiteRetryMs;

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

  void resetWebsiteRetry()
  {
    gWebsiteRetryDelayMs = kInitialWebsiteRetryMs;
  }

  // 网站同步失败时使用指数退避，避免服务器或网络异常时高频请求。
  uint32_t nextWebsiteRetryDelay()
  {
    const uint32_t delayMs = gWebsiteRetryDelayMs;
    gWebsiteRetryDelayMs = min(gWebsiteRetryDelayMs * 2, kSmartPetRetryMaxMs);
    return delayMs;
  }

  bool wifiReady()
  {
    const bool connected = WiFi.status() == WL_CONNECTED;
    gWifiConnected = connected;
    return connected;
  }

  // 发起 WiFi 连接。开启 STA 模式、省电，并限制发射功率来降低消耗。
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

  String websiteEndpoint(const char *path)
  {
    String url = kSmartPetApiBaseUrl;
    url += path;
    return url;
  }

  String weatherEndpoint()
  {
    String url = kAmapWeatherApiUrl;
    url += "?city=";
    url += kAmapWeatherCity;
    url += "&key=";
    url += kAmapWeatherKey;
    url += "&extensions=base&output=JSON";
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

  // 所有 HTTPS 请求的公共入口。
  bool beginSecureHttp(HTTPClient &http,
                       WiFiClientSecure &client,
                       const String &url,
                       uint32_t timeoutMs)
  {
    client.setInsecure();
    http.setTimeout(timeoutMs);
    return http.begin(client, url);
  }

  bool beginWebsiteHttp(HTTPClient &http, WiFiClientSecure &client, const String &url)
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

  // 从 ack 队列取一条待发送回执；若已有待发送 ack，则先保留它直到同步成功。
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

  // 如果主循环还没提交状态，先生成一个保守默认状态，保证心跳仍可发送。
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

  // 把网站返回的命令放入 command 队列，等待主循环取走并交给状态机处理。
  bool queueCommand(const char *id, const char *command)
  {
    if (command == nullptr || command[0] == '\0')
    {
      return true;
    }

    if (!commandQueueHasRoom())
    {
      printNetworkLine("Website command queue full");
      return true;
    }

    NetworkRemoteCommand message;
    copyText(message.id, sizeof(message.id), id);
    copyText(message.command, sizeof(message.command), command);
    if (xQueueSend(gCommandQueue, &message, 0) == pdTRUE)
    {
      printNetworkLine("Website command queued");
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

  // 解析 /sync 响应：确认 ok 字段，并提取可能存在的新命令。
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
      printNetworkLine("Website sync JSON parse failed");
      return false;
    }

    JsonVariant okValue = doc["ok"];
    if (!okValue.isNull() && !okValue.as<bool>())
    {
      printNetworkLine("Website sync rejected");
      return false;
    }

    return queueCommandFromResponse(doc);
  }

  // 组装发给网站 /sync 的 JSON：心跳、当前状态，以及可选命令回执。
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

  // 执行一次完整网站同步：提交状态/ack，接收网站下发的命令。
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
    if (!beginWebsiteHttp(http, client, websiteEndpoint("/sync")))
    {
      printNetworkLine("Website sync begin failed");
      return false;
    }

    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(payload);
    const String response = http.getString();
    http.end();

    if (code < 200 || code >= 300)
    {
      printHttpIssue("Website sync failed", code, response);
      return false;
    }

    if (!handleSyncResponse(response))
    {
      printHttpIssue("Website sync bad response", code, response);
      return false;
    }

    if (gHasPendingAck)
    {
      gHasPendingAck = false;
    }

    resetWebsiteRetry();
    printNetworkLine("Website sync sent");
    return true;
  }

  bool processWebsiteSync(uint32_t nowMs)
  {
    if (!gWebsiteModeEnabled || !timeReached(nowMs, gNextSyncMs))
    {
      return false;
    }

    const bool ok = sendSyncNow(nowMs);
    gNextSyncMs = nowMs + (ok ? kSmartPetSyncIntervalMs : nextWebsiteRetryDelay());
    return true;
  }

  // Open-Meteo 的 weather_code 转成项目内部显示用的短文本。
  bool containsText(const char *text, const char *needle)
  {
    return text != nullptr && strstr(text, needle) != nullptr;
  }

  const char *amapWeatherName(const char *weather)
  {
    if (weather == nullptr || weather[0] == '\0')
    {
      return "UNKNOWN";
    }
    if (containsText(weather, "\xE6\x99\xB4"))
    {
      return "CLEAR";
    }
    if (containsText(weather, "\xE9\x9B\xB7"))
    {
      return "STORM";
    }
    if (containsText(weather, "\xE9\x9B\xA8"))
    {
      return "RAIN";
    }
    if (containsText(weather, "\xE9\x9B\xAA"))
    {
      return "SNOW";
    }
    if (containsText(weather, "\xE9\x9B\xBE") || containsText(weather, "\xE9\x9C\xBE"))
    {
      return "FOG";
    }
    if (containsText(weather, "\xE6\xB2\x99") || containsText(weather, "\xE5\xB0\x98"))
    {
      return "DUST";
    }
    if (containsText(weather, "\xE4\xBA\x91"))
    {
      return "CLOUD";
    }
    if (containsText(weather, "\xE9\x98\xB4"))
    {
      return "OVERCAST";
    }
    return "WEATHER";
  }

  const char *amapWindDirectionName(const char *direction)
  {
    if (direction == nullptr || direction[0] == '\0' || containsText(direction, "\xE6\x97\xA0"))
    {
      return "CALM";
    }
    if (containsText(direction, "\xE4\xB8\x9C\xE5\x8C\x97"))
    {
      return "NE";
    }
    if (containsText(direction, "\xE4\xB8\x9C\xE5\x8D\x97"))
    {
      return "SE";
    }
    if (containsText(direction, "\xE8\xA5\xBF\xE5\x8C\x97"))
    {
      return "NW";
    }
    if (containsText(direction, "\xE8\xA5\xBF\xE5\x8D\x97"))
    {
      return "SW";
    }
    if (containsText(direction, "\xE5\x8C\x97"))
    {
      return "N";
    }
    if (containsText(direction, "\xE5\x8D\x97"))
    {
      return "S";
    }
    if (containsText(direction, "\xE4\xB8\x9C"))
    {
      return "E";
    }
    if (containsText(direction, "\xE8\xA5\xBF"))
    {
      return "W";
    }
    return "VAR";
  }
  void extractWindLevel(char *target, size_t targetLength, const char *windPower)
  {
    if (targetLength == 0)
    {
      return;
    }

    size_t length = 0;
    if (windPower != nullptr)
    {
      for (size_t index = 0; windPower[index] != '\0' && length + 1 < targetLength; ++index)
      {
        const char c = windPower[index];
        if ((c >= '0' && c <= '9') || c == '-')
        {
          target[length++] = c;
        }
      }
    }

    if (length == 0)
    {
      target[length++] = 'N';
      if (length + 1 < targetLength)
      {
        target[length++] = 'A';
      }
    }
    target[length] = '\0';
  }

  void makeWindText(char *target,
                    size_t targetLength,
                    const char *direction,
                    const char *windPower)
  {
    char level[8];
    extractWindLevel(level, sizeof(level), windPower);
    snprintf(target,
             targetLength,
             "WIND %s L%s",
             amapWindDirectionName(direction),
             level);
  }

  // 拉取天气数据并更新 weather_service；返回 true 表示本轮确实尝试了 HTTP 工作。
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
    if (!beginSecureHttp(http, client, weatherEndpoint(), kWeatherHttpTimeoutMs))
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

    if (strcmp(doc["status"] | "", "1") != 0)
    {
      weatherSetNetworkStatus("API REJECTED");
      gNextWeatherFetchMs = nowMs + kWeatherRetryIntervalMs;
      return true;
    }

    JsonObject live = doc["lives"][0];
    if (live.isNull())
    {
      weatherSetNetworkStatus("NO CURRENT DATA");
      gNextWeatherFetchMs = nowMs + kWeatherRetryIntervalMs;
      return true;
    }

    const float temperatureC = atof(live["temperature"] | "0");
    const int humidityPercent = atoi(live["humidity"] | "0");
    const char *weather = live["weather"] | "";
    const char *windDirection = live["winddirection"] | "";
    const char *windPower = live["windpower"] | "";

    char windText[16];
    makeWindText(windText, sizeof(windText), windDirection, windPower);

    weatherUpdateFromNetwork(temperatureC,
                             humidityPercent,
                             windText,
                             amapWeatherName(weather),
                             nowMs);
    gNextWeatherFetchMs = nowMs + kWeatherFetchIntervalMs;
    return true;
  }

  // 后台网络任务主体：先保证 WiFi，再优先网站同步，其次天气拉取。
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
        if (processWebsiteSync(nowMs))
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

} // namespace

// 初始化网络模块：创建队列，并启动固定在 core 0 的后台网络任务。
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
  resetWebsiteRetry();

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

// 切换网站控制模式。关闭时清空网站命令、状态和回执，避免模式切换后残留。
void networkTaskSetWebsiteMode(bool enabled)
{
  if (enabled == gWebsiteModeEnabled)
  {
    return;
  }

  gWebsiteModeEnabled = enabled;
  gNextSyncMs = millis();
  resetWebsiteRetry();

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

// 主循环调用：非阻塞地取出一条网站命令，没有命令就立刻返回 false。
bool networkTaskTakeCommand(NetworkRemoteCommand &out)
{
  if (!gWebsiteModeEnabled || gCommandQueue == nullptr)
  {
    return false;
  }

  return xQueueReceive(gCommandQueue, &out, 0) == pdTRUE;
}

// 主循环调用：提交命令执行结果，并触发尽快 /sync 给网站。
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
    printNetworkLine("Website ack queue full");
  }

  gNextSyncMs = millis();
}

// 主循环调用：覆盖保存最新状态；强制或事件状态会触发尽快同步。
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

// 给显示或调试逻辑查询最近一次缓存的 WiFi 连接状态。
bool networkTaskIsWifiConnected()
{
  return gWifiConnected;
}