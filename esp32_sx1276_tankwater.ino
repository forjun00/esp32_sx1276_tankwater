/*
 * ═══════════════════════════════════════════════════════════════════
 *  Tank Water Level — LoRaWAN Firmware
 * ═══════════════════════════════════════════════════════════════════
 *  Sensor  : A01NYUB Waterproof Ultrasonic (UART)
 *  Battery : ADC pin 33
 *  Radio   : SX1276 RadioLib ABP AS923
 *  Config  : WiFi + LoRa keys via web UI (SPIFFS)
 *  Control : Relay on GPIO 25 via LoRa downlink
 * ───────────────────────────────────────────────────────────────────
 *  File structure:
 *    config.h      — pins, project info, global variables
 *    sensor.h      — A01NYUB class + battery ADC
 *    lora_module.h — LoRa init, TX, downlink handler
 *    storage.h     — SPIFFS read/write + config loader
 *    webui.h       — web server + HTML UI
 *    (this file)   — setup() + loop() only
 * ═══════════════════════════════════════════════════════════════════
 *  Libraries needed: RadioLib, Arduino_JSON
 * ═══════════════════════════════════════════════════════════════════
 */

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_sleep.h"
#include <WiFi.h>

#include "config.h"
#include "sensor.h"
#include "lora_module.h"
#include "storage.h"
#include "webui.h"

// ─── Deep sleep ───────────────────────────────────────────────────────────────
void deepSleep(uint64_t seconds) {
  Serial.println("[Sleep] " + String(seconds) + "s");
  digitalWrite(PIN_TRIGGER, LOW);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  esp_deep_sleep_start();
}

// ═══════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(PIN_TRIGGER, OUTPUT); digitalWrite(PIN_TRIGGER, HIGH);
  pinMode(PIN_RELAY,   OUTPUT); digitalWrite(PIN_RELAY,   LOW);

  Serial.println(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER ?
                 "[Boot] Wake from deep sleep" : "[Boot] First boot / reset");

  // ── Sensor init ──
  ultrasonic.begin(PIN_RX2, PIN_TX2);
  delay(200);
  for (int i = 0; i < 20; i++) { ultrasonic.read(); delay(50); }
  distanceCM  = ultrasonic.getDistance();
  sensorError = !ultrasonic.isValid();
  battVoltage = readBattery();
  Serial.printf("[Sensor] dist=%dcm (%s)  batt=%.2fV\n",
                distanceCM, ultrasonic.getStatus().c_str(), battVoltage);

  // ── SPIFFS ──
  if (!SPIFFS.begin(true)) { Serial.println("[SPIFFS] Mount failed"); return; }
  loadLoRaConfig();
  loadWiFiConfig();

  // ── WiFi ──
  Serial.print("[WiFi] Connecting to " + String(wifiSSID) + " ");
  WiFi.begin(wifiSSID, wifiPASS);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
    delay(500); Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] " + WiFi.localIP().toString());
    startWebServer();
    webStarted = true;
  } else {
    Serial.println("[WiFi] Not connected — retrying in loop");
    wifiLostAt  = millis();
    wifiWasLost = true;
  }

  // ── LoRa ──
  initLoRa();
  lastLoRaTx = millis() - lora_interval;   // TX immediately on first loop

  Serial.println("──────────────── Setup complete ────────────────");
}

// ═══════════════════════════════════════════════════════════════════
//  Loop
// ═══════════════════════════════════════════════════════════════════
void loop() {
  server.handleClient();
  ultrasonic.read();   // non-blocking, keeps serial buffer clear

  // ── Update sensor every 2s ──
  if (millis() - lastSensorRead >= 2000) {
    lastSensorRead = millis();
    distanceCM  = ultrasonic.getDistance();
    sensorError = !ultrasonic.isValid();
    battVoltage = readBattery();
  }

  if (pauseloop) return;

  // ── WiFi reconnect ──
  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiWasLost) { wifiWasLost = true; wifiLostAt = millis(); }
    if (millis() - lastWiFiCheck >= 5000) {
      lastWiFiCheck = millis();
      WiFi.begin(wifiSSID, wifiPASS);
    }
    if (WiFi.status() == WL_CONNECTED && !webStarted) {
      webStarted = true; wifiWasLost = false; startWebServer();
    }
    if (millis() - wifiLostAt >= 30000) deepSleep(300);
    return;
  } else {
    wifiWasLost = false;
  }

  // ── LoRa TX ──
  if (millis() - lastLoRaTx >= lora_interval) {
    lastLoRaTx = millis();
    for (int i = 0; i < 10; i++) { ultrasonic.read(); delay(30); }
    distanceCM  = ultrasonic.getDistance();
    sensorError = !ultrasonic.isValid();
    battVoltage = readBattery();
    sendLoRa(distanceCM, battVoltage);
    Serial.println("[TX] Next in " + String(lora_interval/1000) + "s");
  }

  // ── Debug every 10s ──
  if (millis() - lastDebugPrint >= 10000) {
    lastDebugPrint = millis();
    Serial.printf("[Status] dist=%dcm batt=%.2fV lora=%d fcnt=%d relay=%d\n",
                  distanceCM, battVoltage, loraReady,
                  node.getFCntUp(), relayState);
  }
}
