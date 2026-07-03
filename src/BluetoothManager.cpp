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

  struct BleGattContext
  {
    BLEServer *server = nullptr;
    BLECharacteristic *txCharacteristic = nullptr;
    bool advertisingConfigured = false;
  };

  struct BleRuntimeState
  {
    volatile bool enabled = true;
    volatile bool initRequested = false;
    volatile bool initialized = false;
    volatile bool connected = false;
    volatile bool advertising = false;
    volatile bool connectEvent = false;
    volatile bool disconnectEvent = false;
    uint32_t initAtMs = 0;
    uint16_t connectionId = kNoConnectionId;
  };

  struct BleCommandMailbox
  {
    volatile bool pending = false;
    char buffer[kBleCommandLength + 1] = "";
  };

  BleGattContext gGatt;
  BleRuntimeState gState;
  BleCommandMailbox gMailbox;
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

  void printBleLine(const char *message)
  {
    Serial.print('[');
    Serial.print(millis());
    Serial.print(F(" ms] "));
    Serial.println(message);
  }

  void resetConnectionState()
  {
    gState.connected = false;
    gState.advertising = false;
    gState.connectEvent = false;
    gState.disconnectEvent = false;
    gState.connectionId = kNoConnectionId;
  }

  void clearPendingCommand()
  {
    portENTER_CRITICAL(&gCommandMux);
    gMailbox.buffer[0] = '\0';
    gMailbox.pending = false;
    portEXIT_CRITICAL(&gCommandMux);
  }

  void storePendingCommand(const char *text)
  {
    portENTER_CRITICAL(&gCommandMux);
    strncpy(gMailbox.buffer, text, kBleCommandLength);
    gMailbox.buffer[kBleCommandLength] = '\0';
    gMailbox.pending = true;
    portEXIT_CRITICAL(&gCommandMux);
  }

  bool takePendingCommand(char *out, size_t outLength)
  {
    bool hasCommand = false;

    portENTER_CRITICAL(&gCommandMux);
    if (gMailbox.pending)
    {
      strncpy(out, gMailbox.buffer, outLength - 1);
      out[outLength - 1] = '\0';
      gMailbox.pending = false;
      hasCommand = true;
    }
    portEXIT_CRITICAL(&gCommandMux);

    return hasCommand;
  }

  bool copyWrittenValueToText(BLECharacteristic *characteristic,
                              char *out,
                              size_t outLength)
  {
    if (characteristic == nullptr || outLength == 0)
    {
      return false;
    }

    const auto value = characteristic->getValue();
    if (value.length() == 0)
    {
      out[0] = '\0';
      return false;
    }

    size_t length = 0;
    for (size_t index = 0; index < value.length() && length < outLength - 1; ++index)
    {
      const char c = static_cast<char>(value[index]);
      if (c == '\r' || c == '\n')
      {
        continue;
      }
      out[length++] = c;
    }
    out[length] = '\0';

    return length > 0;
  }

  void markConnected(uint16_t connectionId)
  {
    gState.connectionId = connectionId;
    gState.connected = true;
    gState.connectEvent = true;
    gState.advertising = false;
  }

  void markDisconnected()
  {
    gState.connectionId = kNoConnectionId;
    gState.connected = false;
    gState.disconnectEvent = true;
    gState.advertising = false;
  }

  void configureAdvertising(BLEAdvertising *advertising)
  {
    if (advertising == nullptr || gGatt.advertisingConfigured)
    {
      return;
    }

    advertising->addServiceUUID(kBleServiceUuid);
    advertising->setScanResponse(false);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    gGatt.advertisingConfigured = true;
  }

  void startAdvertising()
  {
    if (!gState.initialized || !gState.enabled)
    {
      return;
    }

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    configureAdvertising(advertising);
    BLEDevice::startAdvertising();
    gState.advertising = true;
  }

  class ServerCallbacks : public BLEServerCallbacks
  {
  public:
    void onConnect(BLEServer *server) override
    {
      gGatt.server = server;
      markConnected(server != nullptr ? server->getConnId() : kNoConnectionId);
    }

    void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
    {
      (void)server;
      markConnected(param != nullptr ? param->connect.conn_id : kNoConnectionId);
    }

    void onDisconnect(BLEServer *server) override
    {
      (void)server;
      markDisconnected();
    }

    void onDisconnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
    {
      (void)server;
      (void)param;
      markDisconnected();
    }
  };

  class RxCallbacks : public BLECharacteristicCallbacks
  {
  public:
    void onWrite(BLECharacteristic *characteristic) override
    {
      if (!gState.enabled)
      {
        return;
      }

      char text[kBleCommandLength + 1];
      if (!copyWrittenValueToText(characteristic, text, sizeof(text)))
      {
        return;
      }

      char *trimmed = trimText(text);
      if (*trimmed == '\0')
      {
        return;
      }

      storePendingCommand(trimmed);
    }
  };

  ServerCallbacks gServerCallbacks;
  RxCallbacks gRxCallbacks;

  void createGattServer()
  {
    gGatt.server = BLEDevice::createServer();
    gGatt.server->setCallbacks(&gServerCallbacks);
  }

  BLEService *createSmartPetService()
  {
    return gGatt.server->createService(kBleServiceUuid);
  }

  void createTxCharacteristic(BLEService *service)
  {
    gGatt.txCharacteristic = service->createCharacteristic(
        kBleTxUuid,
        BLECharacteristic::PROPERTY_NOTIFY);
    gGatt.txCharacteristic->addDescriptor(new BLE2902());
  }

  void createRxCharacteristic(BLEService *service)
  {
    BLECharacteristic *rxCharacteristic = service->createCharacteristic(
        kBleRxUuid,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rxCharacteristic->setCallbacks(&gRxCallbacks);
  }

  void initializeBluetoothNow()
  {
    if (!gState.enabled || gState.initialized)
    {
      return;
    }

    BLEDevice::init(kBleDeviceName);
    BLEDevice::setPower(PET_BLE_POWER_LEVEL);

    createGattServer();
    BLEService *service = createSmartPetService();
    createTxCharacteristic(service);
    createRxCharacteristic(service);

    service->start();
    gState.initialized = true;
    startAdvertising();

    printBleLine("BLE SmartPet advertising");
    printBleLine("BLE app: connect SmartPet");
    printBleLine("BLE app: enable TX Notify, write UTF-8 text to RX");
  }

  void releaseGattContext()
  {
    gGatt.server = nullptr;
    gGatt.txCharacteristic = nullptr;
    gGatt.advertisingConfigured = false;
  }

  void disableBluetoothNow()
  {
    gState.initRequested = false;
    clearPendingCommand();

    if (!gState.initialized)
    {
      resetConnectionState();
      return;
    }

    if (gState.connected && gGatt.txCharacteristic != nullptr)
    {
      bluetoothNotify("BLE disabled: website mode");
    }

    if (gGatt.server != nullptr &&
        gState.connected &&
        gState.connectionId != kNoConnectionId)
    {
      gGatt.server->disconnect(gState.connectionId);
    }

    BLEDevice::stopAdvertising();
    BLEDevice::deinit(false);

    releaseGattContext();
    gState.initialized = false;
    resetConnectionState();
    printBleLine("BLE disabled");
  }

}  // namespace

void bluetoothInit()
{
  if (!gState.enabled || gState.initRequested || gState.initialized)
  {
    return;
  }

  gState.initRequested = true;
  gState.initAtMs = millis() + kBleStartDelayMs;
  if (kBleStartDelayMs == 0)
  {
    initializeBluetoothNow();
    return;
  }

  printBleLine("BLE init delayed");
}

void updateBluetooth()
{
  if (!gState.enabled)
  {
    return;
  }

  if (!gState.initialized)
  {
    if (gState.initRequested && timeReached(millis(), gState.initAtMs))
    {
      initializeBluetoothNow();
    }
    return;
  }

  if (gState.connectEvent)
  {
    gState.connectEvent = false;
    printBleLine("BLE connected");
    bluetoothNotify("BLE connected: SmartPet");
  }

  if (gState.disconnectEvent)
  {
    gState.disconnectEvent = false;
    printBleLine("BLE disconnected");
    if (!gState.advertising)
    {
      startAdvertising();
      printBleLine("BLE advertising restarted");
    }
  }
}

void bluetoothSetEnabled(bool enabled)
{
  if (enabled == gState.enabled)
  {
    return;
  }

  gState.enabled = enabled;
  if (!enabled)
  {
    disableBluetoothNow();
    return;
  }

  bluetoothInit();
}

bool bluetoothIsEnabled()
{
  return gState.enabled;
}

bool bluetoothTakeCommand(String &out)
{
  if (!gState.enabled || !gState.initialized)
  {
    return false;
  }

  char command[kBleCommandLength + 1];
  if (!takePendingCommand(command, sizeof(command)))
  {
    return false;
  }

  out = command;
  return true;
}

void bluetoothNotify(const String &message)
{
  if (!gState.enabled ||
      !gState.initialized ||
      !gState.connected ||
      gGatt.txCharacteristic == nullptr)
  {
    return;
  }

  String line = message;
  if (!line.endsWith("\n"))
  {
    line += '\n';
  }

  gGatt.txCharacteristic->setValue(line.c_str());
  gGatt.txCharacteristic->notify();
}

bool bluetoothIsConnected()
{
  return gState.enabled && gState.connected;
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