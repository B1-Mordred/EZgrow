#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>

#include <Adafruit_SHT4x.h>
#include <Adafruit_Sensor.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <time.h>

// ================= PIN MAPPING (LC-Relay-ESP32-4R-A2) =================
const int RELAY_LIGHT1 = 25;
const int RELAY_LIGHT2 = 26;
const int RELAY_FAN    = 32;
const int RELAY_PUMP   = 33;

// Analog soil moisture (ADC1 -> safe with WiFi)
const int SOIL1_PIN = 34; // ADC1_CH6
const int SOIL2_PIN = 35; // ADC1_CH7

// I2C pins (SHT40 + WE-DA-361 OLED share this bus)
const int I2C_SDA = 21;
const int I2C_SCL = 22;

// Relay active level (most ESP32 relay boards are active LOW)
const bool RELAY_ACTIVE_LEVEL   = LOW;   // LOW = relay energized
const bool RELAY_INACTIVE_LEVEL = !RELAY_ACTIVE_LEVEL;

// ================= WIFI + NTP CONFIG =================
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.nist.gov";
// Europe/Berlin style TZ (CET/CEST)
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";

WebServer server(80);

// ================= SENSORS & DISPLAY =================
Adafruit_SHT4x sht4;

// WE-DA-361: 0.91" 128x32 I2C OLED, usually SSD1306 128x32
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ================= CONFIG STORAGE =================
Preferences prefs;

// ---------- Defaults ----------
const float         FAN_ON_TEMP_DEFAULT        = 28.0;
const float         FAN_OFF_TEMP_DEFAULT       = 26.0;
const int           SOIL_DRY_THRESHOLD_DEFAULT = 35;       // %
const int           SOIL_WET_THRESHOLD_DEFAULT = 45;       // %
const unsigned long PUMP_MIN_OFF_SEC_DEFAULT   = 5 * 60;   // 5 min
const unsigned long PUMP_MAX_ON_SEC_DEFAULT    = 30;       // 30 s

// Light schedule defaults (minutes since midnight)
const int L1_ON_MIN_DEFAULT  = 8  * 60;  // 08:00
const int L1_OFF_MIN_DEFAULT = 20 * 60;  // 20:00
const int L2_ON_MIN_DEFAULT  = 8  * 60;  // 08:00
const int L2_OFF_MIN_DEFAULT = 20 * 60;  // 20:00

// ---------- Runtime configuration (changeable via web UI) ----------
float         fanOnTemp        = FAN_ON_TEMP_DEFAULT;
float         fanOffTemp       = FAN_OFF_TEMP_DEFAULT;
int           soilDryThreshold = SOIL_DRY_THRESHOLD_DEFAULT;
int           soilWetThreshold = SOIL_WET_THRESHOLD_DEFAULT;
unsigned long pumpMinOffSec    = PUMP_MIN_OFF_SEC_DEFAULT;
unsigned long pumpMaxOnSec     = PUMP_MAX_ON_SEC_DEFAULT;

// Light schedule config
bool autoLight1 = false;
bool autoLight2 = false;
int  l1OnMin    = L1_ON_MIN_DEFAULT;
int  l1OffMin   = L1_OFF_MIN_DEFAULT;
int  l2OnMin    = L2_ON_MIN_DEFAULT;
int  l2OffMin   = L2_OFF_MIN_DEFAULT;

// Helpers to get ms from sec
inline unsigned long pumpMinOffMs() { return pumpMinOffSec * 1000UL; }
inline unsigned long pumpMaxOnMs()  { return pumpMaxOnSec * 1000UL;  }

// ================= RUNTIME STATE =================
float temperatureC = NAN;
float humidityRH   = NAN;
int   soil1Percent = 0;
int   soil2Percent = 0;

bool light1State = false;
bool light2State = false;
bool fanState    = false;
bool pumpState   = false;

bool autoFan  = true;
bool autoPump = true;

// Pump timing state
bool          pumpRunning     = false;
unsigned long pumpStartMs     = 0;
unsigned long lastPumpStopMs  = 0;

// Sensor read interval
unsigned long lastSensorUpdateMs  = 0;
const unsigned long SENSOR_PERIOD = 2000; // 2 seconds

// Time handling
struct tm timeInfo;
bool timeAvailable          = false;
unsigned long lastTimeUpdateMs = 0;
const unsigned long TIME_UPDATE_INTERVAL = 60000; // 60 s

// ================= HELPER: TIME & SCHEDULE =================
void updateTime() {
  unsigned long now = millis();
  if (now - lastTimeUpdateMs < TIME_UPDATE_INTERVAL) return;
  lastTimeUpdateMs = now;

  if (getLocalTime(&timeInfo)) {
    timeAvailable = true;
  } else {
    timeAvailable = false;
  }
}

String minutesToTimeStr(int minutes) {
  if (minutes < 0) minutes = 0;
  minutes = minutes % (24 * 60);
  int h = minutes / 60;
  int m = minutes % 60;
  char buf[6];
  sprintf(buf, "%02d:%02d", h, m);
  return String(buf);
}

int parseTimeToMinutes(const String& s, int fallback) {
  int colon = s.indexOf(':');
  if (colon < 0) return fallback;
  int h = s.substring(0, colon).toInt();
  int m = s.substring(colon + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

// true if schedule is ON at nowMin
bool scheduleIsOn(int onMin, int offMin, int nowMin) {
  onMin  = (onMin  + 1440) % 1440;
  offMin = (offMin + 1440) % 1440;
  nowMin = (nowMin + 1440) % 1440;

  if (onMin == offMin) {
    return false; // degenerate, treat as always off
  }

  if (onMin < offMin) {
    // normal: e.g. 08:00–20:00
    return (nowMin >= onMin && nowMin < offMin);
  } else {
    // crosses midnight: e.g. 20:00–06:00
    return (nowMin >= onMin || nowMin < offMin);
  }
}

// ================= RELAY HELPERS =================
void applyRelay(int pin, bool logicalOn) {
  digitalWrite(pin, logicalOn ? RELAY_ACTIVE_LEVEL : RELAY_INACTIVE_LEVEL);
}

void syncRelays() {
  applyRelay(RELAY_LIGHT1, light1State);
  applyRelay(RELAY_LIGHT2, light2State);
  applyRelay(RELAY_FAN,    fanState);
  applyRelay(RELAY_PUMP,   pumpState);
}

// ================= CONFIG LOAD/SAVE =================
void loadConfig() {
  prefs.begin("gh_cfg", true); // read-only
  fanOnTemp        = prefs.getFloat("fanOn",  FAN_ON_TEMP_DEFAULT);
  fanOffTemp       = prefs.getFloat("fanOff", FAN_OFF_TEMP_DEFAULT);
  soilDryThreshold = prefs.getInt("soilDry",  SOIL_DRY_THRESHOLD_DEFAULT);
  soilWetThreshold = prefs.getInt("soilWet",  SOIL_WET_THRESHOLD_DEFAULT);
  pumpMinOffSec    = prefs.getULong("pumpOff", PUMP_MIN_OFF_SEC_DEFAULT);
  pumpMaxOnSec     = prefs.getULong("pumpOn",  PUMP_MAX_ON_SEC_DEFAULT);

  autoLight1 = prefs.getBool("l1Auto", false);
  autoLight2 = prefs.getBool("l2Auto", false);
  l1OnMin    = prefs.getInt("l1OnMin", L1_ON_MIN_DEFAULT);
  l1OffMin   = prefs.getInt("l1OffMin", L1_OFF_MIN_DEFAULT);
  l2OnMin    = prefs.getInt("l2OnMin", L2_ON_MIN_DEFAULT);
  l2OffMin   = prefs.getInt("l2OffMin", L2_OFF_MIN_DEFAULT);
  prefs.end();

  // Basic sanity checks
  if (fanOffTemp >= fanOnTemp) {
    fanOnTemp  = FAN_ON_TEMP_DEFAULT;
    fanOffTemp = FAN_OFF_TEMP_DEFAULT;
  }
  soilDryThreshold = constrain(soilDryThreshold, 0, 100);
  soilWetThreshold = constrain(soilWetThreshold, 0, 100);
  if (soilWetThreshold <= soilDryThreshold) {
    soilDryThreshold = SOIL_DRY_THRESHOLD_DEFAULT;
    soilWetThreshold = SOIL_WET_THRESHOLD_DEFAULT;
  }
  if (pumpMinOffSec < 10)   pumpMinOffSec = PUMP_MIN_OFF_SEC_DEFAULT;
  if (pumpMaxOnSec  < 5)    pumpMaxOnSec  = PUMP_MAX_ON_SEC_DEFAULT;

  l1OnMin  = constrain(l1OnMin,  0, 24*60-1);
  l1OffMin = constrain(l1OffMin, 0, 24*60-1);
  l2OnMin  = constrain(l2OnMin,  0, 24*60-1);
  l2OffMin = constrain(l2OffMin, 0, 24*60-1);
}

void saveConfig() {
  prefs.begin("gh_cfg", false); // read-write
  prefs.putFloat("fanOn",  fanOnTemp);
  prefs.putFloat("fanOff", fanOffTemp);
  prefs.putInt("soilDry",  soilDryThreshold);
  prefs.putInt("soilWet",  soilWetThreshold);
  prefs.putULong("pumpOff", pumpMinOffSec);
  prefs.putULong("pumpOn",  pumpMaxOnSec);

  prefs.putBool("l1Auto",  autoLight1);
  prefs.putBool("l2Auto",  autoLight2);
  prefs.putInt("l1OnMin",  l1OnMin);
  prefs.putInt("l1OffMin", l1OffMin);
  prefs.putInt("l2OnMin",  l2OnMin);
  prefs.putInt("l2OffMin", l2OffMin);
  prefs.end();
}

// ================= SENSORS =================
void updateSensors() {
  unsigned long now = millis();
  if (now - lastSensorUpdateMs < SENSOR_PERIOD) return;
  lastSensorUpdateMs = now;

  // SHT40 (temperature + humidity)
  sensors_event_t hum, temp;
  if (sht4.getEvent(&hum, &temp)) {
    temperatureC = temp.temperature;
    humidityRH   = hum.relative_humidity;
  } else {
    temperatureC = NAN;
    humidityRH   = NAN;
  }

  // Soil sensors: raw 0..4095 -> 0..100% (rough mapping, calibrate in practice)
  int raw1 = analogRead(SOIL1_PIN);
  int raw2 = analogRead(SOIL2_PIN);

  soil1Percent = map(raw1, 0, 4095, 100, 0);
  soil2Percent = map(raw2, 0, 4095, 100, 0);

  soil1Percent = constrain(soil1Percent, 0, 100);
  soil2Percent = constrain(soil2Percent, 0, 100);
}

// ================= CONTROL LOGIC =================
void updateControlLogic() {
  unsigned long nowMs = millis();

  // ----- Light schedules -----
  if (timeAvailable) {
    int nowMin = timeInfo.tm_hour * 60 + timeInfo.tm_min;
    if (autoLight1) {
      bool on = scheduleIsOn(l1OnMin, l1OffMin, nowMin);
      light1State = on;
    }
    if (autoLight2) {
      bool on = scheduleIsOn(l2OnMin, l2OffMin, nowMin);
      light2State = on;
    }
  }

  // ----- Fan (auto mode) -----
  if (autoFan && !isnan(temperatureC)) {
    if (!fanState && temperatureC >= fanOnTemp) {
      fanState = true;
    } else if (fanState && temperatureC <= fanOffTemp) {
      fanState = false;
    }
  }

  // ----- Pump (auto mode) -----
  if (autoPump) {
    bool tooDry    = (soil1Percent < soilDryThreshold) || (soil2Percent < soilDryThreshold);
    bool wetEnough = (soil1Percent > soilWetThreshold) && (soil2Percent > soilWetThreshold);

    if (!pumpRunning) {
      if (tooDry && (nowMs - lastPumpStopMs > pumpMinOffMs())) {
        pumpRunning = true;
        pumpStartMs = nowMs;
        pumpState   = true;
      }
    } else {
      if (wetEnough || (nowMs - pumpStartMs > pumpMaxOnMs())) {
        pumpRunning    = false;
        pumpState      = false;
        lastPumpStopMs = nowMs;
      }
    }
  }

  syncRelays();
}

// ================= DISPLAY (128x32) =================
void updateDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  // Line 1: Temperature / Humidity
  u8g2.setCursor(0, 10);
  u8g2.print("T:");
  if (!isnan(temperatureC)) {
    u8g2.print(temperatureC, 1);
    u8g2.print("C ");
  } else {
    u8g2.print("--.-C ");
  }
  u8g2.print("H:");
  if (!isnan(humidityRH)) {
    u8g2.print((int)humidityRH);
    u8g2.print("%");
  } else {
    u8g2.print("--%");
  }

  // Line 2: Soil moisture
  u8g2.setCursor(0, 20);
  u8g2.print("S1:");
  u8g2.print(soil1Percent);
  u8g2.print("% S2:");
  u8g2.print(soil2Percent);
  u8g2.print("%");

  // Line 3: Relays & modes (L1 L2 F P)
  u8g2.setCursor(0, 30);
  u8g2.print("L1:");
  u8g2.print(light1State ? "1" : "0");
  u8g2.print(autoLight1 ? "A " : "M ");

  u8g2.print("L2:");
  u8g2.print(light2State ? "1" : "0");
  u8g2.print(autoLight2 ? "A " : "M ");

  u8g2.print("F:");
  u8g2.print(fanState ? "1" : "0");
  u8g2.print(autoFan ? "A " : "M ");

  u8g2.print("P:");
  u8g2.print(pumpState ? "1" : "0");
  u8g2.print(autoPump ? "A" : "M");

  u8g2.sendBuffer();
}

// ================= WEB UI =================
String htmlBool(bool b) { return b ? "ON" : "OFF"; }
String htmlAuto(bool a) { return a ? "AUTO" : "MAN"; }

// ---- main status page ----
void handleRoot() {
  String page;
  page.reserve(5000);

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

  if (timeAvailable) {
    char buf[32];
    sprintf(buf, "%02d:%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
    page += String("<p>Time: ") + buf + "</p>";
  } else {
    page += "<p>Time: syncing...</p>";
  }

  // Sensors
  page += "<div class='card'><h2>Sensors</h2><ul>";
  page += "<li>Temperature: ";
  if (!isnan(temperatureC)) page += String(temperatureC, 1) + " &deg;C";
  else page += "N/A";
  page += "</li>";

  page += "<li>Humidity: ";
  if (!isnan(humidityRH)) page += String(humidityRH, 0) + " %";
  else page += "N/A";
  page += "</li>";

  page += "<li>Soil 1: " + String(soil1Percent) + " %</li>";
  page += "<li>Soil 2: " + String(soil2Percent) + " %</li>";
  page += "</ul></div>";

  // Relays & modes
  page += "<div class='card'><h2>Relays</h2>";

  // Light 1
  page += "<p>Light 1: " + htmlBool(light1State) + " (" + htmlAuto(autoLight1) + ") ";
  if (autoLight1) {
    page += "<a href='/mode?id=light1&auto=0'><button>Switch to MANUAL</button></a>";
  } else {
    page += "<a href='/toggle?id=light1'><button>Toggle</button></a>";
    page += "<a href='/mode?id=light1&auto=1'><button>Switch to AUTO</button></a>";
  }
  page += "<br><small>Schedule: ";
  page += minutesToTimeStr(l1OnMin) + "–" + minutesToTimeStr(l1OffMin) + "</small></p>";

  // Light 2
  page += "<p>Light 2: " + htmlBool(light2State) + " (" + htmlAuto(autoLight2) + ") ";
  if (autoLight2) {
    page += "<a href='/mode?id=light2&auto=0'><button>Switch to MANUAL</button></a>";
  } else {
    page += "<a href='/toggle?id=light2'><button>Toggle</button></a>";
    page += "<a href='/mode?id=light2&auto=1'><button>Switch to AUTO</button></a>";
  }
  page += "<br><small>Schedule: ";
  page += minutesToTimeStr(l2OnMin) + "–" + minutesToTimeStr(l2OffMin) + "</small></p>";

  // Fan
  page += "<p>Fan: " + htmlBool(fanState) + " (" + htmlAuto(autoFan) + ") ";
  if (autoFan) {
    page += "<a href='/mode?id=fan&auto=0'><button>Switch to MANUAL</button></a>";
  } else {
    page += "<a href='/toggle?id=fan'><button>Toggle</button></a>";
    page += "<a href='/mode?id=fan&auto=1'><button>Switch to AUTO</button></a>";
  }
  page += "</p>";

  // Pump
  page += "<p>Pump: " + htmlBool(pumpState) + " (" + htmlAuto(autoPump) + ") ";
  if (autoPump) {
    page += "<a href='/mode?id=pump&auto=0'><button>Switch to MANUAL</button></a>";
  } else {
    page += "<a href='/toggle?id=pump'><button>Toggle</button></a>";
    page += "<a href='/mode?id=pump&auto=1'><button>Switch to AUTO</button></a>";
  }
  page += "</p>";

  page += "<p><small>Fan: ON &ge; " + String(fanOnTemp, 1) +
          " &deg;C, OFF &le; " + String(fanOffTemp, 1) +
          " &deg;C. Pump: dry &lt; " + String(soilDryThreshold) +
          "%, wet &gt; " + String(soilWetThreshold) + "%.</small></p>";

  page += "</div>";

  page += "</body></html>";

  server.send(200, "text/html", page);
}

// ---- toggle relays (only if manual) ----
void handleToggle() {
  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Missing id");
    return;
  }
  String id = server.arg("id");

  if (id == "light1" && !autoLight1) {
    light1State = !light1State;
  } else if (id == "light2" && !autoLight2) {
    light2State = !light2State;
  } else if (id == "fan" && !autoFan) {
    fanState = !fanState;
  } else if (id == "pump" && !autoPump) {
    pumpState = !pumpState;
  }

  syncRelays();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ---- change auto/manual mode ----
void handleMode() {
  if (!server.hasArg("id") || !server.hasArg("auto")) {
    server.send(400, "text/plain", "Missing args");
    return;
  }
  String id   = server.arg("id");
  bool autoOn = (server.arg("auto") == "1");

  if (id == "fan")       autoFan    = autoOn;
  else if (id == "pump") autoPump   = autoOn;
  else if (id == "light1") autoLight1 = autoOn;
  else if (id == "light2") autoLight2 = autoOn;

  saveConfig(); // persist mode change
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ---- configuration form (GET) ----
void handleConfigGet() {
  String page;
  page.reserve(5000);

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

  // Fan & pump thresholds
  page += "<div class='card'><h2>Environment thresholds</h2>";
  page += "<form method='POST' action='/config'>";

  page += "<label>Fan ON temperature (&deg;C):<br>";
  page += "<input type='number' step='0.1' name='fanOn' value='" + String(fanOnTemp, 1) + "'></label>";

  page += "<label>Fan OFF temperature (&deg;C):<br>";
  page += "<input type='number' step='0.1' name='fanOff' value='" + String(fanOffTemp, 1) + "'></label>";

  page += "<label>Soil DRY threshold (%):<br>";
  page += "<input type='number' step='1' name='soilDry' value='" + String(soilDryThreshold) + "'></label>";

  page += "<label>Soil WET threshold (%):<br>";
  page += "<input type='number' step='1' name='soilWet' value='" + String(soilWetThreshold) + "'></label>";

  page += "<label>Pump minimum OFF time (seconds):<br>";
  page += "<input type='number' step='1' name='pumpOff' value='" + String(pumpMinOffSec) + "'></label>";

  page += "<label>Pump maximum ON time (seconds):<br>";
  page += "<input type='number' step='1' name='pumpOn' value='" + String(pumpMaxOnSec) + "'></label>";

  // Light schedules
  page += "<h2>Light schedules</h2>";

  // Light 1
  page += "<label><input type='checkbox' name='l1Auto' value='1'";
  if (autoLight1) page += " checked";
  page += "> Use schedule for Light 1</label>";

  page += "<label>Light 1 ON time:<br>";
  page += "<input type='time' name='l1On' value='" + minutesToTimeStr(l1OnMin) + "'></label>";

  page += "<label>Light 1 OFF time:<br>";
  page += "<input type='time' name='l1Off' value='" + minutesToTimeStr(l1OffMin) + "'></label>";

  // Light 2
  page += "<label><input type='checkbox' name='l2Auto' value='1'";
  if (autoLight2) page += " checked";
  page += "> Use schedule for Light 2</label>";

  page += "<label>Light 2 ON time:<br>";
  page += "<input type='time' name='l2On' value='" + minutesToTimeStr(l2OnMin) + "'></label>";

  page += "<label>Light 2 OFF time:<br>";
  page += "<input type='time' name='l2Off' value='" + minutesToTimeStr(l2OffMin) + "'></label>";

  page += "<button type='submit'>Save</button>";
  page += "</form></div>";

  page += "</body></html>";

  server.send(200, "text/html", page);
}

// ---- configuration form (POST) ----
void handleConfigPost() {
  // Thresholds
  if (server.hasArg("fanOn")) {
    float v = server.arg("fanOn").toFloat();
    if (v > 0 && v < 80) fanOnTemp = v;
  }
  if (server.hasArg("fanOff")) {
    float v = server.arg("fanOff").toFloat();
    if (v > 0 && v < 80) fanOffTemp = v;
  }
  if (fanOffTemp >= fanOnTemp) {
    fanOnTemp  = FAN_ON_TEMP_DEFAULT;
    fanOffTemp = FAN_OFF_TEMP_DEFAULT;
  }

  if (server.hasArg("soilDry")) {
    int v = server.arg("soilDry").toInt();
    soilDryThreshold = constrain(v, 0, 100);
  }
  if (server.hasArg("soilWet")) {
    int v = server.arg("soilWet").toInt();
    soilWetThreshold = constrain(v, 0, 100);
  }
  if (soilWetThreshold <= soilDryThreshold) {
    soilDryThreshold = SOIL_DRY_THRESHOLD_DEFAULT;
    soilWetThreshold = SOIL_WET_THRESHOLD_DEFAULT;
  }

  if (server.hasArg("pumpOff")) {
    unsigned long v = server.arg("pumpOff").toInt();
    if (v >= 10 && v <= 36000) pumpMinOffSec = v; // 10s..10h
  }
  if (server.hasArg("pumpOn")) {
    unsigned long v = server.arg("pumpOn").toInt();
    if (v >= 5 && v <= 3600) pumpMaxOnSec = v; // 5s..1h
  }

  // Light schedules
  autoLight1 = server.hasArg("l1Auto");
  autoLight2 = server.hasArg("l2Auto");

  if (server.hasArg("l1On")) {
    l1OnMin = parseTimeToMinutes(server.arg("l1On"), l1OnMin);
  }
  if (server.hasArg("l1Off")) {
    l1OffMin = parseTimeToMinutes(server.arg("l1Off"), l1OffMin);
  }
  if (server.hasArg("l2On")) {
    l2OnMin = parseTimeToMinutes(server.arg("l2On"), l2OnMin);
  }
  if (server.hasArg("l2Off")) {
    l2OffMin = parseTimeToMinutes(server.arg("l2Off"), l2OffMin);
  }

  // basic sanity: if on==off, revert to defaults for that light
  if (l1OnMin == l1OffMin) {
    l1OnMin  = L1_ON_MIN_DEFAULT;
    l1OffMin = L1_OFF_MIN_DEFAULT;
  }
  if (l2OnMin == l2OffMin) {
    l2OnMin  = L2_ON_MIN_DEFAULT;
    l2OffMin = L2_OFF_MIN_DEFAULT;
  }

  saveConfig();

  server.sendHeader("Location", "/config", true);
  server.send(302, "text/plain", "");
}

// ================= SETUP & LOOP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Load config from NVS
  loadConfig();

  // GPIO init
  pinMode(RELAY_LIGHT1, OUTPUT);
  pinMode(RELAY_LIGHT2, OUTPUT);
  pinMode(RELAY_FAN,    OUTPUT);
  pinMode(RELAY_PUMP,   OUTPUT);

  light1State = light2State = fanState = pumpState = false;
  syncRelays();

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // SHT40 init
  if (!sht4.begin()) {
    Serial.println("ERROR: SHT40 not found");
  } else {
    Serial.println("SHT40 OK");
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
  }

  // OLED init
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Greenhouse boot...");
  u8g2.sendBuffer();

  // Wi-Fi (station mode)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // NTP / timezone
  setenv("TZ", TZ_INFO, 1);
  tzset();
  configTime(0, 0, NTP_SERVER1, NTP_SERVER2);

  // Web server routes
  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/toggle",  HTTP_GET,  handleToggle);
  server.on("/mode",    HTTP_GET,  handleMode);
  server.on("/config",  HTTP_GET,  handleConfigGet);
  server.on("/config",  HTTP_POST, handleConfigPost);
  server.begin();

  // Show IP on display briefly
  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("IP:");
  u8g2.setCursor(0, 20);
  u8g2.print(WiFi.localIP().toString().c_str());
  u8g2.sendBuffer();
  delay(2000);
}

void loop() {
  server.handleClient();
  updateTime();
  updateSensors();
  updateControlLogic();
  updateDisplay();
}
