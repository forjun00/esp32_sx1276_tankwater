#pragma once
#include <RadioLib.h>
#include "config.h"

// ─── Radio objects ────────────────────────────────────────────────────────────
SX1276      radio = new Module(5, 26, 14, 35);   // NSS, DIO0, RST, DIO1
LoRaWANNode node(&radio, &AS923);

// ─── Helpers ──────────────────────────────────────────────────────────────────
String bytesToHexStr(uint8_t* b, int len) {
  String s = "";
  for (int i = 0; i < len; i++) {
    if (b[i] < 0x10) s += "0";
    s += String(b[i], HEX);
  }
  s.toUpperCase();
  return s;
}

void hexStrToBytes(String hex, uint8_t* out, int len) {
  hex.replace(" ", "");
  hex.toUpperCase();
  for (int i = 0; i < len; i++)
    out[i] = (uint8_t)strtol(hex.substring(i*2, i*2+2).c_str(), NULL, 16);
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void initLoRa() {
  Serial.print("[LoRa] Init... ");
  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("radio FAILED: " + String(state)); return;
  }
  node.beginABP(lora_devAddr, lora_nwkSKey, lora_nwkSKey, lora_nwkSKey, lora_appSKey);
  state = node.activateABP();
  if (state != RADIOLIB_ERR_NONE &&
      state != RADIOLIB_LORAWAN_NEW_SESSION &&
      state != RADIOLIB_LORAWAN_SESSION_RESTORED) {
    Serial.println("ABP FAILED: " + String(state)); return;
  }
  loraReady = true;
  Serial.println("OK  DevAddr:" + String(lora_devAddr, HEX));
}

// ─── Downlink handler ─────────────────────────────────────────────────────────
void handleDownlink(uint8_t* buf, size_t len) {
  if (len == 0) return;
  Serial.print("[Downlink] ");
  for (size_t i = 0; i < len; i++) Serial.printf("%02X ", buf[i]);
  Serial.println();

  switch (buf[0]) {
    case 0x01:
      relayState = true;
      digitalWrite(PIN_RELAY, HIGH);
      Serial.println("[Relay] ON");
      break;
    case 0x00:
      relayState = false;
      digitalWrite(PIN_RELAY, LOW);
      Serial.println("[Relay] OFF");
      break;
    case 0x02:                             // Set TX offset (server time-slotting)
      if (len >= 2) {
        uint8_t offset_sec = buf[1];
        lora_tx_offset    = (unsigned long)offset_sec * 1000UL;
        // Reschedule next TX: fire in offset_sec seconds from now
        lastLoRaTx        = millis() - lora_interval + lora_tx_offset;
        pendingTxOffset   = offset_sec;
        txOffsetNeedsSave = true;
        Serial.printf("[Downlink] TX offset set: %us  next TX in ~%us\n",
                      offset_sec, offset_sec);
      }
      break;
    default:
      Serial.println("[Downlink] Unknown cmd: 0x" + String(buf[0], HEX));
      break;
  }
}

// ─── Send uplink ──────────────────────────────────────────────────────────────
void sendLoRa(int distCM, float volt) {
  if (!loraReady) { Serial.println("[LoRa] Not ready"); return; }

  uint16_t d = (uint16_t)constrain(distCM, 0, 65535);
  uint16_t v = (uint16_t)(volt * 100.0f);
  // Payload: [dist_hi, dist_lo, volt_hi, volt_lo, tx_offset_sec]
  // Byte 4 reports current TX offset so server can confirm assignment
  uint8_t  payload[5] = { (uint8_t)(d>>8), (uint8_t)(d&0xFF),
                           (uint8_t)(v>>8), (uint8_t)(v&0xFF),
                           (uint8_t)(lora_tx_offset / 1000) };

  // Downlink buffer — passed directly into sendReceive
  uint8_t downBuf[255];
  size_t  downLen = sizeof(downBuf);

  Serial.printf("[LoRa] TX dist=%dcm volt=%.2fV offset=%lus\n",
                distCM, volt, lora_tx_offset / 1000);
  Serial.printf("[Payload] %02X %02X %02X %02X %02X\n",
                payload[0], payload[1], payload[2], payload[3], payload[4]);
  Serial.print("[LoRa] Sending ... ");
  int state = node.sendReceive(payload, sizeof(payload), 1, downBuf, &downLen);

  if (state > 0) {
    Serial.printf("OK + Downlink  FCnt:%d  downLen:%d\n", node.getFCntUp(), downLen);
    if (downLen > 0) {
      Serial.print("[Downlink RAW] ");
      for (size_t i = 0; i < downLen; i++) Serial.printf("%02X ", downBuf[i]);
      Serial.println();
      handleDownlink(downBuf, downLen);
    } else {
      Serial.println("[Downlink] MAC only (no user payload)");
    }
  } else if (state == 0) {
    Serial.println("OK  FCnt:" + String(node.getFCntUp()));
  } else {
    Serial.println("FAILED: " + String(state));
  }
}
