#pragma once

// ─── Pins ─────────────────────────────────────────────────────────────────────
#define PIN_RX2      16    // A01NYUB TX → ESP32 RX2
#define PIN_TX2      17    // A01NYUB RX → ESP32 TX2
#define PIN_BATT     33    // Battery ADC
#define PIN_TRIGGER  22    // Sensor power trigger
#define PIN_RELAY    25    // Relay (HIGH=ON, LOW=OFF)

// ─── Sensor limits ────────────────────────────────────────────────────────────
// Sensor is mounted 30 cm above water surface.
// Full tank  → sensor reads ~30 cm  (SENSOR_MIN_CM)
// Empty tank → sensor reads ~230 cm (30 cm mount + 200 cm water depth)
#define SENSOR_MIN_CM   30   // distance when tank FULL  (cm)
#define SENSOR_MAX_CM  230   // distance when tank EMPTY (cm)

// ─── Project info ─────────────────────────────────────────────────────────────
const String Projectname = "Tank Water Level";
const String FW_VERSION  = "v2.0-LoRa";
String       deviceSN    = "SI17507378760841";   // editable via web config

// ─── LoRa default keys (overridden by SPIFFS) ────────────────────────────────
uint32_t lora_devAddr     = 0x01020304;
uint8_t  lora_nwkSKey[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                              0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
uint8_t  lora_appSKey[16] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0xAA,0xBB,
                              0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0xAA,0xBB};
unsigned long lora_interval = 120000;   // ms  (overridden by SPIFFS/web UI)

// ─── AP mode (always on — no router needed) ───────────────────────────────────
const char* AP_SSID = "Tank-Config";   // hotspot name
const char* AP_PASS = "12345678";      // hotspot password (min 8 chars)
//                                        connect → http://192.168.4.1

// ─── WiFi STA defaults (overridden by SPIFFS) ─────────────────────────────────
char      wifiSSID[32] = "";           // empty = skip STA, AP only
char      wifiPASS[32] = "";
IPAddress localIP(192, 168, 1, 100);
IPAddress gatewayIP(192, 168, 1, 1);
IPAddress subnetIP(255, 255, 255, 0);
IPAddress dnsIP(8, 8, 8, 8);
String    useDHCP   = "1";             // 1=DHCP, 0=Static

// ─── TX offset (assigned by server via downlink 0x02) ─────────────────────────
unsigned long lora_tx_offset    = 0;     // ms — delay first TX after boot
bool          txOffsetNeedsSave = false; // flag: save offset to SPIFFS in loop()
uint8_t       pendingTxOffset   = 0;     // seconds — pending save value

// ─── Runtime state ────────────────────────────────────────────────────────────
bool          loraReady      = false;
bool          relayState     = false;
bool          pauseloop      = false;
bool          webStarted     = false;
unsigned long lastLoRaTx     = 0;
unsigned long lastSensorRead = 0;
unsigned long lastWiFiCheck  = 0;
unsigned long lastDebugPrint = 0;

// ─── Sensor readings ──────────────────────────────────────────────────────────
int   distanceCM  = 0;
float battVoltage = 0.0;
bool  sensorError = false;
