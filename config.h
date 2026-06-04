#pragma once

// ─── Pins ─────────────────────────────────────────────────────────────────────
#define PIN_RX2      16    // A01NYUB TX → ESP32 RX2
#define PIN_TX2      17    // A01NYUB RX → ESP32 TX2
#define PIN_BATT     33    // Battery ADC
#define PIN_TRIGGER  22    // Sensor power trigger
#define PIN_RELAY    25    // Relay (HIGH=ON, LOW=OFF)

// ─── Sensor limits ────────────────────────────────────────────────────────────
#define SENSOR_MIN_CM  28
#define SENSOR_MAX_CM 750

// ─── Project info ─────────────────────────────────────────────────────────────
const String Projectname = "Tank Water Level";
const String FW_VERSION  = "v2.0-LoRa";
const String deviceSN    = "SI17507378760841";

// ─── LoRa default keys (overridden by SPIFFS) ────────────────────────────────
uint32_t lora_devAddr     = 0x01020304;
uint8_t  lora_nwkSKey[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                              0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
uint8_t  lora_appSKey[16] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0xAA,0xBB,
                              0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0xAA,0xBB};
unsigned long lora_interval = 120000;   // ms

// ─── WiFi defaults (overridden by SPIFFS) ─────────────────────────────────────
char      wifiSSID[32] = "AsefaIoT";
char      wifiPASS[32] = "Asf026867766";
IPAddress localIP(172, 16, 110, 134);
IPAddress gatewayIP(172, 16, 110, 254);
IPAddress subnetIP(255, 255, 255, 0);
IPAddress dnsIP(8, 8, 8, 8);
String    useDHCP = "0";

// ─── Runtime state ────────────────────────────────────────────────────────────
bool          loraReady      = false;
bool          relayState     = false;
bool          pauseloop      = false;
bool          webStarted     = false;
bool          wifiWasLost    = false;
unsigned long lastLoRaTx     = 0;
unsigned long lastSensorRead = 0;
unsigned long lastWiFiCheck  = 0;
unsigned long lastDebugPrint = 0;
unsigned long wifiLostAt     = 0;

// ─── Sensor readings ──────────────────────────────────────────────────────────
int   distanceCM  = 0;
float battVoltage = 0.0;
bool  sensorError = false;
