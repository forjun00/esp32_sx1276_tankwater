#pragma once
#include "FS.h"
#include "SPIFFS.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Arduino_JSON.h>
#include "config.h"
#include "lora_module.h"

extern WebServer server;   // defined in webui.h

// ─── Read / Write ─────────────────────────────────────────────────────────────
String readFile(const char* path) {
  File f = SPIFFS.open(path);
  if (!f || f.isDirectory()) return "";
  String out;
  while (f.available()) out += (char)f.read();
  f.close();
  return out;
}

void writeFileAndRedirect(const char* path, const char* content) {
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) { server.send(500, "text/plain", "Write failed"); return; }
  f.print(content);
  f.close();
  Serial.println("[SPIFFS] Saved: " + String(path));
  String url  = "http://" + WiFi.localIP().toString() + "/";
  String html = "<script>alert('Saved!');location.replace('" + url + "');</script>";
  server.send(200, "text/html", html);
}

// ─── Load LoRa config ─────────────────────────────────────────────────────────
void loadLoRaConfig() {
  String content = readFile(("/" + deviceSN + "_lora_config.txt").c_str());
  if (content == "") { Serial.println("[LoRa] No config — using defaults"); return; }

  JSONVar obj = JSON.parse(content);
  if (JSON.typeof(obj) == "undefined") return;

  if (obj.hasOwnProperty("devaddr"))
    lora_devAddr = strtoul((const char*)obj["devaddr"], NULL, 16);
  if (obj.hasOwnProperty("nwkskey"))
    hexStrToBytes((const char*)obj["nwkskey"], lora_nwkSKey, 16);
  if (obj.hasOwnProperty("appskey"))
    hexStrToBytes((const char*)obj["appskey"], lora_appSKey, 16);
  if (obj.hasOwnProperty("interval"))
    lora_interval = (unsigned long)((int)obj["interval"]) * 1000UL;

  Serial.printf("[LoRa] Config → DevAddr:%08X  Interval:%lus\n",
                lora_devAddr, lora_interval / 1000);
}

// ─── Load WiFi config ─────────────────────────────────────────────────────────
void loadWiFiConfig() {
  String content = readFile(("/" + deviceSN + "_config.txt").c_str());
  if (content == "") return;

  JSONVar obj = JSON.parse(content);
  if (JSON.typeof(obj) == "undefined") return;

  String s_ssid   = obj.hasOwnProperty("ssid")   ? (const char*)obj["ssid"]   : "";
  String s_pwd    = obj.hasOwnProperty("pwd")     ? (const char*)obj["pwd"]    : "";
  String s_dhcp   = obj.hasOwnProperty("dhcp")    ? (const char*)obj["dhcp"]   : "0";
  String s_ip     = obj.hasOwnProperty("ip")      ? (const char*)obj["ip"]     : "";
  String s_gw     = obj.hasOwnProperty("gw")      ? (const char*)obj["gw"]     : "";
  String s_subnet = obj.hasOwnProperty("subnet")  ? (const char*)obj["subnet"] : "";

  useDHCP = s_dhcp;
  if (s_ssid != "") s_ssid.toCharArray(wifiSSID, sizeof(wifiSSID));
  if (s_pwd  != "") s_pwd.toCharArray(wifiPASS,  sizeof(wifiPASS));

  if (s_dhcp == "0" && s_ip != "") {
    localIP.fromString(s_ip);
    gatewayIP.fromString(s_gw);
    subnetIP.fromString(s_subnet);
    WiFi.config(localIP, gatewayIP, subnetIP, dnsIP, dnsIP);
  }
}
