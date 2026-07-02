#include "BluetoothManager.h"

#include "app_config.h"

#ifndef PET_ENABLE_BLE_CONTROL
#define PET_ENABLE_BLE_CONTROL 1
#endif

#if PET_ENABLE_BLE_CONTROL

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include <ctype.h>
#include <string.h>

#ifndef PET_BLE_POWER_LEVEL
#define PET_BLE_POWER_LEVEL ESP_PWR_LVL_N9
#endif

namespace
{
  constexpr const char *kBleDeviceName = "SmartPet";

  constexpr const char *kBleServiceUuid =
      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
  constexpr const char *kBleRxUuid =
      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
  constexpr const char *kBleTxUuid =
      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

  constexpr size_t kBleCommandLength = 80;
  constexpr uint16_t kNoConnectionId = 0xFFFF;

  BLEServer *gServer = nullptr;
  BLECharacteristic *gTxCharacteristic = nullptr;
  volatile bool gEnabled = true;
  volatile bool gInitRequested = false;
  volatile bool gInitialized = false;
  volatile bool gConnected = false;
  volatile bool gAdvertising = false;
  bool gAdvertisingConfigured = false;
  volatile bool gConnectEvent = false;
  volatile bool gDisconnectEvent = false;
  volatile bool gPendingCommand = false;
  uint32_t gInitAtMs = 0;
  uint16_t gConnectionId = kNoConnectionId;
  char gCommandBuffer[kBleCommandLength + 1] = "";
  portMUX_TYPE gCommandMux = portMUX_INITIALIZER_UNLOCKED;

  bool timeReached(uint32_t nowMs, uint32_t targetMs)
  {
    return static_cast<int32_t>(nowMs - targetMs) >= 0;
  }

  char *trimText(char *text)
  {
    while (*text != '\0' && isspace(static_cast<unsigned char>(*text)))
    {
      ++text;
    }

    if (*text == '\0')
    {
      return text;
    }

    char *end = text + strlen(text) - 1;
    while (end > text && isspace(static_cast<unsigned char>(*end)))
    {
      *end = '\0';
      --end;
    }

    return text;
  }

  void clearPendingCommand()
  {
    portENTER_CRITICAL(&gCommandMux);
    gCommandBuffer[0] = '\0';
    gPendingCommand = false;
    portEXIT_CRITICAL(&gCommandMux);
  }

  void printBleLine(const char *message)
  {
    Serial.print('[');
    Serial.print(millis());
    Serial.print(F(" ms] "));
    Serial.println(message);
  }

  void startAdvertising()
  {
    if (!gInitialized || !gEnabled)
    {
      return;
    }

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    if (!gAdvertisingConfigured)
    {
      advertising->addServiceUUID(kBleServiceUuid);
      advertising->setScanResponse(false);
      advertising->setMinPreferred(0x06);
      advertising->setMinPreferred(0x12);
      gAdvertisingConfigured = true;
    }
    BLEDevice::startAdvertising();
    gAdvertising = true;
  }

  class ServerCallbacks : public BLEServerCallbacks
  {
  public:
    void onConnect(BLEServer *server) override
    {
      gServer = server;
      gConnectionId = server != nullptr ? server->getConnId() : kNoConnectionId;
      gConnected = true;
      gConnectEvent = true;
      gAdvertising = false;
    }

    void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
    {
      (void)server;
      gConnectionId = param != nullptr ? param->connect.conn_id : kNoConnectionId;
      gConnected = true;
      gConnectEvent = true;
      gAdvertising = false;
    }

    void onDisconnect(BLEServer *server) override
    {
      (void)server;
      gConnectionId = kNoConnectionId;
      gConnected = false;
      gDisconnectEvent = true;
      gAdvertising = false;
    }

    void onDisconnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
    {
      (void)server;
      (void)param;
      gConnectionId = kNoConnectionId;
      gConnected = false;
      gDisconnectEvent = true;
      gAdvertising = false;
    }
  };

  class RxCallbacks : public BLECharacteristicCallbacks
  {
  public:
    void onWrite(BLECharacteristic *characteristic) override
    {
      if (!gEnabled)
      {
        return;
      }

      const auto value = characteristic->getValue();
      if (value.length() == 0)
      {
        return;
      }

      char text[kBleCommandLength + 1];
      size_t length = 0;
      for (size_t index = 0; index < value.length() && length < kBleCommandLength; ++index)
      {
        const char c = static_cast<char>(value[index]);
        if (c == '\r' || c == '\n')
        {
          continue;
        }
        text[length++] = c;
      }
      text[length] = '\0';

      char *trimmed = trimText(text);
      if (*trimmed == '\0')
      {
        return;
      }

      portENTER_CRITICAL(&gCommandMux);
      strncpy(gCommandBuffer, trimmed, kBleCommandLength);
      gCommandBuffer[kBleCommandLength] = '\0';
      gPendingCommand = true;
      portEXIT_CRITICAL(&gCommandMux);
    }
  };

  ServerCallbacks gServerCallbacks;
  RxCallbacks gRxCallbacks;

  void initializeBluetoothNow()
  {
    if (!gEnabled || gInitialized)
    {
      return;
    }

    BLEDevice::init(kBleDeviceName);
    BLEDevice::setPower(PET_BLE_POWER_LEVEL);

    gServer = BLEDevice::createServer();
    gServer->setCallbacks(&gServerCallbacks);

    BLEService *service = gServer->createService(kBleServiceUuid);

    gTxCharacteristic = service->createCharacteristic(
        kBleTxUuid,
        BLECharacteristic::PROPERTY_NOTIFY);
    gTxCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *rxCharacteristic = service->createCharacteristic(
        kBleRxUuid,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rxCharacteristic->setCallbacks(&gRxCallbacks);

    service->start();
    gInitialized = true;
    startAdvertising();

    printBleLine("BLE SmartPet advertising");
    printBleLine("BLE app: connect SmartPet");
    printBleLine("BLE app: enable TX Notify, write UTF-8 text to RX");
  }

  void disableBluetoothNow()
  {
    gInitRequested = false;
    clearPendingCommand();

    if (!gInitialized)
    {
      gConnected = false;
      gAdvertising = false;
      gConnectEvent = false;
      gDisconnectEvent = false;
      gConnectionId = kNoConnectionId;
      return;
    }

    if (gConnected && gTxCharacteristic != nullptr)
    {
      bluetoothNotify("BLE disabled: website mode");
    }

    if (gServer != nullptr && gConnected && gConnectionId != kNoConnectionId)
    {
      gServer->disconnect(gConnectionId);
    }

    BLEDevice::stopAdvertising();
    BLEDevice::deinit(false);

    gServer = nullptr;
    gTxCharacteristic = nullptr;
    gInitialized = false;
    gConnected = false;
    gAdvertising = false;
    gAdvertisingConfigured = false;
    gConnectEvent = false;
    gDisconnectEvent = false;
    gConnectionId = kNoConnectionId;
    printBleLine("BLE disabled");
  }

}  // namespace

void bluetoothInit()
{
  if (!gEnabled || gInitRequested || gInitialized)
  {
    return;
  }

  gInitRequested = true;
  gInitAtMs = millis() + kBleStartDelayMs;
  if (kBleStartDelayMs == 0)
  {
    initializeBluetoothNow();
    return;
  }

  printBleLine("BLE init delayed");
}

void updateBluetooth()
{
  if (!gEnabled)
  {
    return;
  }

  if (!gInitialized)
  {
    if (gInitRequested && timeReached(millis(), gInitAtMs))
    {
      initializeBluetoothNow();
    }
    return;
  }

  if (gConnectEvent)
  {
    gConnectEvent = false;
    printBleLine("BLE connected");
    bluetoothNotify("BLE connected: SmartPet");
  }

  if (gDisconnectEvent)
  {
    gDisconnectEvent = false;
    printBleLine("BLE disconnected");
    if (!gAdvertising)
    {
      startAdvertising();
      printBleLine("BLE advertising restarted");
    }
  }
}

void bluetoothSetEnabled(bool enabled)
{
  if (enabled == gEnabled)
  {
    return;
  }

  gEnabled = enabled;
  if (!enabled)
  {
    disableBluetoothNow();
    return;
  }

  bluetoothInit();
}

bool bluetoothIsEnabled()
{
  return gEnabled;
}

bool bluetoothTakeCommand(String &out)
{
  if (!gEnabled || !gInitialized)
  {
    return false;
  }

  char command[kBleCommandLength + 1];
  bool hasCommand = false;

  portENTER_CRITICAL(&gCommandMux);
  if (gPendingCommand)
  {
    strncpy(command, gCommandBuffer, kBleCommandLength);
    command[kBleCommandLength] = '\0';
    gPendingCommand = false;
    hasCommand = true;
  }
  portEXIT_CRITICAL(&gCommandMux);

  if (!hasCommand)
  {
    return false;
  }

  out = command;
  return true;
}

void bluetoothNotify(const String &message)
{
  if (!gEnabled || !gInitialized || !gConnected || gTxCharacteristic == nullptr)
  {
    return;
  }

  String line = message;
  if (!line.endsWith("\n"))
  {
    line += '\n';
  }

  gTxCharacteristic->setValue(line.c_str());
  gTxCharacteristic->notify();
}

bool bluetoothIsConnected()
{
  return gEnabled && gConnected;
}

#else

void bluetoothInit()
{
}

void updateBluetooth()
{
}

void bluetoothSetEnabled(bool enabled)
{
  (void)enabled;
}

bool bluetoothIsEnabled()
{
  return false;
}

bool bluetoothTakeCommand(String &out)
{
  (void)out;
  return false;
}

void bluetoothNotify(const String &message)
{
  (void)message;
}

bool bluetoothIsConnected()
{
  return false;
}

#endif