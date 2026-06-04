#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Arduino_JSON.h>
#include "config.h"
#include "lora_module.h"
#include "storage.h"

WebServer server(80);

// ─── /status — JSON for live update ──────────────────────────────────────────
void handleStatus() {
  String json = "{";
  json += "\"dist\":"    + String(distanceCM)     + ",";
  json += "\"volt\":"    + String(battVoltage, 2)  + ",";
  json += "\"sensor\":\"" + String(sensorError ? "Error" : "OK") + "\",";
  json += "\"rssi\":"    + String(WiFi.RSSI())    + ",";
  json += "\"fcnt\":"    + String(node.getFCntUp()) + ",";
  json += "\"lora\":"    + String(loraReady ? 1 : 0) + ",";
  json += "\"relay\":"   + String(relayState ? 1 : 0) + ",";
  json += "\"uptime\":"  + String(millis() / 1000) + ",";
  json += "\"pause\":"   + String(pauseloop ? 1 : 0);
  json += "}";
  server.send(200, "application/json", json);
}

// ─── / — main page ────────────────────────────────────────────────────────────
void handleRoot() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("reset"))   { server.send(200,"text/html","<script>alert('Restarting...');</script>"); delay(500); ESP.restart(); }
    if (server.hasArg("pause"))   pauseloop = true;
    if (server.hasArg("unpause")) pauseloop = false;

    if (server.hasArg("devaddr")) {
      JSONVar d;
      d["devaddr"]  = server.arg("devaddr");
      d["nwkskey"]  = server.arg("nwkskey");
      d["appskey"]  = server.arg("appskey");
      d["interval"] = server.arg("interval").toInt();
      writeFileAndRedirect(("/" + deviceSN + "_lora_config.txt").c_str(),
                           JSON.stringify(d).c_str());
      return;
    }
    if (server.hasArg("ssid")) {
      JSONVar d;
      d["ssid"]   = server.arg("ssid");
      d["pwd"]    = server.arg("pwd");
      d["dhcp"]   = server.arg("dhcp");
      d["ip"]     = server.arg("ip");
      d["gw"]     = server.arg("gw");
      d["subnet"] = server.arg("subnet");
      writeFileAndRedirect(("/" + deviceSN + "_config.txt").c_str(),
                           JSON.stringify(d).c_str());
      return;
    }
  }

  String h = F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Tank Sensor</title><style>"
    "body{font-family:Arial,sans-serif;margin:0;padding:12px;background:#f4f4f4;color:#333}"
    ".card{background:#fff;border-radius:10px;padding:16px;margin:10px auto;max-width:500px;box-shadow:0 2px 8px rgba(0,0,0,.12)}"
    "h2{margin:0 0 12px;font-size:18px;color:#1565C0}"
    "h4{margin:4px 0;font-size:13px;color:#888}"
    ".row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #f0f0f0}"
    ".label{color:#666;font-size:14px}.val{font-weight:bold;font-size:14px}"
    ".badge{display:inline-block;padding:3px 10px;border-radius:12px;font-size:12px;font-weight:bold}"
    ".ok{background:#c8e6c9;color:#1b5e20}.err{background:#ffcdd2;color:#b71c1c}"
    "button{padding:8px 14px;border:1px solid #ccc;border-radius:6px;font-size:13px;cursor:pointer;background:#f0f0f0;margin:4px}"
    "button:hover{background:#e0e0e0}.btn-red{background:#ef9a9a;border-color:#e57373}"
    "label{font-size:13px;color:#555;display:block;margin:8px 0 2px}"
    "input[type=text],input[type=password],select{width:100%;padding:8px;border:1px solid #ccc;border-radius:5px;box-sizing:border-box;font-size:13px;margin-bottom:6px}"
    "input[type=submit]{padding:9px 20px;border:none;border-radius:6px;background:#1565C0;color:#fff;font-size:14px;cursor:pointer;margin-top:6px}"
    "input[type=submit]:hover{background:#0d47a1}"
    "</style><script>"
    "function r(){fetch('/status').then(x=>x.json()).then(d=>{"
    "document.getElementById('d_dist').innerText=d.dist+' cm';"
    "document.getElementById('d_volt').innerText=d.volt+' V';"
    "document.getElementById('d_rssi').innerText=d.rssi+' dBm';"
    "document.getElementById('d_fcnt').innerText=d.fcnt;"
    "document.getElementById('d_up').innerText=d.uptime+'s';"
    "var l=document.getElementById('d_lora');l.innerText=d.lora?'Ready':'Not Ready';l.className='badge '+(d.lora?'ok':'err');"
    "var s=document.getElementById('d_sensor');s.innerText=d.sensor;s.className='badge '+(d.sensor=='OK'?'ok':'err');"
    "var rv=document.getElementById('d_relay');rv.innerText=d.relay?'ON':'OFF';rv.className='badge '+(d.relay?'ok':'warn');"
    "});}"
    "setInterval(r,2000);</script></head><body>");

  h += "<div class='card'><h2>" + Projectname + "</h2>";
  h += "<h4>" + deviceSN + " &nbsp;|&nbsp; " + FW_VERSION + "</h4>";
  h += "<form method='POST'>";
  h += "<button name='pause'   value='1'>Pause</button>";
  h += "<button name='unpause' value='1'>Resume</button>";
  h += "<button name='reset'   value='1' class='btn-red'>Restart</button>";
  h += "</form></div>";

  h += "<div class='card'><h2>Live Data</h2>";
  h += "<div class='row'><span class='label'>Distance</span><span class='val' id='d_dist'>" + String(distanceCM) + " cm</span></div>";
  h += "<div class='row'><span class='label'>Battery</span><span class='val' id='d_volt'>" + String(battVoltage,2) + " V</span></div>";
  h += "<div class='row'><span class='label'>Sensor</span><span id='d_sensor' class='badge " + String(sensorError?"err":"ok") + "'>" + String(sensorError?"Error":"OK") + "</span></div>";
  h += "<div class='row'><span class='label'>Relay</span><span id='d_relay' class='badge " + String(relayState?"ok":"warn") + "'>" + String(relayState?"ON":"OFF") + "</span></div>";
  h += "<div class='row'><span class='label'>LoRa</span><span id='d_lora' class='badge " + String(loraReady?"ok":"err") + "'>" + String(loraReady?"Ready":"Not Ready") + "</span></div>";
  h += "<div class='row'><span class='label'>AP IP</span><span class='val'>192.168.4.1</span></div>";
  h += "<div class='row'><span class='label'>STA IP</span><span class='val'>" + WiFi.localIP().toString() + "</span></div>";
  h += "<div class='row'><span class='label'>WiFi RSSI</span><span class='val' id='d_rssi'>" + String(WiFi.RSSI()) + " dBm</span></div>";
  h += "<div class='row'><span class='label'>LoRa FCnt</span><span class='val' id='d_fcnt'>" + String(node.getFCntUp()) + "</span></div>";
  h += "<div class='row'><span class='label'>TX Interval</span><span class='val'>" + String(lora_interval/1000) + " s</span></div>";
  h += "<div class='row'><span class='label'>Uptime</span><span class='val' id='d_up'>" + String(millis()/1000) + " s</span></div>";
  h += "</div>";

  if (pauseloop) {
    h += "<div class='card'><h2>LoRa Config (ABP)</h2><form method='POST'>";
    h += "<label>DevAddr</label><input type='text' name='devaddr' value='" + String(lora_devAddr,HEX) + "'>";
    h += "<label>NwkSKey</label><input type='text' name='nwkskey' value='" + bytesToHexStr(lora_nwkSKey,16) + "'>";
    h += "<label>AppSKey</label><input type='text' name='appskey' value='" + bytesToHexStr(lora_appSKey,16) + "'>";
    h += "<label>TX Interval (seconds)</label><input type='text' name='interval' value='" + String(lora_interval/1000) + "'>";
    h += "<input type='submit' value='Save LoRa Config'></form></div>";

    h += "<div class='card'><h2>WiFi Config</h2><form method='POST' id='wf'>";
    h += "<label>SSID</label><input type='text' name='ssid' value='" + String(wifiSSID) + "'>";
    h += "<label>Password</label><input type='password' name='pwd' value='" + String(wifiPASS) + "'>";

    // DHCP / Static toggle
    h += "<label>Mode</label>";
    h += "<select name='dhcp' id='dhcp_sel' onchange=\"document.getElementById('static_fields').style.display=this.value=='0'?'block':'none'\">";
    h += (useDHCP=="0") ? "<option value='1'>DHCP (auto)</option><option value='0' selected>Static IP</option>"
                        : "<option value='1' selected>DHCP (auto)</option><option value='0'>Static IP</option>";
    h += "</select>";

    // Static IP fields — hidden when DHCP
    h += "<div id='static_fields' style='display:" + String(useDHCP=="0"?"block":"none") + "'>";
    h += "<label>IP Address</label><input type='text' name='ip'     value='" + localIP.toString()   + "'>";
    h += "<label>Gateway</label>   <input type='text' name='gw'     value='" + gatewayIP.toString() + "'>";
    h += "<label>Subnet</label>    <input type='text' name='subnet' value='" + subnetIP.toString()  + "'>";
    h += "<label>DNS</label>       <input type='text' name='dns'    value='" + dnsIP.toString()     + "'>";
    h += "</div>";

    h += "<input type='submit' value='Save WiFi Config'></form></div>";

    h += "<div class='card'><h2>OTA Update</h2>";
    h += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    h += "<input type='file' name='update' style='margin:8px 0'><br>";
    h += "<input type='submit' value='Upload Firmware'></form></div>";
  }

  h += "</body></html>";
  server.send(200, "text/html", h);
}

void handleFileUpload() {
  HTTPUpload& u = server.upload();
  if      (u.status == UPLOAD_FILE_START) { Update.begin(UPDATE_SIZE_UNKNOWN); }
  else if (u.status == UPLOAD_FILE_WRITE) { Update.write(u.buf, u.currentSize); }
  else if (u.status == UPLOAD_FILE_END)   { Update.end(true); Serial.printf("OTA: %u bytes\n", u.totalSize); }
}

void startWebServer() {
  server.on("/",       handleRoot);
  server.on("/status", handleStatus);
  server.on("/update", HTTP_POST,
    []() { server.send(200,"text/html","<script>alert('Done! Rebooting...');</script>"); delay(500); ESP.restart(); },
    handleFileUpload);
  server.begin();
  Serial.println("[Web] http://" + WiFi.localIP().toString());
}
