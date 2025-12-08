#include "WebUI.h"
#include "Greenhouse.h"

#include <WebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <DNSServer.h>

// Single global web server (port 80)
static WebServer server(80);

// DNS server for captive portal
static DNSServer dnsServer;
static bool      sCaptivePortalActive = false;

// Web UI Basic Auth credentials (loaded from NVS)
static String sWebAuthUser;
static String sWebAuthPass;

// ================= Helpers =================

static String htmlBool(bool b) { return b ? "ON" : "OFF"; }
static String htmlAuto(bool a) { return a ? "AUTO" : "MAN"; }

// Convert "HH:MM" to minutes since midnight
static int parseTimeToMinutes(const String &s, int fallback) {
  int colon = s.indexOf(':');
  if (colon < 0) return fallback;
  int h = s.substring(0, colon).toInt();
  int m = s.substring(colon + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

// In AP-only (captive portal) mode, we skip authentication so onboarding is open.
// In STA mode, all protected endpoints require Basic Auth, unless username is empty.
static bool requireAuth() {
  if (sCaptivePortalActive) {
    // Captive/AP mode: no auth required
    return true;
  }

  // If username is empty, auth is disabled
  if (sWebAuthUser.length() == 0) {
    return true;
  }

  if (!server.authenticate(sWebAuthUser.c_str(), sWebAuthPass.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// ================= History API =================

static void handleHistoryApi() {
  if (!requireAuth()) return;

  String json;
  json.reserve(20000);
  json += "{ \"points\":[";

  size_t count = gHistoryFull ? HISTORY_SIZE : gHistoryIndex;
  bool first   = true;

  for (size_t i = 0; i < count; ++i) {
    size_t idx = gHistoryFull ? ((gHistoryIndex + i) % HISTORY_SIZE) : i;
    HistorySample &s = gHistoryBuf[idx];

    if (!first) json += ",";
    first = false;

    json += "{";

    json += "\"t\":";
    json += String((unsigned long)s.timestamp);

    json += ",\"temp\":";
    if (isnan(s.temp)) json += "null";
    else json += String(s.temp, 1);

    json += ",\"hum\":";
    if (isnan(s.hum)) json += "null";
    else json += String(s.hum, 0);

    json += ",\"l1\":";
    json += s.light1 ? "1" : "0";
    json += ",\"l2\":";
    json += s.light2 ? "1" : "0";

    json += "}";
  }

  json += "]}";
  server.send(200, "application/json", json);
}

// ================= Static Chart.js from LittleFS =================

static void handleChartJs() {
  // Chart.js is not sensitive; no auth required even in STA mode
  File f = LittleFS.open("/chart.umd.min.js", "r");
  if (!f) {
    server.send(404, "text/plain", "chart.umd.min.js not found");
    return;
  }
  server.streamFile(f, "application/javascript");
  f.close();
}

// ================= Wi-Fi configuration page =================

static void handleWifiConfigGet() {
  if (!requireAuth()) return;

  String storedSsid, storedPass;
  loadWifiCredentials(storedSsid, storedPass);

  // Scan Wi-Fi networks
  int n = WiFi.scanNetworks();
  Serial.print("[WiFi] Scan found ");
  Serial.print(n);
  Serial.println(" networks");

  String page;
  page.reserve(9000);

  page += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>Wi-Fi Configuration</title>";
  page += "<style>";
  page += "body{font-family:sans-serif;margin:1rem;}";
  page += "label{display:block;margin-top:0.5rem;}";
  page += "input{width:100%;max-width:280px;padding:0.2rem;}";
  page += "button{padding:0.4rem 0.8rem;margin-top:0.5rem;}";
  page += ".card{border:1px solid #ccc;padding:0.5rem;margin-bottom:0.5rem;border-radius:4px;}";
  page += "table{border-collapse:collapse;width:100%;}";
  page += "th,td{border:1px solid #ccc;padding:0.3rem;font-size:0.9rem;}";
  page += "tr.ssid-row:hover{background:#eef;cursor:pointer;}";
  page += "</style>";
  page += "<script>";
  page += "function setSsid(v){ document.getElementById('ssid').value=v; }";
  page += "</script>";
  page += "</head><body>";

  page += "<h1>Wi-Fi Settings</h1>";
  if (!sCaptivePortalActive) {
    page += "<p><a href='/'><button>Back</button></a></p>";
  }

  // Current connection status
  page += "<div class='card'><h2>Current connection</h2><p>";
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    page += "Connected to <b>";
    page += WiFi.SSID();
    page += "</b> (RSSI ";
    page += String(WiFi.RSSI());
    page += " dBm), IP ";
    page += WiFi.localIP().toString();
  } else {
    page += "Not connected.";
  }
  page += "</p></div>";

  // Config form
  page += "<div class='card'><h2>Configure Wi-Fi</h2>";
  page += "<form method='POST' action='/wifi'>";
  page += "<label>SSID:<br>";
  page += "<input type='text' id='ssid' name='ssid' value='";
  page += storedSsid;
  page += "'></label>";
  page += "<label>Password:<br>";
  page += "<input type='password' name='pass' value='";
  page += storedPass;
  page += "'></label>";
  page += "<p><small>Password is stored in ESP32 NVS (not encrypted). "
          "After saving, the device will reboot and try to connect.</small></p>";
  page += "<button type='submit'>Save &amp; Reboot</button>";
  page += "</form></div>";

  // Scan results
  page += "<div class='card'><h2>Available networks</h2>";
  if (n <= 0) {
    page += "<p>No networks found.</p>";
  } else {
    page += "<p>Click a row to copy SSID into the form.</p>";
    page += "<table><tr><th>SSID</th><th>RSSI (dBm)</th><th>Enc</th></tr>";
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      wifi_auth_mode_t enc = WiFi.encryptionType(i);

      page += "<tr class='ssid-row' onclick=\"setSsid('";
      // escape single quotes in SSID
      String esc = ssid;
      esc.replace("'", "\\'");
      page += esc;
      page += "')\"><td>";
      page += ssid;
      page += "</td><td>";
      page += String(rssi);
      page += "</td><td>";
      if (enc == WIFI_AUTH_OPEN) page += "open";
      else page += "secured";
      page += "</td></tr>";
    }
    page += "</table>";
  }
  page += "</div>";

  page += "</body></html>";

  server.send(200, "text/html", page);

  // Free scan results
  WiFi.scanDelete();
}

static void handleWifiConfigPost() {
  if (!requireAuth()) return;

  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Missing ssid");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";

  ssid.trim();
  pass.trim();

  saveWifiCredentials(ssid, pass);

  String page;
  page.reserve(1024);
  page += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>Wi-Fi Saved</title></head><body>";
  page += "<h1>Wi-Fi configuration saved</h1>";
  page += "<p>SSID: <b>";
  page += ssid;
  page += "</b></p>";
  page += "<p>The device will reboot now and attempt to connect.</p>";
  page += "</body></html>";

  server.send(200, "text/html", page);

  // Small delay to allow the response to be sent
  delay(500);
  ESP.restart();
}

// ================= Status page (/) =================

static void handleRoot() {
  if (!requireAuth()) return;

  String page;
  page.reserve(12000);

  struct tm nowTime;
  bool timeAvail;
  greenhouseGetTime(nowTime, timeAvail);

  page += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>Greenhouse Controller</title>";
  page += "<style>";
  page += "body{font-family:sans-serif;margin:1rem;}";
  page += "h1{font-size:1.2rem;}";
  page += ".card{border:1px solid #ccc;padding:0.5rem;margin-bottom:0.5rem;border-radius:4px;}";
  page += "button{padding:0.4rem 0.8rem;margin:0.1rem;}";
  page += "a.btn{display:inline-block;margin:0.2rem 0;}";
  page += "</style></head><body>";

  page += "<h1>Greenhouse Controller</h1>";
  page += "<p><a class='btn' href='/config'><button>Configuration</button></a></p>";
  page += "<p><a class='btn' href='/wifi'><button>Wi-Fi Settings</button></a></p>";

  if (timeAvail) {
    char buf[32];
    sprintf(buf, "%02d:%02d:%02d", nowTime.tm_hour, nowTime.tm_min, nowTime.tm_sec);
    page += String("<p>Time: ") + buf + "</p>";
  } else {
    page += "<p>Time: syncing...</p>";
  }

  // Sensors
  page += "<div class='card'><h2>Sensors</h2><ul>";
  page += "<li>Temperature: ";
  if (!isnan(gSensors.temperatureC)) page += String(gSensors.temperatureC, 1) + " &deg;C";
  else page += "N/A";
  page += "</li>";

  page += "<li>Humidity: ";
  if (!isnan(gSensors.humidityRH)) page += String(gSensors.humidityRH, 0) + " %";
  else page += "N/A";
  page += "</li>";

  page += "<li>Soil 1: " + String(gSensors.soil1Percent) + " %</li>";
  page += "<li>Soil 2: " + String(gSensors.soil2Percent) + " %</li>";
  page += "</ul></div>";

  // Relays & modes
  page += "<div class='card'><h2>Relays</h2>";

  // Light 1
  page += "<p>Light 1: " + htmlBool(gRelays.light1) + " (" + htmlAuto(gConfig.light1.enabled) + ") ";
  if (gConfig.light1.enabled) {
    page += "<a href='/mode?id=light1&auto=0'><button>Switch to MANUAL</button></a>";
  } else {
    page += "<a href='/toggle?id=light1'><button>Toggle</button></a>";
    page += "<a href='/mode?id=light1&auto=1'><button>Switch to AUTO</button></a>";
  }
  page += "<br><small>Schedule: ";
  page += minutesToTimeStr(gConfig.light1.onMinutes) + "–" + minutesToTimeStr(gConfig.light1.offMinutes) + "</small></p>";

  // Light 2
  page += "<p>Light 2: " + htmlBool(gRelays.light2) + " (" + htmlAuto(gConfig.light2.enabled) + ") ";
  if (gConfig.light2.enabled) {
    page += "<a href='/mode?id=light2&auto=0'><button>Switch to MANUAL</button></a>";
  } else {
    page += "<a href='/toggle?id=light2'><button>Toggle</button></a>";
    page += "<a href='/mode?id=light2&auto=1'><button>Switch to AUTO</button></a>";
  }
  page += "<br><small>Schedule: ";
  page += minutesToTimeStr(gConfig.light2.onMinutes) + "–" + minutesToTimeStr(gConfig.light2.offMinutes) + "</small></p>";

  // Fan
  page += "<p>Fan: " + htmlBool(gRelays.fan) + " (" + htmlAuto(gConfig.autoFan) + ") ";
  if (gConfig.autoFan) {
    page += "<a href='/mode?id=fan&auto=0'><button>Switch to MANUAL</button></a>";
  } else {
    page += "<a href='/toggle?id=fan'><button>Toggle</button></a>";
    page += "<a href='/mode?id=fan&auto=1'><button>Switch to AUTO</button></a>";
  }
  page += "</p>";

  // Pump
  page += "<p>Pump: " + htmlBool(gRelays.pump) + " (" + htmlAuto(gConfig.autoPump) + ") ";
  if (gConfig.autoPump) {
    page += "<a href='/mode?id=pump&auto=0'><button>Switch to MANUAL</button></a>";
  } else {
    page += "<a href='/toggle?id=pump'><button>Toggle</button></a>";
    page += "<a href='/mode?id=pump&auto=1'><button>Switch to AUTO</button></a>";
  }
  page += "</p>";

  page += "<p><small>Fan: ON &ge; " + String(gConfig.env.fanOnTemp, 1) +
          " &deg;C or &ge; " + String(gConfig.env.fanHumOn) +
          "% RH; OFF when &le; " + String(gConfig.env.fanOffTemp, 1) +
          " &deg;C and &le; " + String(gConfig.env.fanHumOff) +
          "% RH. Pump: dry &lt; " + String(gConfig.env.soilDryThreshold) +
          "%, wet &gt; " + String(gConfig.env.soilWetThreshold) + "%.</small></p>";

  page += "</div>";

  // History card
  page += "<div class='card'><h2>History (last 24 h)</h2>";
  page += "<p>Temperature, humidity and light states (logged every minute).</p>";
  page += "<canvas id='tempHumChart' height='150'></canvas>";
  page += "<canvas id='lightChart' style='margin-top:1rem;' height='120'></canvas>";
  page += "</div>";

  // Load Chart.js locally from LittleFS
  page += "<script src='/chart.umd.min.js'></script>";

  // Chart initialization script
  page += "<script>";
  page += "function loadHistory(){";
  page += "fetch('/api/history').then(r=>r.json()).then(d=>{";
  page += "const pts=d.points||[];";
  page += "if(!pts.length){return;}";

  page += "const labels=pts.map((p,idx)=>{";
  page += " if(p.t&&p.t>0){";
  page += "  const dt=new Date(p.t*1000);";
  page += "  return dt.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});";
  page += " } else { return idx.toString(); }";
  page += "});";

  page += "const temps=pts.map(p=>p.temp);";
  page += "const hums=pts.map(p=>p.hum);";
  page += "const l1=pts.map(p=>p.l1);";
  page += "const l2=pts.map(p=>p.l2);";

  // Temp/Hum chart
  page += "const ctx1=document.getElementById('tempHumChart').getContext('2d');";
  page += "new Chart(ctx1,{type:'line',data:{labels:labels,datasets:[";
  page += "{label:'Temperature (°C)',data:temps,borderColor:'red',backgroundColor:'rgba(255,0,0,0.1)',tension:0.2,yAxisID:'y'},";
  page += "{label:'Humidity (%)',data:hums,borderColor:'blue',backgroundColor:'rgba(0,0,255,0.1)',tension:0.2,yAxisID:'y1'}";
  page += "]},options:{responsive:true,interaction:{mode:'index',intersect:false},stacked:false,plugins:{legend:{display:true}},scales:{";
  page += "y:{type:'linear',position:'left',title:{display:true,text:'Temperature (°C)'}},";
  page += "y1:{type:'linear',position:'right',title:{display:true,text:'Humidity (%)'},grid:{drawOnChartArea:false}}";
  page += "}}});";

  // Light chart
  page += "const ctx2=document.getElementById('lightChart').getContext('2d');";
  page += "new Chart(ctx2,{type:'line',data:{labels:labels,datasets:[";
  page += "{label:'Light 1',data:l1,stepped:true,borderColor:'green',backgroundColor:'rgba(0,255,0,0.1)'},";
  page += "{label:'Light 2',data:l2,stepped:true,borderColor:'orange',backgroundColor:'rgba(255,165,0,0.1)'}";
  page += "]},options:{responsive:true,interaction:{mode:'index',intersect:false},plugins:{legend:{display:true}},scales:{";
  page += "y:{min:-0.1,max:1.1,ticks:{stepSize:1},title:{display:true,text:'Light state (0=OFF,1=ON)'}},";
  page += "x:{ticks:{maxTicksLimit:12}}";
  page += "}}});";

  page += "}).catch(e=>{console.error(e);});";   // end fetch
  page += "}";                                   // end loadHistory
  page += "window.addEventListener('load', loadHistory);";
  page += "</script>";

  page += "</body></html>";

  server.send(200, "text/html", page);
}

// ================= Relay toggle & mode =================

static void handleToggle() {
  if (!requireAuth()) return;

  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Missing id");
    return;
  }
  String id = server.arg("id");

  if (id == "light1" && !gConfig.light1.enabled) {
    gRelays.light1 = !gRelays.light1;
  } else if (id == "light2" && !gConfig.light2.enabled) {
    gRelays.light2 = !gRelays.light2;
  } else if (id == "fan" && !gConfig.autoFan) {
    gRelays.fan = !gRelays.fan;
  } else if (id == "pump" && !gConfig.autoPump) {
    gRelays.pump = !gRelays.pump;
  }

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

static void handleMode() {
  if (!requireAuth()) return;

  if (!server.hasArg("id") || !server.hasArg("auto")) {
    server.send(400, "text/plain", "Missing args");
    return;
  }
  String id   = server.arg("id");
  bool autoOn = (server.arg("auto") == "1");

  if      (id == "fan")    gConfig.autoFan        = autoOn;
  else if (id == "pump")   gConfig.autoPump       = autoOn;
  else if (id == "light1") gConfig.light1.enabled = autoOn;
  else if (id == "light2") gConfig.light2.enabled = autoOn;

  saveConfig();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ================= Config page =================

static void handleConfigGet() {
  if (!requireAuth()) return;

  String page;
  page.reserve(7000);

  page += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>Greenhouse Config</title>";
  page += "<style>";
  page += "body{font-family:sans-serif;margin:1rem;}";
  page += "label{display:block;margin-top:0.5rem;}";
  page += "input{width:100%;max-width:220px;padding:0.2rem;}";
  page += "button{padding:0.4rem 0.8rem;margin-top:0.5rem;}";
  page += ".card{border:1px solid #ccc;padding:0.5rem;margin-bottom:0.5rem;border-radius:4px;}";
  page += "</style></head><body>";

  page += "<h1>Configuration</h1>";
  page += "<p><a href='/'><button>Back</button></a></p>";

  page += "<div class='card'><h2>Environment thresholds</h2>";
  page += "<form method='POST' action='/config'>";

  page += "<label>Fan ON temperature (&deg;C):<br>";
  page += "<input type='number' step='0.1' name='fanOn' value='" + String(gConfig.env.fanOnTemp, 1) + "'></label>";

  page += "<label>Fan OFF temperature (&deg;C):<br>";
  page += "<input type='number' step='0.1' name='fanOff' value='" + String(gConfig.env.fanOffTemp, 1) + "'></label>";

  page += "<label>Fan ON humidity (%RH):<br>";
  page += "<input type='number' step='1' name='fanHumOn' value='" + String(gConfig.env.fanHumOn) + "'></label>";

  page += "<label>Fan OFF humidity (%RH):<br>";
  page += "<input type='number' step='1' name='fanHumOff' value='" + String(gConfig.env.fanHumOff) + "'></label>";

  page += "<label>Soil DRY threshold (%):<br>";
  page += "<input type='number' step='1' name='soilDry' value='" + String(gConfig.env.soilDryThreshold) + "'></label>";

  page += "<label>Soil WET threshold (%):<br>";
  page += "<input type='number' step='1' name='soilWet' value='" + String(gConfig.env.soilWetThreshold) + "'></label>";

  page += "<label>Pump minimum OFF time (seconds):<br>";
  page += "<input type='number' step='1' name='pumpOff' value='" + String(gConfig.env.pumpMinOffSec) + "'></label>";

  page += "<label>Pump maximum ON time (seconds):<br>";
  page += "<input type='number' step='1' name='pumpOn' value='" + String(gConfig.env.pumpMaxOnSec) + "'></label>";

  // Light schedules
  page += "<h2>Light schedules</h2>";

  page += "<label><input type='checkbox' name='l1Auto' value='1'";
  if (gConfig.light1.enabled) page += " checked";
  page += "> Use schedule for Light 1</label>";

  page += "<label>Light 1 ON time:<br>";
  page += "<input type='time' name='l1On' value='" + minutesToTimeStr(gConfig.light1.onMinutes) + "'></label>";

  page += "<label>Light 1 OFF time:<br>";
  page += "<input type='time' name='l1Off' value='" + minutesToTimeStr(gConfig.light1.offMinutes) + "'></label>";

  page += "<label><input type='checkbox' name='l2Auto' value='1'";
  if (gConfig.light2.enabled) page += " checked";
  page += "> Use schedule for Light 2</label>";

  page += "<label>Light 2 ON time:<br>";
  page += "<input type='time' name='l2On' value='" + minutesToTimeStr(gConfig.light2.onMinutes) + "'></label>";

  page += "<label>Light 2 OFF time:<br>";
  page += "<input type='time' name='l2Off' value='" + minutesToTimeStr(gConfig.light2.offMinutes) + "'></label>";

  page += "<label><input type='checkbox' name='autoFan' value='1'";
  if (gConfig.autoFan) page += " checked";
  page += "> Use automatic fan control</label>";

  page += "<label><input type='checkbox' name='autoPump' value='1'";
  if (gConfig.autoPump) page += " checked";
  page += "> Use automatic pump control</label>";

  // Web UI auth
  page += "<h2>Web UI authentication</h2>";
  page += "<p><small>If username is empty, HTTP authentication is disabled.</small></p>";

  page += "<label>Username:<br>";
  page += "<input type='text' name='authUser' value='";
  page += sWebAuthUser;
  page += "'></label>";

  page += "<label>Password (leave blank to keep current):<br>";
  page += "<input type='password' name='authPass' value=''></label>";

  page += "<button type='submit'>Save</button>";
  page += "</form></div>";

  page += "</body></html>";

  server.send(200, "text/html", page);
}

static void handleConfigPost() {
  if (!requireAuth()) return;

  // Env thresholds
  if (server.hasArg("fanOn")) {
    float v = server.arg("fanOn").toFloat();
    if (v > 0 && v < 80) gConfig.env.fanOnTemp = v;
  }
  if (server.hasArg("fanOff")) {
    float v = server.arg("fanOff").toFloat();
    if (v > 0 && v < 80) gConfig.env.fanOffTemp = v;
  }
  if (gConfig.env.fanOffTemp >= gConfig.env.fanOnTemp) {
    gConfig.env.fanOnTemp  = 28.0f;
    gConfig.env.fanOffTemp = 26.0f;
  }

  if (server.hasArg("fanHumOn")) {
    int v = server.arg("fanHumOn").toInt();
    gConfig.env.fanHumOn = constrain(v, 0, 100);
  }
  if (server.hasArg("fanHumOff")) {
    int v = server.arg("fanHumOff").toInt();
    gConfig.env.fanHumOff = constrain(v, 0, 100);
  }
  if (gConfig.env.fanHumOff >= gConfig.env.fanHumOn) {
    gConfig.env.fanHumOn  = 80;
    gConfig.env.fanHumOff = 70;
  }

  if (server.hasArg("soilDry")) {
    int v = server.arg("soilDry").toInt();
    gConfig.env.soilDryThreshold = constrain(v, 0, 100);
  }
  if (server.hasArg("soilWet")) {
    int v = server.arg("soilWet").toInt();
    gConfig.env.soilWetThreshold = constrain(v, 0, 100);
  }
  if (gConfig.env.soilWetThreshold <= gConfig.env.soilDryThreshold) {
    gConfig.env.soilDryThreshold = 35;
    gConfig.env.soilWetThreshold = 45;
  }

  if (server.hasArg("pumpOff")) {
    unsigned long v = server.arg("pumpOff").toInt();
    if (v >= 10 && v <= 36000) gConfig.env.pumpMinOffSec = v;
  }
  if (server.hasArg("pumpOn")) {
    unsigned long v = server.arg("pumpOn").toInt();
    if (v >= 5 && v <= 3600) gConfig.env.pumpMaxOnSec = v;
  }

  // Light schedules
  gConfig.light1.enabled = server.hasArg("l1Auto");
  gConfig.light2.enabled = server.hasArg("l2Auto");

  if (server.hasArg("l1On"))  gConfig.light1.onMinutes  = parseTimeToMinutes(server.arg("l1On"),  gConfig.light1.onMinutes);
  if (server.hasArg("l1Off")) gConfig.light1.offMinutes = parseTimeToMinutes(server.arg("l1Off"), gConfig.light1.offMinutes);
  if (server.hasArg("l2On"))  gConfig.light2.onMinutes  = parseTimeToMinutes(server.arg("l2On"),  gConfig.light2.onMinutes);
  if (server.hasArg("l2Off")) gConfig.light2.offMinutes = parseTimeToMinutes(server.arg("l2Off"), gConfig.light2.offMinutes);

  if (gConfig.light1.onMinutes == gConfig.light1.offMinutes) {
    gConfig.light1.onMinutes  = 8 * 60;
    gConfig.light1.offMinutes = 20 * 60;
  }
  if (gConfig.light2.onMinutes == gConfig.light2.offMinutes) {
    gConfig.light2.onMinutes  = 8 * 60;
    gConfig.light2.offMinutes = 20 * 60;
  }

  gConfig.autoFan  = server.hasArg("autoFan");
  gConfig.autoPump = server.hasArg("autoPump");

  // Web UI auth: update credentials in memory and NVS
  String newUser = sWebAuthUser;
  String newPass = sWebAuthPass;

  if (server.hasArg("authUser")) {
    String u = server.arg("authUser");
    u.trim();
    // username may be empty -> disable auth
    newUser = u;
  }

  if (server.hasArg("authPass")) {
    String p = server.arg("authPass");
    p.trim();
    // If password field is non-empty, update it; if empty, keep existing
    if (p.length() > 0) {
      newPass = p;
    }
  }

  // If username is empty, we also clear password to avoid confusion
  if (newUser.length() == 0) {
    newPass = "";
  }

  sWebAuthUser = newUser;
  sWebAuthPass = newPass;

  saveConfig();
  saveWebAuthConfig(sWebAuthUser, sWebAuthPass);

  server.sendHeader("Location", "/config", true);
  server.send(302, "text/plain", "");
}

// ================= Not found / captive portal redirect =================

static void handleNotFound() {
  if (sCaptivePortalActive) {
    // In captive portal mode, redirect everything to Wi-Fi setup page
    server.sendHeader("Location", "/wifi", true);
    server.send(302, "text/plain", "");
    return;
  }

  server.send(404, "text/plain", "Not found");
}

// ================= Public API =================

void initWebServer() {
  // Load web auth credentials from NVS at startup
  loadWebAuthConfig(sWebAuthUser, sWebAuthPass);

  // Determine if we should run a captive portal:
  // AP is enabled AND STA is NOT connected
  bool hasAp        = (WiFi.getMode() & WIFI_MODE_AP);
  bool staConnected = (WiFi.status() == WL_CONNECTED);

  if (hasAp && !staConnected) {
    sCaptivePortalActive = true;
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("[Portal] Captive portal active on AP IP: ");
    Serial.println(apIP);
    dnsServer.start(53, "*", apIP);
  } else {
    sCaptivePortalActive = false;
  }

  server.on("/",                 HTTP_GET,  handleRoot);
  server.on("/toggle",           HTTP_GET,  handleToggle);
  server.on("/mode",             HTTP_GET,  handleMode);
  server.on("/config",           HTTP_GET,  handleConfigGet);
  server.on("/config",           HTTP_POST, handleConfigPost);
  server.on("/api/history",      HTTP_GET,  handleHistoryApi);
  server.on("/chart.umd.min.js", HTTP_GET,  handleChartJs);
  server.on("/wifi",             HTTP_GET,  handleWifiConfigGet);
  server.on("/wifi",             HTTP_POST, handleWifiConfigPost);
  server.onNotFound(handleNotFound);

  server.begin();
}

void handleWebServer() {
  server.handleClient();

  if (sCaptivePortalActive) {
    dnsServer.processNextRequest();
  }
}
