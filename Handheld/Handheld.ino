#include <SPI.h>
#include <LoRa.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_wifi.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// LoRa pins for T3 v1.6
#define SCK     5
#define MISO    19
#define MOSI    27
#define SS      18
#define RST     23
#define DIO0    26

// OLED
#define OLED_SDA    21
#define OLED_SCL    22
#define OLED_RST    -1
#define OLED_ADDR   0x3C
#define SCREEN_W    128
#define SCREEN_H    64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RST);

// Battery
#define BAT_PIN     35
#define BAT_SAMPLES 10

// Settings
#define LORA_FREQ     915E6
#define LORA_SYNC     0xF3
#define SECRET        "qQXR79UCb4*r"
#define ACK_WAIT_MS   10000
#define BAT_REPORT_MS 300000  // 5 minutes

// BLE UUIDs
#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NOTIFY_UUID         "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

bool deviceConnected    = false;
bool loRaPending        = false;
bool sendBatPending     = false;
unsigned long batSendTime = 0;
String pendingLoRaCmd   = "";
BLECharacteristic* pNotifyCharacteristic = nullptr;
BLEServer* pServer = nullptr;

// ── Display ───────────────────────────────────────────────
String statusLines[4] = {"", "", "", ""};
int batPct = -1;
String bleStatus = "Advertising";

void displayUpdate() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  display.println("=== Clef de char ===");

  // BLE status
  display.setCursor(0, 12);
  display.print("BLE: ");
  display.println(bleStatus);

  // Battery
  display.setCursor(0, 22);
  display.print("Bat: ");
  if (batPct >= 0) {
    display.print(batPct);
    display.println("%");
  } else {
    display.println("--");
  }

  // Divider
  display.drawLine(0, 32, 128, 32, SSD1306_WHITE);

  // Status log — last 4 lines
  for (int i = 0; i < 4; i++) {
    display.setCursor(0, 34 + i * 8);
    display.println(statusLines[i]);
  }

  display.display();
}

void addStatus(String msg) {
  // Scroll lines up
  statusLines[0] = statusLines[1];
  statusLines[1] = statusLines[2];
  statusLines[2] = statusLines[3];
  statusLines[3] = msg;
  Serial.println(msg);
  displayUpdate();
}

// ── Battery ───────────────────────────────────────────────
float readBatteryVoltage() {
  long sum = 0;
  for (int i = 0; i < BAT_SAMPLES; i++) {
    sum += analogRead(BAT_PIN);
    delay(10);
  }
  float avg = sum / BAT_SAMPLES;
  return (avg / 4095.0) * 2.0 * 3.9 * 1.1;
}

int batteryPercent() {
  float v = readBatteryVoltage();
  int pct = (int)((v - 3.0) / (4.2 - 3.0) * 100);
  if (pct > 100) pct = 100;
  if (pct < 0)   pct = 0;
  return pct;
}

// ── BLE ──────────────────────────────────────────────────
void sendBLE(String msg) {
  if (!deviceConnected || !pNotifyCharacteristic) return;
  pNotifyCharacteristic->setValue(msg.c_str());
  pNotifyCharacteristic->notify();
}

void sendBatteryLevel() {
  batPct = batteryPercent();
  String batMsg = "BAT:" + String(batPct);
  sendBLE(batMsg);
  addStatus("Bat: " + String(batPct) + "%");
  displayUpdate();
}

// ── LoRa ─────────────────────────────────────────────────
void sendLoRa(String cmd) {
  LoRa.idle();
  delay(10);

  String fullMsg = SECRET + cmd;
  LoRa.beginPacket();
  LoRa.print(fullMsg);
  LoRa.endPacket();
  addStatus("> " + cmd);

  LoRa.receive();
  unsigned long waitStart = millis();
  while (millis() - waitStart < ACK_WAIT_MS) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String msg = "";
      while (LoRa.available())
        msg += (char)LoRa.read();
      if (msg.startsWith("ACK:")) {
        String ack = msg.substring(4);
        addStatus("< ACK:" + ack);
        sendBLE(msg);
      } else if (msg.startsWith("GPS:")) {
        addStatus("< GPS rcvd");
        sendBLE(msg);
      }
      break;
    }
  }

  LoRa.sleep();
}

// ── BLE callbacks ─────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    bleStatus = "Connected";
    addStatus("Phone connected");
    BLEDevice::startAdvertising();
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected  = false;
    sendBatPending   = false;
    bleStatus = "Advertising";
    addStatus("Disconnected");
    delay(500);
    BLEDevice::startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String cmd = pCharacteristic->getValue().c_str();
    cmd.trim();
    pCharacteristic->setValue("");

    if      (cmd == "LOCK")    { pendingLoRaCmd = "LOCK";    loRaPending = true; }
    else if (cmd == "UNLOCK")  { pendingLoRaCmd = "UNLOCK";  loRaPending = true; }
    else if (cmd == "CLIMATE") { pendingLoRaCmd = "CLIMATE"; loRaPending = true; }
    else if (cmd == "WHERE")   { pendingLoRaCmd = "WHERE";   loRaPending = true; }
    else if (cmd == "PING")    { /* silent */ }
    else if (cmd == "BAT") {
      sendBatPending = true;
      batSendTime    = millis();
    }
  }
};

void initBLE() {
  BLEDevice::init("CarRemote");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Device Information Service — required for stable iOS connection
  BLEService* pInfoService = pServer->createService((uint16_t)0x180A);

  BLECharacteristic* pManufacturer = pInfoService->createCharacteristic(
    (uint16_t)0x2A29,
    BLECharacteristic::PROPERTY_READ
  );
  pManufacturer->setValue("Espressif");

  BLECharacteristic* pModel = pInfoService->createCharacteristic(
    (uint16_t)0x2A24,
    BLECharacteristic::PROPERTY_READ
  );
  pModel->setValue("CarRemote");

  BLECharacteristic* pFirmware = pInfoService->createCharacteristic(
    (uint16_t)0x2A26,
    BLECharacteristic::PROPERTY_READ
  );
  pFirmware->setValue("1.0");

  BLECharacteristic* pSerial = pInfoService->createCharacteristic(
    (uint16_t)0x2A25,
    BLECharacteristic::PROPERTY_READ
  );
  pSerial->setValue("001");

  pInfoService->start();

  // Main UART Service
  BLEService* pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic* pWriteCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pWriteCharacteristic->setCallbacks(new CommandCallbacks());

  pNotifyCharacteristic = pService->createCharacteristic(
    NOTIFY_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pNotifyCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  // Advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->addServiceUUID((uint16_t)0x180A);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->setMinInterval(1600);
  pAdvertising->setMaxInterval(3200);
  pAdvertising->start();
}

void initLoRa() {
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    addStatus("LoRa FAILED");
    while (1);
  }

  LoRa.setSyncWord(LORA_SYNC);
  LoRa.setSpreadingFactor(10);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(20);
  LoRa.sleep();
}

void initDisplay() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
    while (1);
  }
  display.clearDisplay();
  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(BAT_PIN, INPUT);
  analogSetAttenuation(ADC_11db);

  // Disable WiFi
  esp_wifi_stop();
  esp_wifi_deinit();

  initDisplay();
  addStatus("Starting...");

  initLoRa();
  addStatus("LoRa OK");

  initBLE();
  addStatus("BLE OK");

  delay(100);
  LoRa.sleep();

  addStatus("Ready");
}

void loop() {
  // Send battery after request
  if (sendBatPending && millis() - batSendTime >= 1500) {
    sendBatPending = false;
    sendBatteryLevel();
  }

  // Handle pending LoRa transmission
  if (loRaPending) {
    loRaPending = false;
    sendLoRa(pendingLoRaCmd);
  }

  // Send battery every 5 minutes
  static unsigned long lastBatReport = 0;
  if (millis() - lastBatReport >= BAT_REPORT_MS) {
    lastBatReport = millis();
    sendBatteryLevel();
  }
}
