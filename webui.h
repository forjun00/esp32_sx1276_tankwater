#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Arduino_JSON.h>
#include "config.h"
#include "lora_module.h"
#include "storage.h"

WebServer server(80);

// ─── Static CSS + JS in flash (saves heap) ───────────────────────────────────
static const char PAGE_CSS[] PROGMEM = R"(
*{box-sizing:border-box;margin:0;padding:0}
body{background:#080d18;color:#c9d8ff;font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;padding:14px}
.hdr{display:flex;justify-content:space-between;align-items:center;padding:14px 18px;background:linear-gradient(135deg,#0d1929,#0a1520);border:1px solid #1a3a5c;border-radius:12px;margin-bottom:14px}
.hdr-l .title{font-size:17px;font-weight:700;color:#58a6ff;letter-spacing:.5px}
.hdr-l .sub{font-size:11px;color:#4a6480;margin-top:3px;font-family:monospace}
.live{display:flex;align-items:center;gap:6px;font-size:11px;color:#3fb950;font-weight:600;text-transform:uppercase;letter-spacing:1px}
.dot{width:8px;height:8px;background:#3fb950;border-radius:50%;box-shadow:0 0 8px #3fb950;animation:blink 2s infinite}
@keyframes blink{0%,100%{opacity:1;box-shadow:0 0 8px #3fb950}50%{opacity:.4;box-shadow:0 0 3px #3fb950}}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px}
.g3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;margin-bottom:12px}
@media(max-width:480px){.g2,.g3{grid-template-columns:1fr}}
.card{background:linear-gradient(145deg,#0d1929,#0a1520);border:1px solid #1a3a5c;border-radius:12px;padding:16px}
.card-wide{background:linear-gradient(145deg,#0d1929,#0a1520);border:1px solid #1a3a5c;border-radius:12px;padding:16px;margin-bottom:12px}
.ctag{font-size:10px;text-transform:uppercase;letter-spacing:1.5px;color:#4a6480;margin-bottom:8px}
.cval{font-size:2.4rem;font-weight:700;color:#58a6ff;font-family:'Courier New',monospace;line-height:1}
.cval.green{color:#3fb950}.cval.red{color:#f85149}.cval.yellow{color:#d29922}
.cunit{font-size:.85rem;color:#6e86a0;margin-left:4px}
.bar-track{background:#101e2d;border-radius:4px;height:5px;margin-top:12px;overflow:hidden}
.bar-fill{height:100%;border-radius:4px;background:linear-gradient(90deg,#1f6feb,#58a6ff);transition:width .6s ease}
.bar-fill.g{background:linear-gradient(90deg,#238636,#3fb950)}
.srow{display:flex;justify-content:space-between;align-items:center;padding:9px 12px;background:#060c16;border-radius:8px;border:1px solid #111e2e;margin-bottom:7px}
.slabel{font-size:10px;color:#4a6480;text-transform:uppercase;letter-spacing:1px}
.sval{font-size:13px;font-weight:600;font-family:monospace;color:#c9d8ff}
.badge{display:inline-flex;align-items:center;gap:5px;padding:3px 10px;border-radius:20px;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.5px}
.badge::before{content:'';width:6px;height:6px;border-radius:50%}
.bok{background:#3fb95010;color:#3fb950;border:1px solid #238636}.bok::before{background:#3fb950;box-shadow:0 0 5px #3fb950}
.berr{background:#f8514910;color:#f85149;border:1px solid #6e2427}.berr::before{background:#f85149}
.bwarn{background:#d2992210;color:#d29922;border:1px solid #6e4c12}.bwarn::before{background:#d29922}
.btn-row{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}
.btn{padding:9px 16px;border-radius:8px;font-size:12px;font-weight:700;cursor:pointer;border:1px solid;text-transform:uppercase;letter-spacing:.5px;transition:all .2s}
.bblu{background:#1f6feb15;color:#58a6ff;border-color:#1f6feb}.bblu:hover{background:#1f6feb35}
.bred{background:#f8514915;color:#f85149;border-color:#6e2427}.bred:hover{background:#f8514935}
.bgrn{background:#23863615;color:#3fb950;border-color:#238636}.bgrn:hover{background:#23863635}
.sec-title{font-size:11px;text-transform:uppercase;letter-spacing:1.5px;color:#58a6ff;padding-bottom:10px;border-bottom:1px solid #1a3a5c;margin-bottom:14px}
label{font-size:11px;color:#4a6480;display:block;margin:10px 0 4px;text-transform:uppercase;letter-spacing:.5px}
input[type=text],input[type=password],select{width:100%;padding:9px 12px;background:#060c16;border:1px solid #1a3a5c;border-radius:8px;color:#c9d8ff;font-size:13px;font-family:monospace;outline:none;transition:border-color .2s}
input:focus,select:focus{border-color:#58a6ff;box-shadow:0 0 0 3px #58a6ff15}
select option{background:#0d1929}
input[type=submit]{width:100%;padding:11px;background:linear-gradient(135deg,#1f6feb,#388bfd);color:#fff;border:none;border-radius:8px;font-size:13px;font-weight:700;cursor:pointer;margin-top:12px;text-transform:uppercase;letter-spacing:1px;transition:filter .2s}
input[type=submit]:hover{filter:brightness(1.15)}
input[type=file]{color:#c9d8ff;margin:10px 0;font-size:13px}
.divider{height:1px;background:#1a3a5c;margin:14px 0}
.warn-box{background:#d2992210;border:1px solid #6e4c12;border-radius:8px;padding:10px 14px;font-size:12px;color:#d29922;margin-bottom:12px}
)";

static const char PAGE_JS[] PROGMEM = R"(
function upd(){
  fetch('/status').then(r=>r.json()).then(d=>{
    var de=document.getElementById.bind(document);
    de('v_dist').innerText=d.dist;
    de('v_volt').innerText=d.volt;
    de('v_rssi').innerText=d.rssi;
    de('v_fcnt').innerText=d.fcnt;
    de('v_up').innerText=d.uptime;
    de('v_free').innerText=d.spiffs_free;
    var bp=Math.min(100,Math.round(d.dist/750*100));
    de('bar_dist').style.width=bp+'%';
    var vp=Math.min(100,Math.round((d.volt/4.2)*100));
    de('bar_volt').style.width=vp+'%';
    var lb=de('b_lora');lb.className='badge '+(d.lora?'bok':'berr');lb.innerHTML=(d.lora?'&#9679; Ready':'&#9679; Error');
    var sb=de('b_sensor');sb.className='badge '+(d.sensor=='OK'?'bok':'berr');sb.innerHTML='&#9679; '+d.sensor;
    var rb=de('b_relay');rb.className='badge '+(d.relay?'bok':'bwarn');rb.innerHTML='&#9679; '+(d.relay?'ON':'OFF');
  }).catch(()=>{});
}
setInterval(upd,2000);
function tog(id){var e=document.getElementById(id);e.style.display=e.style.display=='none'?'block':'none';}
function chkDhcp(v){document.getElementById('static_fields').style.display=v=='0'?'block':'none';}
)";

// ─── /status ─────────────────────────────────────────────────────────────────
void handleStatus() {
  String j = "{";
  j += "\"dist\":"    + String(distanceCM)     + ",";
  j += "\"volt\":"    + String(battVoltage, 2)  + ",";
  j += "\"sensor\":\"" + String(sensorError ? "Err" : "OK") + "\",";
  j += "\"rssi\":"    + String(WiFi.RSSI())    + ",";
  j += "\"fcnt\":"    + String(node.getFCntUp()) + ",";
  j += "\"lora\":"    + String(loraReady ? 1 : 0) + ",";
  j += "\"relay\":"   + String(relayState ? 1 : 0) + ",";
  j += "\"uptime\":"  + String(millis() / 1000) + ",";
  j += "\"pause\":"   + String(pauseloop ? 1 : 0) + ",";
  j += "\"spiffs_free\":" + String(SPIFFS.totalBytes() - SPIFFS.usedBytes());
  j += "}";
  server.send(200, "application/json", j);
}

// ─── /format ─────────────────────────────────────────────────────────────────
void handleFormat() {
  SPIFFS.format();
  SPIFFS.begin(true);
  server.send(200, "text/html",
    "<html><body style='background:#080d18;color:#3fb950;font-family:monospace;padding:30px'>"
    "<h2>SPIFFS Formatted</h2><p>All config cleared.</p>"
    "<a href='/' style='color:#58a6ff'>&#8592; Back</a></body></html>");
}

// ─── / ───────────────────────────────────────────────────────────────────────
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
      writeFileAndRedirect(LORA_CFG_FILE, JSON.stringify(d).c_str());
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
      d["dns"]    = server.arg("dns");
      writeFileAndRedirect(WIFI_CFG_FILE, JSON.stringify(d).c_str());
      return;
    }
    if (server.hasArg("new_sn")) {
      saveDeviceSN(server.arg("new_sn"));
      server.sendHeader("Location", "/");
      server.send(302);
      return;
    }
  }

  // ── Compute bar widths ──
  int distPct = min(100, (int)(distanceCM * 100 / SENSOR_MAX_CM));
  int voltPct  = min(100, (int)(battVoltage / 4.2f * 100));

  String h = F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Tank Monitor</title>");
  h += F("<style>"); h += FPSTR(PAGE_CSS); h += F("</style>");
  h += F("<script>"); h += FPSTR(PAGE_JS); h += F("</script>");
  h += F("</head><body>");

  // ── Header ──
  h += "<div class='hdr'>";
  h += "<div class='hdr-l'><div class='title'>&#9670; " + Projectname + "</div>";
  h += "<div class='sub'>" + deviceSN + " &nbsp;&#124;&nbsp; " + FW_VERSION + "</div></div>";
  h += "<div class='live'><span class='dot'></span>LIVE</div>";
  h += "</div>";

  // ── Action buttons ──
  h += "<div class='btn-row'>";
  h += "<form method='POST' style='display:contents'>";
  h += "<button class='btn bblu' name='pause'   value='1'>&#9646;&#9646; Pause</button>";
  h += "<button class='btn bgrn' name='unpause' value='1'>&#9654; Resume</button>";
  h += "<button class='btn bred' name='reset'   value='1'>&#8635; Restart</button>";
  h += "</form></div>";

  // ── Big data cards ──
  h += "<div class='g2'>";

  // Distance card
  h += "<div class='card'>";
  h += "<div class='ctag'>Distance</div>";
  h += "<div><span class='cval' id='v_dist'>" + String(distanceCM) + "</span>";
  h += "<span class='cunit'>cm</span></div>";
  h += "<div class='bar-track'><div class='bar-fill' id='bar_dist' style='width:" + String(distPct) + "%'></div></div>";
  h += "</div>";

  // Battery card
  h += "<div class='card'>";
  h += "<div class='ctag'>Battery</div>";
  h += "<div><span class='cval green' id='v_volt'>" + String(battVoltage, 2) + "</span>";
  h += "<span class='cunit'>V</span></div>";
  h += "<div class='bar-track'><div class='bar-fill g' id='bar_volt' style='width:" + String(voltPct) + "%'></div></div>";
  h += "</div>";

  h += "</div>"; // end g2

  // ── Status rows ──
  h += "<div class='card-wide'>";
  h += "<div class='ctag'>System Status</div>";

  h += "<div class='srow'><span class='slabel'>Sensor</span>"
       "<span id='b_sensor' class='badge " + String(sensorError?"berr":"bok") + "'>"
       "&#9679; " + String(sensorError?"Err":"OK") + "</span></div>";

  h += "<div class='srow'><span class='slabel'>LoRa</span>"
       "<span id='b_lora' class='badge " + String(loraReady?"bok":"berr") + "'>"
       "&#9679; " + String(loraReady?"Ready":"Error") + "</span></div>";

  h += "<div class='srow'><span class='slabel'>Relay</span>"
       "<span id='b_relay' class='badge " + String(relayState?"bok":"bwarn") + "'>"
       "&#9679; " + String(relayState?"ON":"OFF") + "</span></div>";

  h += "<div class='srow'><span class='slabel'>WiFi RSSI</span>"
       "<span class='sval' id='v_rssi'>" + String(WiFi.RSSI()) + " dBm</span></div>";

  h += "<div class='srow'><span class='slabel'>LoRa FCnt</span>"
       "<span class='sval' id='v_fcnt'>" + String(node.getFCntUp()) + "</span></div>";

  h += "<div class='srow'><span class='slabel'>TX Interval</span>"
       "<span class='sval'>" + String(lora_interval / 1000) + " s</span></div>";

  h += "<div class='srow'><span class='slabel'>AP IP</span>"
       "<span class='sval'>" + WiFi.softAPIP().toString() + "</span></div>";

  h += "<div class='srow'><span class='slabel'>STA IP</span>"
       "<span class='sval'>" + WiFi.localIP().toString() + "</span></div>";

  h += "<div class='srow'><span class='slabel'>Uptime</span>"
       "<span class='sval' id='v_up'>" + String(millis()/1000) + " s</span></div>";

  h += "<div class='srow'><span class='slabel'>SPIFFS Free</span>"
       "<span class='sval' id='v_free'>" + String(SPIFFS.totalBytes()-SPIFFS.usedBytes()) + " B</span></div>";

  h += "</div>"; // end card-wide

  if (pauseloop) {
    h += "<div class='divider'></div>";

    // ── LoRa Config ──
    h += "<div class='card-wide'><p class='sec-title'>&#9670; LoRa Config (ABP)</p>";
    h += "<form method='POST'>";
    h += "<label>DevAddr</label><input type='text' name='devaddr' placeholder='01020304' value='" + String(lora_devAddr,HEX) + "'>";
    h += "<label>NwkSKey</label><input type='text' name='nwkskey' placeholder='32 hex chars' value='" + bytesToHexStr(lora_nwkSKey,16) + "'>";
    h += "<label>AppSKey</label><input type='text' name='appskey' placeholder='32 hex chars' value='" + bytesToHexStr(lora_appSKey,16) + "'>";
    h += "<label>TX Interval (seconds)</label><input type='text' name='interval' value='" + String(lora_interval/1000) + "'>";
    h += "<input type='submit' value='Save LoRa Config'></form></div>";

    // ── WiFi Config ──
    h += "<div class='card-wide'><p class='sec-title'>&#9670; WiFi Config</p>";
    h += "<form method='POST'>";
    h += "<label>SSID</label><input type='text' name='ssid' value='" + String(wifiSSID) + "'>";
    h += "<label>Password</label><input type='password' name='pwd' value='" + String(wifiPASS) + "'>";
    h += "<label>Mode</label><select name='dhcp' onchange='chkDhcp(this.value)'>";
    h += (useDHCP=="0") ? "<option value='1'>DHCP (auto)</option><option value='0' selected>Static IP</option>"
                        : "<option value='1' selected>DHCP (auto)</option><option value='0'>Static IP</option>";
    h += "</select>";
    h += "<div id='static_fields' style='display:" + String(useDHCP=="0"?"block":"none") + "'>";
    h += "<label>IP Address</label><input type='text' name='ip'     value='" + localIP.toString()   + "'>";
    h += "<label>Gateway</label>   <input type='text' name='gw'     value='" + gatewayIP.toString() + "'>";
    h += "<label>Subnet</label>    <input type='text' name='subnet' value='" + subnetIP.toString()  + "'>";
    h += "<label>DNS</label>       <input type='text' name='dns'    value='" + dnsIP.toString()     + "'>";
    h += "</div>";
    h += "<input type='submit' value='Save WiFi Config'></form></div>";

    // ── Device SN ──
    h += "<div class='card-wide'><p class='sec-title'>&#9670; Device Serial Number</p>";
    h += "<form method='POST'>";
    h += "<label>Serial Number</label><input type='text' name='new_sn' value='" + deviceSN + "'>";
    h += "<input type='submit' value='Save Serial Number'></form></div>";

    // ── OTA ──
    h += "<div class='card-wide'><p class='sec-title'>&#9670; OTA Firmware Update</p>";
    h += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    h += "<input type='file' name='update' accept='.bin'><br>";
    h += "<input type='submit' value='Upload Firmware'></form></div>";

    // ── SPIFFS ──
    h += "<div class='card-wide'><p class='sec-title'>&#9670; Storage</p>";
    h += "<div class='srow'><span class='slabel'>Total</span><span class='sval'>" + String(SPIFFS.totalBytes()) + " B</span></div>";
    h += "<div class='srow'><span class='slabel'>Used</span><span class='sval'>" + String(SPIFFS.usedBytes()) + " B</span></div>";
    h += "<br><div class='warn-box'>&#9888; Format erases all saved config</div>";
    h += "<a href='/format' onclick=\"return confirm('Format SPIFFS? All config will be erased!')\">";
    h += "<button class='btn bred' type='button' style='width:100%'>Format SPIFFS</button></a></div>";
  }

  h += "</body></html>";
  server.send(200, "text/html", h);
}

// ─── OTA upload ───────────────────────────────────────────────────────────────
void handleFileUpload() {
  HTTPUpload& u = server.upload();
  if      (u.status == UPLOAD_FILE_START) { Update.begin(UPDATE_SIZE_UNKNOWN); }
  else if (u.status == UPLOAD_FILE_WRITE) { Update.write(u.buf, u.currentSize); }
  else if (u.status == UPLOAD_FILE_END)   { Update.end(true); }
}

void startWebServer() {
  server.on("/",       handleRoot);
  server.on("/status", handleStatus);
  server.on("/format", handleFormat);
  server.on("/update", HTTP_POST,
    []() {
      server.send(200, "text/html",
        "<html><body style='background:#080d18;color:#3fb950;font-family:monospace;padding:30px'>"
        "<h2>&#10003; Upload Complete</h2><p>Rebooting...</p></body></html>");
      delay(500); ESP.restart();
    }, handleFileUpload);
  server.begin();
  Serial.println("[Web] http://" + WiFi.softAPIP().toString());
}
