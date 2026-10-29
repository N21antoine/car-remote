#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include "esp_wifi.h"
#include "esp_bt.h"

// LoRa pins for T3 v1.6
#define SCK     5
#define MISO    19
#define MOSI    27
#define SS      18
#define RST     23
#define DIO0    26

// Relay pins
#define RELAY_UNLOCK  14  // CH1
#define RELAY_LOCK    13  // CH2
#define RELAY_START   4   // CH3
#define RELAY_POWER   2   // CH4

// GPS pins
#define GPS_RX   21
#define GPS_TX   22
#define GPS_BAUD 9600

// Settings
#define LORA_FREQ        915E6
#define LORA_SYNC        0xF3
#define SECRET           "qQXR79UCb4*r"
#define FOB_WAKE_MS      800
#define PRESS_MS         500
#define LOCK_TO_START_MS 800
#define START_HOLD_MS    3000
#define FOB_POST_TX_MS   1000
#define RELOCK_DELAY_MS  30000

// GPS
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
double lastLat = 0;
double lastLng = 0;
bool hadFix    = false;

// State machine states
enum State {
  IDLE,
  FOB_ON,
  PRESS_LOCK,
  PRESS_UNLOCK,
  PRESS_START_LOCK,
  HOLD_START,
  RELEASE_START,
  FOB_OFF,
  SEND_ACK
};

State     currentState  = IDLE;
State     nextState     = IDLE;
String    pendingAck    = "";
bool      waitingRelock = false;
unsigned long stateTimer  = 0;
unsigned long relockTimer = 0;

void setup() {
  Serial.begin(115200);

  // Disable WiFi and Bluetooth — not needed, reduces interference and saves power
  esp_wifi_stop();
  esp_bt_controller_disable();

  pinMode(RELAY_UNLOCK, OUTPUT);
  pinMode(RELAY_LOCK,   OUTPUT);
  pinMode(RELAY_START,  OUTPUT);
  pinMode(RELAY_POWER,  OUTPUT);

  digitalWrite(RELAY_UNLOCK, HIGH);
  digitalWrite(RELAY_LOCK,   HIGH);
  digitalWrite(RELAY_START,  HIGH);
  digitalWrite(RELAY_POWER,  HIGH);

  // Turn off onboard LED
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);

  // GPS init
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS initialized");

  // LoRa init
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed");
    while (1);
  }

  LoRa.setSyncWord(LORA_SYNC);
  LoRa.setSpreadingFactor(10);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(20);

  Serial.println("Car unit ready");
}

void sendAck(String msg) {
  LoRa.beginPacket();
  LoRa.print("ACK:" + msg);
  LoRa.endPacket();
  Serial.print("ACK sent: ");
  Serial.println(msg);
}

void sendLocation() {
  if (gps.location.isValid() && gps.location.age() < 5000) {
    lastLat = gps.location.lat();
    lastLng = gps.location.lng();
    hadFix  = true;
    String loc = "GPS:" + String(lastLat, 6) + "," + String(lastLng, 6);
    LoRa.beginPacket();
    LoRa.print(loc);
    LoRa.endPacket();
    Serial.println("Location sent (fresh): " + loc);
  }
  else if (hadFix) {
    String loc = "GPS:LAST:" + String(lastLat, 6) + "," + String(lastLng, 6);
    LoRa.beginPacket();
    LoRa.print(loc);
    LoRa.endPacket();
    Serial.println("Location sent (last known): " + loc);
  }
  else {
    LoRa.beginPacket();
    LoRa.print("GPS:NOFIX");
    LoRa.endPacket();
    Serial.println("No GPS fix — no last known location");
  }
}

void startSequence(State first, String ack) {
  if (currentState != IDLE) {
    Serial.println("Busy — ignoring command");
    return;
  }
  pendingAck   = ack;
  currentState = FOB_ON;
  nextState    = first;
  stateTimer   = millis();
  digitalWrite(RELAY_POWER, LOW);
  Serial.println("FOB ON");
}

void handleCommand(String msg) {
  if (msg.indexOf(SECRET) < 0) {
    Serial.println("Wrong password");
    return;
  }

  if (msg.indexOf("UNLOCK") >= 0) {
    startSequence(PRESS_UNLOCK, "UNLOCK");
    waitingRelock = true;
    relockTimer   = millis();
  }
  else if (msg.indexOf("LOCK") >= 0) {
    startSequence(PRESS_LOCK, "LOCK");
    waitingRelock = false;
  }
  else if (msg.indexOf("CLIMATE") >= 0) {
    startSequence(PRESS_START_LOCK, "CLIMATE");
  }
  else if (msg.indexOf("WHERE") >= 0) {
    Serial.println("Location requested");
    sendLocation();
    sendAck("WHERE");
  }
  else {
    Serial.println("Unknown command");
  }
}

void runStateMachine() {
  unsigned long now = millis();

  switch (currentState) {

    case IDLE:
      if (waitingRelock && now - relockTimer >= RELOCK_DELAY_MS) {
        Serial.println("Auto-relocking");
        waitingRelock = false;
        startSequence(PRESS_LOCK, "AUTO-LOCK");
      }
      break;

    case FOB_ON:
      if (now - stateTimer >= FOB_WAKE_MS) {
        currentState = nextState;
        stateTimer   = now;
        Serial.println("FOB awake — executing command");
      }
      break;

    case PRESS_UNLOCK:
      digitalWrite(RELAY_UNLOCK, LOW);
      currentState = FOB_OFF;
      nextState    = IDLE;
      stateTimer   = now;
      Serial.println("UNLOCK pressed");
      break;

    case PRESS_LOCK:
      digitalWrite(RELAY_LOCK, LOW);
      currentState = FOB_OFF;
      nextState    = IDLE;
      stateTimer   = now;
      Serial.println("LOCK pressed");
      break;

    case PRESS_START_LOCK:
      digitalWrite(RELAY_LOCK, LOW);
      currentState = HOLD_START;
      stateTimer   = now;
      Serial.println("LOCK pressed for climate");
      break;

    case HOLD_START:
      // Release lock after PRESS_MS
      if (now - stateTimer >= PRESS_MS) {
        digitalWrite(RELAY_LOCK, HIGH);
      }
      // Press start after PRESS_MS + LOCK_TO_START_MS
      if (now - stateTimer >= PRESS_MS + LOCK_TO_START_MS) {
        digitalWrite(RELAY_START, LOW);
        currentState = RELEASE_START;
        stateTimer   = now;
        Serial.println("START held");
      }
      break;

    case RELEASE_START:
      if (now - stateTimer >= START_HOLD_MS) {
        digitalWrite(RELAY_START, HIGH);
        currentState = FOB_OFF;
        nextState    = IDLE;
        stateTimer   = now;
        Serial.println("START released");
      }
      break;

    case FOB_OFF:
      if (now - stateTimer >= PRESS_MS + FOB_POST_TX_MS) {
        digitalWrite(RELAY_UNLOCK, HIGH);
        digitalWrite(RELAY_LOCK,   HIGH);
        digitalWrite(RELAY_POWER,  HIGH);
        Serial.println("FOB OFF");
        currentState = SEND_ACK;
        stateTimer   = now;
      }
      break;

    case SEND_ACK:
      sendAck(pendingAck);
      pendingAck   = "";
      currentState = IDLE;
      Serial.println("Back to IDLE");
      break;
  }
}

void loop() {
  // Feed GPS data continuously and update last known fix
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // Keep updating last known fix whenever valid
  if (gps.location.isValid() && gps.location.age() < 5000) {
    lastLat = gps.location.lat();
    lastLng = gps.location.lng();
    hadFix  = true;
  }

  // Debug GPS fix status every 10 seconds
  static unsigned long lastGpsDebug = 0;
  if (millis() - lastGpsDebug >= 10000) {
    lastGpsDebug = millis();
    if (gps.location.isValid()) {
      Serial.print("GPS fix: ");
      Serial.print(lastLat, 6);
      Serial.print(", ");
      Serial.println(lastLng, 6);
    } else {
      Serial.print("No GPS fix — satellites: ");
      Serial.print(gps.satellites.value());
      if (hadFix) Serial.print(" (last known fix available)");
      Serial.println();
    }
  }

  // Run state machine
  runStateMachine();

  // Debug state changes
  static State lastState = IDLE;
  if (currentState != lastState) {
    Serial.print("State changed to: ");
    Serial.println(currentState);
    lastState = currentState;
  }

  // Check for incoming LoRa packets
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String msg = "";
    while (LoRa.available())
      msg += (char)LoRa.read();
    Serial.print("Received: ");
    Serial.println(msg);
    handleCommand(msg);
  }
}
