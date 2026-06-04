#pragma once
#include "config.h"

// ═══════════════════════════════════════════════════════════════════
//  A01NYUB Waterproof Ultrasonic Sensor
//  Protocol: 0xFF | Hi | Lo | Checksum  @ 9600 baud UART
// ═══════════════════════════════════════════════════════════════════
class A01NYUB {
private:
  HardwareSerial* _serial;
  int    _distance = 0;
  String _status   = "Init";

public:
  A01NYUB(HardwareSerial* serial) : _serial(serial) {}

  void begin(int rxPin, int txPin) {
    _serial->begin(9600, SERIAL_8N1, rxPin, txPin);
    Serial.println("[Sensor] A01NYUB ready  RX=" + String(rxPin) +
                   "  TX=" + String(txPin));
  }

  // Non-blocking — call every loop tick
  bool read() {
    if (_serial->available() < 4) return false;

    uint8_t b0 = _serial->read();
    if (b0 != 0xFF) return false;

    uint8_t b1 = _serial->read();
    uint8_t b2 = _serial->read();
    uint8_t b3 = _serial->read();

    if (((b0 + b1 + b2) & 0xFF) != b3) {
      Serial.println("[Sensor] Checksum error");
      return false;
    }

    _distance = ((b1 << 8) | b2) / 10;   // mm → cm

    if      (_distance < SENSOR_MIN_CM) _status = "Too Close";
    else if (_distance > SENSOR_MAX_CM) _status = "Out of Range";
    else                                _status = "OK";

    return true;
  }

  int    getDistance() { return _distance; }
  String getStatus()   { return _status;   }
  bool   isValid()     { return _status == "OK"; }
};

// ─── Battery voltage (ADC averaging) ─────────────────────────────────────────
float readBattery(int pin = PIN_BATT, int samples = 5) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return ((float)sum / samples / 4095.0f) * 11.26f;
}

// ─── Global sensor objects ────────────────────────────────────────────────────
HardwareSerial sensorSerial(2);
A01NYUB        ultrasonic(&sensorSerial);
