#include "WebUI.h"
#include "Greenhouse.h"

#include <WebServer.h>
#include <LittleFS.h>
#include <ctype.h>
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
static String htmlAutoChange(bool applies, bool a) { return applies ? htmlAuto(a) : String("—"); }

static String minutesToTimeStrSafe(int mins) {
  mins = constrain(mins, 0, 24 * 60 - 1);
  char buf[6];
  sprintf(buf, "%02d:%02d", mins / 60, mins % 60);
  return String(buf);
}

// Convert "HH:MM" to minutes since midnight
static int parseTimeToMinutes(const String &s, int fallback) {
  int colon = s.indexOf(':');
  if (colon < 0) return fallback;
  int h = s.substring(0, colon).toInt();
  int m = s.substring(colon + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

static String htmlEscape(const String& in) {
  String s; s.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&': s += "&amp;"; break;
      case '<': s += "&lt;"; break;
      case '>': s += "&gt;"; break;
      case '"': s += "&quot;"; break;
      case '\'': s += "&#39;"; break;
      default: s += c; break;
    }
  }
  return s;
}

static String urlencode(const String& in) {
  String s; s.reserve(in.length() * 2);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      s += c;
    } else if (c == ' ') {
      s += '+';
    } else {
      s += '%';
      s += hex[(c >> 4) & 0xF];
      s += hex[c & 0xF];
    }
  }
  return s;
}

static String jsonEscape(const String& in) {
  String s; s.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '\\': s += "\\\\"; break;
      case '"':  s += "\\\""; break;
      case '\n': s += "\\n"; break;
      case '\r': s += "\\r"; break;
      case '\t': s += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          // skip control chars
        } else {
          s += c;
        }
        break;
    }
  }
  return s;
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

static void streamStaticFile(const char* path, const char* contentType) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    server.send(404, "text/plain", String(path) + " not found");
    return;
  }
  server.streamFile(f, contentType);
  f.close();
}

static void beginPage(String& page, const char* title, const char* activeNav, bool includeCharts) {
  page += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<meta name='theme-color' content='#12a150'>";
  page += "<title>";
  page += title;
  page += "</title>";
  page += "<link rel='icon' href='/logo-ezgrow.png' type='image/png'>";
  page += "<link rel='stylesheet' href='/app.css'>";
  if (includeCharts) page += "<script defer src='/chart.umd.min.js'></script>";
  page += "<script defer src='/app.js'></script>";
  page += "</head><body data-page='";
  page += activeNav;
  page += "'>";

  // Top bar
  page += "<div class='topbar'><div class='topbar-inner'>";
  page += "<div class='brand'><img src='/logo-ezgrow.png' class='brand-logo' alt='EZgrow logo'><span class='brand-text'>EZgrow</span></div>";

  page += "<div class='nav'>";
  if (!sCaptivePortalActive) {
    page += "<a href='/'";
    if (String(activeNav) == "dashboard") page += " class='active'";
    page += ">Dashboard</a>";

    page += "<a href='/config'";
    if (String(activeNav) == "config") page += " class='active'";
    page += ">Config</a>";

    page += "<a href='/wifi'";
    if (String(activeNav) == "wifi") page += " class='active'";
    page += ">Wi-Fi</a>";
  } else {
    page += "<a href='/wifi'";
    if (String(activeNav) == "wifi") page += " class='active'";
    page += ">Wi-Fi Setup</a>";
  }
  page += "</div>";

  page += "<div class='pills'>";
  page += "<span class='pill' id='top-time'>—</span>";
  page += "<span class='pill' id='top-conn'>—</span>";
  page += "</div>";

  page += "</div></div>";

  page += "<div class='container'>";
}

static void endPage(String& page) {
  page += "</div></body></html>";
}

// ================= History API =================

static void handleHistoryApi() {
  if (!requireAuth()) return;

  String json;
  json.reserve(24000);
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

    json += ",\"soil1\":";
    json += String(s.soil1);
    json += ",\"soil2\":";
    json += String(s.soil2);

    json += ",\"l1\":";
    json += s.light1 ? "1" : "0";
    json += ",\"l2\":";
    json += s.light2 ? "1" : "0";

    json += "}";
  }

  json += "]}";
  server.send(200, "application/json", json);
}

// ================= Status API (new) =================

static void handleStatusApi() {
  if (!requireAuth()) return;

  struct tm nowTime;
  bool timeAvail;
  greenhouseGetTime(nowTime, timeAvail);

  String timeStr = "syncing…";
  if (timeAvail) {
    char buf[16];
    sprintf(buf, "%02d:%02d:%02d", nowTime.tm_hour, nowTime.tm_min, nowTime.tm_sec);
    timeStr = buf;
  }

  bool connected = (WiFi.status() == WL_CONNECTED);
  String modeStr = connected ? "STA" : ((WiFi.getMode() & WIFI_MODE_AP) ? "AP" : "NONE");

  String json;
  json.reserve(1100);

  json += "{";
  json += "\"time\":\"" + jsonEscape(timeStr) + "\",";
  json += "\"time_synced\":"; json += (timeAvail ? "true" : "false"); json += ",";
  json += "\"timezone\":\"" + jsonEscape(greenhouseTimezoneLabel()) + "\",";
  json += "\"timezone_iana\":\"" + jsonEscape(greenhouseTimezoneIana()) + "\",";

  json += "\"wifi\":{";
  json += "\"connected\":"; json += (connected ? "true" : "false"); json += ",";
  json += "\"mode\":\"" + jsonEscape(modeStr) + "\"";

  if (connected) {
    json += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
    json += ",\"rssi\":" + String(WiFi.RSSI());
    json += ",\"ip\":\"" + jsonEscape(WiFi.localIP().toString()) + "\"";
  }
  json += "},";

  json += "\"sensors\":{";
  json += "\"temp_c\":";
  if (isnan(gSensors.temperatureC)) json += "null";
  else json += String(gSensors.temperatureC, 1);
  json += ",\"hum_rh\":";
  if (isnan(gSensors.humidityRH)) json += "null";
  else json += String(gSensors.humidityRH, 0);
  json += ",\"soil1\":" + String(gSensors.soil1Percent);
  json += ",\"soil2\":" + String(gSensors.soil2Percent);
  json += "},";

  json += "\"chambers\":[";
  auto chamberJson = [&](int idx, const ChamberConfig& cfg, int soilPercent, const char* lightId) {
    const int id = idx + 1;
    const char* fallback = (idx == 0) ? DEFAULT_CHAMBER1_NAME : DEFAULT_CHAMBER2_NAME;
    const String name = cfg.name.length() ? cfg.name : String(fallback);
    json += "{\"id\":" + String(id);
    json += ",\"idx\":" + String(idx);
    json += ",\"name\":\"" + jsonEscape(name) + "\"";
    json += ",\"soil\":" + String(soilPercent);
    json += ",\"soil_dry_threshold\":" + String(cfg.soilDryThreshold);
    json += ",\"soil_wet_threshold\":" + String(cfg.soilWetThreshold);
    json += ",\"light_relay_id\":\"" + jsonEscape(lightId) + "\"}";
  };
  chamberJson(0, gConfig.chamber1, gSensors.soil1Percent, "light1");
  json += ",";
  chamberJson(1, gConfig.chamber2, gSensors.soil2Percent, "light2");
  json += "],";

  auto sched = [](const LightSchedule& lc)->String {
    return minutesToTimeStrSafe(lc.onMinutes) + "–" + minutesToTimeStrSafe(lc.offMinutes);
  };

  json += "\"relays\":{";

  json += "\"light1\":{";
  json += "\"state\":"; json += (gRelays.light1 ? "1" : "0"); json += ",";
  json += "\"auto\":";  json += (gConfig.light1.enabled ? "1" : "0"); json += ",";
  json += "\"schedule\":\"" + jsonEscape(sched(gConfig.light1)) + "\"";
  json += "},";

  json += "\"light2\":{";
  json += "\"state\":"; json += (gRelays.light2 ? "1" : "0"); json += ",";
  json += "\"auto\":";  json += (gConfig.light2.enabled ? "1" : "0"); json += ",";
  json += "\"schedule\":\"" + jsonEscape(sched(gConfig.light2)) + "\"";
  json += "},";

  json += "\"fan\":{";
  json += "\"state\":"; json += (gRelays.fan ? "1" : "0"); json += ",";
  json += "\"auto\":";  json += (gConfig.autoFan ? "1" : "0");
  json += "},";

  json += "\"pump\":{";
  json += "\"state\":"; json += (gRelays.pump ? "1" : "0"); json += ",";
  json += "\"auto\":";  json += (gConfig.autoPump ? "1" : "0");
  json += "}";

  json += "}"; // relays
  json += "}";

  server.send(200, "application/json", json);
}

static bool parseNumericString(const String& raw) {
  if (raw.length() == 0) return false;
  for (size_t i = 0; i < raw.length(); i++) {
    if (!isdigit((unsigned char)raw[i])) return false;
  }
  return true;
}

static bool parseChamberValue(const String& raw, bool preferId, int &chamberIdx, int &chamberId) {
  if (!parseNumericString(raw)) return false;
  int val = raw.toInt();
  if (preferId) {
    if (val >= 1 && val <= 2) {
      chamberIdx = val - 1;
      chamberId = val;
      return true;
    }
    return false;
  }

  if (val == 0 || val == 1) {
    chamberIdx = val;
    chamberId = val + 1;
    return true;
  }
  if (val == 2) {
    chamberIdx = 1;
    chamberId = 2;
    return true;
  }
  return false;
}

static bool resolveChamberParam(int &chamberIdx, int &chamberId) {
  if (server.hasArg("chamber") && parseChamberValue(server.arg("chamber"), false, chamberIdx, chamberId)) {
    return true;
  }
  if (server.hasArg("chamber_id") && parseChamberValue(server.arg("chamber_id"), true, chamberIdx, chamberId)) {
    return true;
  }
  return false;
}

static void handleApplyProfileChamberApi() {
  if (!requireAuth()) return;
  if ((!server.hasArg("chamber") && !server.hasArg("chamber_id")) || !server.hasArg("profile")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_args\"}");
    return;
  }

  int chamberIdx = -1;
  int chamberId  = -1;
  if (!resolveChamberParam(chamberIdx, chamberId)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_chamber\"}");
    return;
  }
  int profileId = server.arg("profile").toInt();
  String appliedName;
  if (!applyGrowProfileToChamber(chamberIdx, profileId, appliedName)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid\"}");
    return;
  }

  saveConfig();

  ChamberConfig &cfg = (chamberIdx == 0) ? gConfig.chamber1 : gConfig.chamber2;
  const char* fallback = (chamberIdx == 0) ? DEFAULT_CHAMBER1_NAME : DEFAULT_CHAMBER2_NAME;
  const String chamberName = cfg.name.length() ? cfg.name : String(fallback);
  String label = appliedName + " -> " + chamberName;

  String json = "{";
  json += "\"ok\":true,";
  json += "\"applied_profile\":\"" + jsonEscape(appliedName) + "\",";
  json += "\"chamber_idx\":" + String(chamberIdx) + ",";
  json += "\"chamber_id\":" + String(chamberId) + ",";
  json += "\"chamber_name\":\"" + jsonEscape(chamberName) + "\",";
  json += "\"label\":\"" + jsonEscape(label) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// ================= Static assets from LittleFS =================

static void handleChartJs() {
  // Chart.js is not sensitive; no auth required even in STA mode
  streamStaticFile("/chart.umd.min.js", "application/javascript");
}

static void handleLogoPng() {
  streamStaticFile("/logo-ezgrow.png", "image/png");
}

static void handleAppCss() {
  streamStaticFile("/app.css", "text/css");
}

static void handleAppJs() {
  streamStaticFile("/app.js", "application/javascript");
}

// ================= API controls (new) =================

static void handleApiToggle() {
  if (!requireAuth()) return;

  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing id\"}");
    return;
  }

  String id = server.arg("id");

  bool changed = false;
  String reason = "";
  if (id == "light1" && !gConfig.light1.enabled) {
    gRelays.light1 = !gRelays.light1; changed = true;
  } else if (id == "light1") {
    reason = "AUTO";
  } else if (id == "light2" && !gConfig.light2.enabled) {
    gRelays.light2 = !gRelays.light2; changed = true;
  } else if (id == "light2") {
    reason = "AUTO";
  } else if (id == "fan" && !gConfig.autoFan) {
    gRelays.fan = !gRelays.fan; changed = true;
  } else if (id == "fan") {
    reason = "AUTO";
  } else if (id == "pump" && !gConfig.autoPump) {
    gRelays.pump = !gRelays.pump; changed = true;
  } else if (id == "pump") {
    reason = "AUTO";
  }

  String json = String("{\"ok\":true,\"changed\":") + (changed ? "true" : "false");
  if (!changed && reason.length() > 0) {
    json += ",\"reason\":\"";
    json += jsonEscape(reason);
    json += "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

static void handleApiMode() {
  if (!requireAuth()) return;

  if (!server.hasArg("id") || !server.hasArg("auto")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing args\"}");
    return;
  }

  String id   = server.arg("id");
  bool autoOn = (server.arg("auto") == "1");

  bool changed = false;

  if (id == "fan") {
    changed = (gConfig.autoFan != autoOn);
    if (changed) gConfig.autoFan = autoOn;
  } else if (id == "pump") {
    changed = (gConfig.autoPump != autoOn);
    if (changed) gConfig.autoPump = autoOn;
  } else if (id == "light1") {
    changed = (gConfig.light1.enabled != autoOn);
    if (changed) gConfig.light1.enabled = autoOn;
  } else if (id == "light2") {
    changed = (gConfig.light2.enabled != autoOn);
    if (changed) gConfig.light2.enabled = autoOn;
  }

  if (changed) saveConfig();
  server.send(200, "application/json", String("{\"ok\":true,\"changed\":") + (changed ? "true" : "false") + "}");
}

// ================= Wi-Fi configuration page =================

static void appendWifiConfigSection(String& page, const String& storedSsid, const String& storedPass, int networkCount) {
  page += "<div class='card'><h2>Current connection</h2>";
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    page += "<div class='sub'>Connected to <b>" + htmlEscape(WiFi.SSID()) + "</b>";
    page += " · RSSI " + String(WiFi.RSSI()) + " dBm";
    page += " · IP " + htmlEscape(WiFi.localIP().toString()) + "</div>";
  } else {
    page += "<div class='sub'>Not connected.</div>";
  }
  page += "</div>";

  page += "<div class='card'><h2>Configure Wi-Fi</h2>";
  page += "<div class='sub'>After saving, the device will reboot and try to connect.</div>";
  page += "<form method='POST' action='/wifi'>";
  page += "<div class='form-grid' style='margin-top:12px'>";
  page += "<div class='field'><label>SSID</label>";
  page += "<input type='text' id='ssid' name='ssid' value='" + htmlEscape(storedSsid) + "'></div>";
  page += "<div class='field'><label>Password</label>";
  page += "<input type='password' name='pass' value='" + htmlEscape(storedPass) + "'></div>";
  page += "</div>";
  page += "<p class='small'>Password is stored in ESP32 NVS (not encrypted).</p>";
  page += "<div class='row'><button class='btn primary' type='submit'>Save &amp; Reboot</button></div>";
  page += "</form></div>";

  page += "<div class='card'><h2>Available networks</h2>";
  page += "<div class='row' style='justify-content:space-between'>";
  page += "<div class='sub'>Click a row to copy the SSID into the form.</div>";
  page += "<input id='ssidFilter' placeholder='Filter SSIDs…' style='max-width:280px'>";
  page += "</div>";

  if (networkCount <= 0) {
    page += "<p class='small'>No networks found.</p>";
  } else {
    page += "<table class='table' style='margin-top:12px'>";
    page += "<tr><th>SSID</th><th>RSSI</th><th>Encryption</th></tr>";
    for (int i = 0; i < networkCount; ++i) {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      wifi_auth_mode_t enc = WiFi.encryptionType(i);

      page += "<tr class='ssid-row' data-ssid='";
      page += htmlEscape(ssid);
      page += "'><td>";
      page += htmlEscape(ssid);
      page += "</td><td>";
      page += String(rssi);
      page += " dBm</td><td>";
      page += (enc == WIFI_AUTH_OPEN) ? "open" : "secured";
      page += "</td></tr>";
    }
    page += "</table>";
  }
  page += "</div>";
}

static void handleWifiConfigGet() {
  if (!requireAuth()) return;

  String storedSsid, storedPass;
  loadWifiCredentials(storedSsid, storedPass);

  int n = WiFi.scanNetworks();

  String page;
  page.reserve(12000);

  beginPage(page, "Wi-Fi", "wifi", false);
  appendWifiConfigSection(page, storedSsid, storedPass, n);

  endPage(page);

  server.send(200, "text/html", page);
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
  page.reserve(1600);

  beginPage(page, "Wi-Fi Saved", "wifi", false);
  page += "<div class='card'><h2>Wi-Fi configuration saved</h2>";
  page += "<p class='sub'>SSID: <b>" + htmlEscape(ssid) + "</b></p>";
  page += "<p class='sub'>Rebooting now and attempting to connect…</p>";
  page += "</div>";
  endPage(page);

  server.send(200, "text/html", page);

  delay(500);
  ESP.restart();
}

// ================= Dashboard (/) =================

static void handleRoot() {
  if (!requireAuth()) return;

  String page;
  page.reserve(9000);

  beginPage(page, "EZgrow Dashboard", "dashboard", true);

  page += "<div class='grid grid-tiles'>";
  page += "<div class='tile'><div class='tile-label'>Temperature</div>"
          "<div class='tile-value'><span id='v-temp'>—</span><span class='tile-unit'>°C</span></div>"
          "<div class='tile-label'>Air</div><canvas class='sparkline' id='spark-temp' height='38'></canvas></div>";

  page += "<div class='tile'><div class='tile-label'>Humidity</div>"
          "<div class='tile-value'><span id='v-hum'>—</span><span class='tile-unit'>%</span></div>"
          "<div class='tile-label'>Air</div><canvas class='sparkline' id='spark-hum' height='38'></canvas></div>";

  page += "<div class='tile'><div class='tile-label'>Soil · <span id='lbl-s1'>" + htmlEscape(gConfig.chamber1.name) + "</span></div>"
          "<div class='tile-value'><span id='v-s1'>—</span><span class='tile-unit'>%</span></div>"
          "<div class='tile-label'>Moisture</div><canvas class='sparkline' id='spark-s1' height='38'></canvas></div>";

  page += "<div class='tile'><div class='tile-label'>Soil · <span id='lbl-s2'>" + htmlEscape(gConfig.chamber2.name) + "</span></div>"
          "<div class='tile-value'><span id='v-s2'>—</span><span class='tile-unit'>%</span></div>"
          "<div class='tile-label'>Moisture</div><canvas class='sparkline' id='spark-s2' height='38'></canvas></div>";
  page += "</div>";

  page += "<div class='card' style='margin-top:14px'>";
  page += "<h2>Controls</h2>";
  page += "<div class='controls'>";

  auto control = [&](const char* id, const char* label, const String& chamberName, bool isAuto, bool isOn, const String& schedule) {
    page += "<div class='card' style='box-shadow:none'>";
    page += "<div class='control-head'>";
    page += "<div><div class='control-title'>" + String(label);
    if (chamberName.length()) {
      page += " · <span id='ctl-";
      page += id;
      page += "-name'>";
      page += htmlEscape(chamberName);
      page += "</span>";
    }
    page += "</div>";
    page += "<div class='sub'>Mode <span class='badge ";
    page += (isAuto ? "auto" : "man");
    page += "' id='m-"; page += id; page += "'>";
    page += htmlAuto(isAuto);
    page += "</span></div></div>";

    page += "<span class='badge ";
    page += (isOn ? "on" : "off");
    page += "' id='b-"; page += id; page += "'>";
    page += htmlBool(isOn);
    page += "</span>";
    page += "</div>";

    page += "<div class='row control-actions'>";
    page += "<div class='segmented' role='group' aria-label='Mode'>";
    page += "<button type='button' class='seg-btn";
    if (isAuto) page += " active";
    page += "' id='seg-"; page += id; page += "-auto' data-mode='auto'>AUTO</button>";
    page += "<button type='button' class='seg-btn";
    if (!isAuto) page += " active";
    page += "' id='seg-"; page += id; page += "-man' data-mode='man'>MAN</button>";
    page += "</div>";

    page += "<button type='button' class='btn' id='tog-"; page += id; page += "'";
    if (isAuto) page += " disabled";
    page += ">";
    page += isOn ? "Turn OFF" : "Turn ON";
    page += "</button>";

    page += "<span class='meta' id='sched-"; page += id; page += "'>";
    page += htmlEscape(schedule);
    page += "</span>";

    page += "</div></div>";
  };

  control("light1", "Light 1", gConfig.chamber1.name, gConfig.light1.enabled, gRelays.light1,
          minutesToTimeStrSafe(gConfig.light1.onMinutes) + "–" + minutesToTimeStrSafe(gConfig.light1.offMinutes));
  control("light2", "Light 2", gConfig.chamber2.name, gConfig.light2.enabled, gRelays.light2,
          minutesToTimeStrSafe(gConfig.light2.onMinutes) + "–" + minutesToTimeStrSafe(gConfig.light2.offMinutes));
  control("fan", "Fan", "", gConfig.autoFan, gRelays.fan, "threshold-based");
  control("pump", "Pump", "", gConfig.autoPump, gRelays.pump, "soil-based");

  page += "</div>"; // controls

  page += "<p class='small' style='margin-top:12px'>Fan: ON ≥ ";
  page += String(gConfig.env.fanOnTemp, 1);
  page += " °C or ≥ ";
  page += String(gConfig.env.fanHumOn);
  page += "% RH · OFF when ≤ ";
  page += String(gConfig.env.fanOffTemp, 1);
  page += " °C and ≤ ";
  page += String(gConfig.env.fanHumOff);
  page += "% RH. Pump: ";
  page += htmlEscape(gConfig.chamber1.name);
  page += " dry &lt; ";
  page += String(gConfig.chamber1.soilDryThreshold);
  page += "%, wet &gt; ";
  page += String(gConfig.chamber1.soilWetThreshold);
  page += "% · ";
  page += htmlEscape(gConfig.chamber2.name);
  page += " dry &lt; ";
  page += String(gConfig.chamber2.soilDryThreshold);
  page += "%, wet &gt; ";
  page += String(gConfig.chamber2.soilWetThreshold);
  page += "%.</p>";

  page += "</div>"; // card

  page += "<div class='card' style='margin-top:14px'>";
  page += "<h2>History (last 24 h)</h2>";
  page += "<div class='sub'>Temperature/humidity, soil moisture, and light states (logged every minute).</div>";
  page += "<div style='margin-top:12px'><canvas id='tempHumChart' height='150'></canvas></div>";
  page += "<div style='margin-top:14px'><canvas id='soilChart' height='120'></canvas></div>";
  page += "<!-- Light history chart removed -->";
  page += "</div>";

  endPage(page);
  server.send(200, "text/html", page);
}

// ================= Relay toggle & mode (legacy endpoints kept) =================

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

  bool changed = false;

  if (id == "fan") {
    changed = (gConfig.autoFan != autoOn);
    if (changed) gConfig.autoFan = autoOn;
  } else if (id == "pump") {
    changed = (gConfig.autoPump != autoOn);
    if (changed) gConfig.autoPump = autoOn;
  } else if (id == "light1") {
    changed = (gConfig.light1.enabled != autoOn);
    if (changed) gConfig.light1.enabled = autoOn;
  } else if (id == "light2") {
    changed = (gConfig.light2.enabled != autoOn);
    if (changed) gConfig.light2.enabled = autoOn;
  }

  if (changed) saveConfig();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ================= Config page (tabbed) =================

static void handleConfigGet() {
  if (!requireAuth()) return;

  String storedSsid, storedPass;
  loadWifiCredentials(storedSsid, storedPass);
  int wifiNetworks = WiFi.scanNetworks();

  String page;
  page.reserve(12000);

  beginPage(page, "EZgrow Config", "config", false);

  page += "<div class='card'><h2>Configuration</h2>";
  page += "<div class='sub'>Settings are saved to NVS and applied immediately.</div>";

  String appliedBanner = server.hasArg("appliedProfile") ? server.arg("appliedProfile") : "";
  page += "<div id='appliedProfileBanner' class='pill' data-label='" + htmlEscape(appliedBanner) + "' style='margin-top:10px";
  if (!appliedBanner.length()) page += ";display:none";
  page += "'>";
  if (appliedBanner.length()) {
    page += "Applied profile: " + htmlEscape(appliedBanner);
  }
  page += "</div>";

  page += "<div class='tabs' data-tabs='config' data-persist='ezgrow_config_tab' style='margin-top:12px'>";
  page += "<button class='tab' type='button' data-tab='env'>Environment</button>";
  page += "<button class='tab' type='button' data-tab='lights'>Lights</button>";
  page += "<button class='tab' type='button' data-tab='auto'>Automation</button>";
  page += "<button class='tab' type='button' data-tab='grow'>Grow profile</button>";
  page += "<button class='tab' type='button' data-tab='wifi'>Wi-Fi</button>";
  page += "<button class='tab' type='button' data-tab='system'>System</button>";
  page += "<button class='tab' type='button' data-tab='security'>Security</button>";
  page += "</div>";

  page += "<div class='tab-panels'>";

  page += "<form method='POST' action='/config' style='margin-top:12px'>";

  // ENV
  page += "<div class='tab-panel' data-tab='env'>";
  page += "<div class='form-grid'>";
  page += "<div class='field'><label>Fan ON temperature (°C)</label>"
          "<input type='number' step='0.1' name='fanOn' value='" + String(gConfig.env.fanOnTemp, 1) + "'></div>";
  page += "<div class='field'><label>Fan OFF temperature (°C)</label>"
          "<input type='number' step='0.1' name='fanOff' value='" + String(gConfig.env.fanOffTemp, 1) + "'></div>";
  page += "<div class='field'><label>Fan ON humidity (%RH)</label>"
          "<input type='number' step='1' name='fanHumOn' value='" + String(gConfig.env.fanHumOn) + "'></div>";
  page += "<div class='field'><label>Fan OFF humidity (%RH)</label>"
          "<input type='number' step='1' name='fanHumOff' value='" + String(gConfig.env.fanHumOff) + "'></div>";
  page += "<div class='field'><label>Chamber 1 name</label>"
          "<input type='text' maxlength='24' name='c1Name' value='" + htmlEscape(gConfig.chamber1.name) + "'><div class='small'>1–24 characters, HTML is stripped automatically.</div></div>";
  page += "<div class='field'><label>Chamber 2 name</label>"
          "<input type='text' maxlength='24' name='c2Name' value='" + htmlEscape(gConfig.chamber2.name) + "'><div class='small'>1–24 characters, HTML is stripped automatically.</div></div>";
  page += "<div class='field'><label>Chamber 1 DRY threshold (%)</label>"
          "<input type='number' step='1' name='c1SoilDry' value='" + String(gConfig.chamber1.soilDryThreshold) + "'><div class='small'>Uses soil sensor 1; pump is shared across chambers.</div></div>";
  page += "<div class='field'><label>Chamber 1 WET threshold (%)</label>"
          "<input type='number' step='1' name='c1SoilWet' value='" + String(gConfig.chamber1.soilWetThreshold) + "'><div class='small'>Keep wet > dry for stable pump automation.</div></div>";
  page += "<div class='field'><label>Chamber 2 DRY threshold (%)</label>"
          "<input type='number' step='1' name='c2SoilDry' value='" + String(gConfig.chamber2.soilDryThreshold) + "'><div class='small'>Uses soil sensor 2; shared pump serves both chambers.</div></div>";
  page += "<div class='field'><label>Chamber 2 WET threshold (%)</label>"
          "<input type='number' step='1' name='c2SoilWet' value='" + String(gConfig.chamber2.soilWetThreshold) + "'><div class='small'>Keep wet > dry for stable pump automation.</div></div>";
  page += "<div class='field'><label>Chamber 1 profile ID (optional)</label>"
          "<input type='number' step='1' name='c1Prof' value='" + String(gConfig.chamber1.profileId) + "'></div>";
  page += "<div class='field'><label>Chamber 2 profile ID (optional)</label>"
          "<input type='number' step='1' name='c2Prof' value='" + String(gConfig.chamber2.profileId) + "'></div>";
  page += "<div class='field'><label>Pump minimum OFF time (seconds)</label>"
          "<input type='number' step='1' name='pumpOff' value='" + String(gConfig.env.pumpMinOffSec) + "'></div>";
  page += "<div class='field'><label>Pump maximum ON time (seconds)</label>"
          "<input type='number' step='1' name='pumpOn' value='" + String(gConfig.env.pumpMaxOnSec) + "'></div>";
  page += "</div>";
  page += "<p class='small' style='margin-top:10px'>Tip: keep hysteresis sane (OFF < ON) to avoid oscillation. Names are limited to 24 characters with HTML stripped. Wet thresholds must stay above dry thresholds per chamber while using the shared pump.</p>";
  page += "</div>";

  // LIGHTS
  page += "<div class='tab-panel' data-tab='lights'>";
  page += "<div class='form-grid'>";
  page += "<div class='field'><label><input type='checkbox' name='l1Auto' value='1'";
  if (gConfig.light1.enabled) page += " checked";
  page += "> Use schedule for Light 1</label>";
  page += "<div class='small'>AUTO uses schedule; MAN allows dashboard toggling.</div></div>";

  page += "<div class='field'><label><input type='checkbox' name='l2Auto' value='1'";
  if (gConfig.light2.enabled) page += " checked";
  page += "> Use schedule for Light 2</label>";
  page += "<div class='small'>Schedules can cross midnight.</div></div>";

  page += "<div class='field'><label>Light 1 ON</label>"
          "<input type='time' name='l1On' value='" + minutesToTimeStrSafe(gConfig.light1.onMinutes) + "'></div>";
  page += "<div class='field'><label>Light 1 OFF</label>"
          "<input type='time' name='l1Off' value='" + minutesToTimeStrSafe(gConfig.light1.offMinutes) + "'></div>";
  page += "<div class='field'><label>Light 2 ON</label>"
          "<input type='time' name='l2On' value='" + minutesToTimeStrSafe(gConfig.light2.onMinutes) + "'></div>";
  page += "<div class='field'><label>Light 2 OFF</label>"
          "<input type='time' name='l2Off' value='" + minutesToTimeStrSafe(gConfig.light2.offMinutes) + "'></div>";
  page += "</div>";
  page += "</div>";

  // AUTO
  page += "<div class='tab-panel' data-tab='auto'>";
  page += "<div class='form-grid'>";
  page += "<div class='field'><label><input type='checkbox' name='autoFan' value='1'";
  if (gConfig.autoFan) page += " checked";
  page += "> Automatic fan control</label><div class='small'>Uses temperature/humidity thresholds.</div></div>";
  page += "<div class='field'><label><input type='checkbox' name='autoPump' value='1'";
  if (gConfig.autoPump) page += " checked";
  page += "> Automatic pump control</label><div class='small'>Uses soil thresholds + min OFF / max ON timing.</div></div>";
  page += "</div>";
  page += "</div>";

  // GROW PROFILE
  page += "<div class='tab-panel' data-tab='grow'>";
  page += "<div class='form-grid'>";

  auto profileOptions = [](int selectedId) -> String {
    auto profileDataAttrs = [](const GrowProfileInfo* info) -> String {
      if (!info) return String("");

      String attrs;
      attrs.reserve(180);

      attrs += " data-label='" + htmlEscape(info->label) + "'";

      attrs += " data-c1-dry='" + String(info->chamber1.soilDryThreshold) + "'";
      attrs += " data-c1-wet='" + String(info->chamber1.soilWetThreshold) + "'";
      attrs += " data-c2-dry='" + String(info->chamber2.soilDryThreshold) + "'";
      attrs += " data-c2-wet='" + String(info->chamber2.soilWetThreshold) + "'";

      attrs += " data-l1-on='" + minutesToTimeStrSafe(info->light1.onMinutes) + "'";
      attrs += " data-l1-off='" + minutesToTimeStrSafe(info->light1.offMinutes) + "'";
      attrs += " data-l1-auto='" + String(info->light1.enabled ? 1 : 0) + "'";
      attrs += " data-l2-on='" + minutesToTimeStrSafe(info->light2.onMinutes) + "'";
      attrs += " data-l2-off='" + minutesToTimeStrSafe(info->light2.offMinutes) + "'";
      attrs += " data-l2-auto='" + String(info->light2.enabled ? 1 : 0) + "'";

      attrs += " data-auto-fan='" + String(info->autoFan ? 1 : 0) + "'";
      attrs += " data-auto-pump='" + String(info->autoPump ? 1 : 0) + "'";
      attrs += " data-set-auto-fan='" + String(info->setsAutoFan ? 1 : 0) + "'";
      attrs += " data-set-auto-pump='" + String(info->setsAutoPump ? 1 : 0) + "'";

      return attrs;
    };

    String opts;
    opts.reserve(160);
    for (size_t i = 0; i < growProfileCount(); i++) {
      const GrowProfileInfo* info = growProfileInfoAt(i);
      if (!info) continue;
      opts += "<option value='" + String(i) + "'";
      opts += profileDataAttrs(info);
      if ((int)i == selectedId) opts += " selected";
      opts += ">" + htmlEscape(info->label) + "</option>";
    }
    return opts;
  };

  auto chamberProfileRow = [&](int idx, const ChamberConfig& cfg, int selectedId) {
    const char* fallback = (idx == 0) ? DEFAULT_CHAMBER1_NAME : DEFAULT_CHAMBER2_NAME;
    const String chamberName = cfg.name.length() ? cfg.name : String(fallback);
    page += "<div class='field chamber-profile' data-chamber='" + String(idx) + "' data-chamber-id='" + String(idx + 1) + "' data-chamber-name='" + htmlEscape(chamberName) + "' data-light-label='Light " + String(idx + 1) + "'>";
    page += "<label>Preset for " + htmlEscape(chamberName) + "</label>";
    page += "<div class='row' style='gap:8px;flex-wrap:wrap'>";
    page += "<select id='prof-ch" + String(idx + 1) + "' name='growProfileCh" + String(idx + 1) + "'>";
    page += profileOptions(selectedId);
    page += "</select>";
    page += "<button class='btn primary apply-profile' type='button' data-chamber='" + String(idx) + "' data-chamber-id='" + String(idx + 1) + "' data-chamber-name='" + htmlEscape(chamberName) + "' data-light-label='Light " + String(idx + 1) + "'>";
    page += "Apply to " + htmlEscape(chamberName);
    page += "</button></div>";
    page += "<div class='small'>Updates only this chamber's soil thresholds and linked light schedule/auto flag.</div>";
    page += "<div class='profile-preview' data-preview='ch" + String(idx + 1) + "' data-chamber-name='" + htmlEscape(chamberName) + "' data-light-label='Light " + String(idx + 1) + "'>";
    page += "<div class='preview-head'><div class='preview-title'>Preview for " + htmlEscape(chamberName) + "</div>";
    page += "<div class='small'>Shows the preset values before applying.</div></div>";
    page += "<table class='table preview-table'><tr><th>Soil</th><td class='pv-soil'>Select a preset</td></tr>";
    page += "<tr><th>Light schedule</th><td class='pv-light'>—</td></tr>";
    page += "<tr><th>Light mode</th><td class='pv-mode'>—</td></tr></table>";
    page += "</div>";
    page += "</div>";
  };

  int chamber1Selected = (gConfig.chamber1.profileId > 0) ? gConfig.chamber1.profileId : 0;
  int chamber2Selected = (gConfig.chamber2.profileId > 0) ? gConfig.chamber2.profileId : 0;
  chamberProfileRow(0, gConfig.chamber1, chamber1Selected);
  chamberProfileRow(1, gConfig.chamber2, chamber2Selected);

  page += "<div class='field'><label>Apply preset to both + env</label>";
  page += "<div class='row' style='gap:8px;flex-wrap:wrap'>";
  page += "<select name='growProfileAll'>";
  page += profileOptions(0);
  page += "</select>";
  page += "<button class='btn' type='submit' name='applyProfile' value='1'>Apply to both + env</button>";
  page += "</div><div class='small'>Applies env thresholds, both chambers, and any preset automation defaults.</div></div>";

  page += "</div>";
  page += "<div class='small' style='margin-top:10px'>Preset preview:</div>";
  page += "<table class='table profile-summary' style='margin-top:6px'>";
  page += "<tr><th>Preset</th><th>Ch1 soil (dry/wet %)</th><th>Ch2 soil (dry/wet %)</th><th>Light windows (L1/L2)</th><th>Fan on/off (°C)</th><th>Hum on/off (%)</th><th>Pump OFF/ON (s)</th><th>Fan/Pump mode change</th></tr>";
  for (size_t i = 0; i < growProfileCount(); i++) {
    const GrowProfileInfo* info = growProfileInfoAt(i);
    if (!info) continue;
    page += "<tr><td>" + htmlEscape(info->label) + "</td>";
    page += "<td>" + String(info->chamber1.soilDryThreshold) + " / " + String(info->chamber1.soilWetThreshold) + "</td>";
    page += "<td>" + String(info->chamber2.soilDryThreshold) + " / " + String(info->chamber2.soilWetThreshold) + "</td>";
    page += "<td>L1 " + minutesToTimeStrSafe(info->light1.onMinutes) + "–" + minutesToTimeStrSafe(info->light1.offMinutes) +
            " · L2 " + minutesToTimeStrSafe(info->light2.onMinutes) + "–" + minutesToTimeStrSafe(info->light2.offMinutes) + "</td>";
    page += "<td>" + String(info->env.fanOnTemp, 1) + " / " + String(info->env.fanOffTemp, 1) + "</td>";
    page += "<td>" + String(info->env.fanHumOn) + " / " + String(info->env.fanHumOff) + "</td>";
    page += "<td>" + String(info->env.pumpMinOffSec) + " / " + String(info->env.pumpMaxOnSec) + "</td>";
    page += "<td>Fan " + htmlAutoChange(info->setsAutoFan, info->autoFan) + " · Pump " + htmlAutoChange(info->setsAutoPump, info->autoPump) + "</td></tr>";
  }
  page += "</table>";
  page += "</div>";

  // SYSTEM
  int tzIndex = gConfig.tzIndex;
  size_t tzCount = greenhouseTimezoneCount();
  if (tzIndex < 0) tzIndex = 0;
  if (tzCount > 0 && (size_t)tzIndex >= tzCount) tzIndex = tzCount - 1;
  gConfig.tzIndex = tzIndex;

  page += "<div class='tab-panel' data-tab='system'>";
  page += "<div class='form-grid'>";
  page += "<div class='field'><label>Current local time</label><div class='pill muted' id='cfg-time'>—</div></div>";
  page += "<div class='field'><label>Timezone</label><select name='tzIndex'>";
  for (size_t i = 0; i < tzCount; i++) {
    page += "<option value='" + String(i) + "'";
    if ((int)i == tzIndex) page += " selected";
    page += ">" + htmlEscape(greenhouseTimezoneLabelAt(i)) + "</option>";
  }
  page += "</select><div class='small'>Applied immediately to NTP and time display.</div></div>";
  page += "</div>";
  page += "</div>";

  // SECURITY
  page += "<div class='tab-panel' data-tab='security'>";
  page += "<p class='small'>If username is empty, HTTP Basic Auth is disabled.</p>";
  page += "<div class='form-grid'>";
  page += "<div class='field'><label>Username</label>"
          "<input type='text' name='authUser' value='" + htmlEscape(sWebAuthUser) + "'></div>";
  page += "<div class='field'><label>Password (leave blank to keep current)</label>"
          "<input type='password' name='authPass' value=''></div>";
  page += "</div>";
  page += "</div>";

  page += "<div class='row' style='margin-top:14px'>";
  page += "<button class='btn primary' type='submit'>Save</button>";
  page += "<a class='btn ghost' href='/'>Back</a>";
  page += "</div>";

  page += "</form>";

  // WIFI
  page += "<div class='tab-panel' data-tab='wifi'>";
  appendWifiConfigSection(page, storedSsid, storedPass, wifiNetworks);
  page += "</div>";
  page += "</div>"; // panels
  page += "</div>"; // card

  endPage(page);
  server.send(200, "text/html", page);
  WiFi.scanDelete();
}

static void handleConfigPost() {
  if (!requireAuth()) return;

  int originalTzIndex = gConfig.tzIndex;
  bool timezoneChanged = false;

  auto chamberProfileArg = [&](int chamberIdx) -> String {
    String field = String("growProfileCh") + String(chamberIdx + 1);
    if (server.hasArg(field)) return server.arg(field);
    if (server.hasArg("growProfile")) return server.arg("growProfile");
    return String("");
  };

  if (server.hasArg("applyProfileChamber")) {
    int chamberIdx = server.arg("applyProfileChamber").toInt();
    int pid = chamberProfileArg(chamberIdx).toInt();
    String appliedName;
    if (applyGrowProfileToChamber(chamberIdx, pid, appliedName)) {
      saveConfig();
      const char* fallbackName = (chamberIdx == 0) ? DEFAULT_CHAMBER1_NAME : DEFAULT_CHAMBER2_NAME;
      const String chamberName = (chamberIdx == 0) ? gConfig.chamber1.name : (chamberIdx == 1 ? gConfig.chamber2.name : String(fallbackName));
      String label = appliedName + " -> " + (chamberName.length() ? chamberName : String(fallbackName));
      server.sendHeader("Location", String("/config?appliedProfile=") + urlencode(label), true);
      server.send(302, "text/plain", "");
      return;
    }
  }

  if (server.hasArg("applyProfile")) {
    int pid = server.hasArg("growProfileAll") ? server.arg("growProfileAll").toInt() : server.arg("growProfile").toInt();
    String appliedName;
    if (applyGrowProfile(pid, appliedName)) {
      saveConfig();
      server.sendHeader("Location", String("/config?appliedProfile=") + urlencode(appliedName), true);
      server.send(302, "text/plain", "");
      return;
    }
  }

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

  if (server.hasArg("c1Name")) {
    String n = server.arg("c1Name");
    n.trim();
    gConfig.chamber1.name = n;
  }
  if (server.hasArg("c2Name")) {
    String n = server.arg("c2Name");
    n.trim();
    gConfig.chamber2.name = n;
  }
  if (server.hasArg("c1SoilDry")) {
    int v = server.arg("c1SoilDry").toInt();
    gConfig.chamber1.soilDryThreshold = constrain(v, 0, 100);
  } else if (server.hasArg("c1Dry")) {
    int v = server.arg("c1Dry").toInt();
    gConfig.chamber1.soilDryThreshold = constrain(v, 0, 100);
  }
  if (server.hasArg("c1SoilWet")) {
    int v = server.arg("c1SoilWet").toInt();
    gConfig.chamber1.soilWetThreshold = constrain(v, 0, 100);
  } else if (server.hasArg("c1Wet")) {
    int v = server.arg("c1Wet").toInt();
    gConfig.chamber1.soilWetThreshold = constrain(v, 0, 100);
  }
  if (server.hasArg("c2SoilDry")) {
    int v = server.arg("c2SoilDry").toInt();
    gConfig.chamber2.soilDryThreshold = constrain(v, 0, 100);
  } else if (server.hasArg("c2Dry")) {
    int v = server.arg("c2Dry").toInt();
    gConfig.chamber2.soilDryThreshold = constrain(v, 0, 100);
  }
  if (server.hasArg("c2SoilWet")) {
    int v = server.arg("c2SoilWet").toInt();
    gConfig.chamber2.soilWetThreshold = constrain(v, 0, 100);
  } else if (server.hasArg("c2Wet")) {
    int v = server.arg("c2Wet").toInt();
    gConfig.chamber2.soilWetThreshold = constrain(v, 0, 100);
  }
  if (server.hasArg("c1Prof")) {
    gConfig.chamber1.profileId = server.arg("c1Prof").toInt();
  }
  if (server.hasArg("c2Prof")) {
    gConfig.chamber2.profileId = server.arg("c2Prof").toInt();
  }

  normalizeChamberConfig(gConfig.chamber1, DEFAULT_CHAMBER1_NAME);
  normalizeChamberConfig(gConfig.chamber2, DEFAULT_CHAMBER2_NAME);

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

  if (server.hasArg("tzIndex")) {
    int tz = server.arg("tzIndex").toInt();
    size_t tzCount = greenhouseTimezoneCount();
    if (tz < 0) tz = 0;
    if (tzCount > 0 && (size_t)tz >= tzCount) tz = tzCount - 1;
    if (tz != originalTzIndex) {
      gConfig.tzIndex = tz;
      timezoneChanged = true;
    }
  }

  // Web UI auth: update credentials in memory and NVS
  String newUser = sWebAuthUser;
  String newPass = sWebAuthPass;

  if (server.hasArg("authUser")) {
    String u = server.arg("authUser");
    u.trim();
    newUser = u;
  }

  if (server.hasArg("authPass")) {
    String p = server.arg("authPass");
    p.trim();
    if (p.length() > 0) {
      newPass = p;
    }
  }

  if (newUser.length() == 0) {
    newPass = "";
  }

  sWebAuthUser = newUser;
  sWebAuthPass = newPass;

  saveConfig();
  saveWebAuthConfig(sWebAuthUser, sWebAuthPass);

  if (timezoneChanged) {
    applyTimezoneFromConfig();
  }

  server.sendHeader("Location", "/config", true);
  server.send(302, "text/plain", "");
}

// ================= Not found / captive portal redirect =================

static void handleNotFound() {
  if (sCaptivePortalActive) {
    server.sendHeader("Location", "/wifi", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

// ================= Public API =================

void initWebServer() {
  loadWebAuthConfig(sWebAuthUser, sWebAuthPass);

  bool hasAp        = (WiFi.getMode() & WIFI_MODE_AP);
  bool staConnected = (WiFi.status() == WL_CONNECTED);

  if (hasAp && !staConnected) {
    sCaptivePortalActive = true;
    IPAddress apIP = WiFi.softAPIP();
    dnsServer.start(53, "*", apIP);
  } else {
    sCaptivePortalActive = false;
  }

  server.on("/",                 HTTP_GET,  handleRoot);

  // Legacy endpoints
  server.on("/toggle",           HTTP_GET,  handleToggle);
  server.on("/mode",             HTTP_GET,  handleMode);

  // New JSON endpoints for the richer UI
  server.on("/api/status",       HTTP_GET,  handleStatusApi);
  server.on("/api/toggle",       HTTP_GET,  handleApiToggle);
  server.on("/api/mode",         HTTP_GET,  handleApiMode);
  server.on("/api/grow/apply",   HTTP_GET,  handleApplyProfileChamberApi);

  server.on("/config",           HTTP_GET,  handleConfigGet);
  server.on("/config",           HTTP_POST, handleConfigPost);

  server.on("/wifi",             HTTP_GET,  handleWifiConfigGet);
  server.on("/wifi",             HTTP_POST, handleWifiConfigPost);

  server.on("/api/history",      HTTP_GET,  handleHistoryApi);

  // Static assets (offline)
  server.on("/chart.umd.min.js", HTTP_GET,  handleChartJs);
  server.on("/logo-ezgrow.png",  HTTP_GET,  handleLogoPng);
  server.on("/app.css",          HTTP_GET,  handleAppCss);
  server.on("/app.js",           HTTP_GET,  handleAppJs);

  server.onNotFound(handleNotFound);

  server.begin();
}

void handleWebServer() {
  server.handleClient();
  if (sCaptivePortalActive) {
    dnsServer.processNextRequest();
  }
}
