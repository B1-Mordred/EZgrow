#include "Greenhouse.h"

#include <WiFi.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_Sensor.h>
#include <U8g2lib.h>

// ================= PIN MAPPING (ESP32-4R-A2) =================
static const int RELAY_LIGHT1_PIN = 25;
static const int RELAY_LIGHT2_PIN = 26;
static const int RELAY_FAN_PIN    = 32;
static const int RELAY_PUMP_PIN   = 33;

static const int SOIL1_PIN = 34; // ADC1_CH6
static const int SOIL2_PIN = 35; // ADC1_CH7

static const int I2C_SDA_PIN = 21;
static const int I2C_SCL_PIN = 22;

// Relay active level (LOW = ON on many boards)
static const bool RELAY_ACTIVE_LEVEL   = LOW;
static const bool RELAY_INACTIVE_LEVEL = HIGH;

// ================= WIFI + NTP CONFIG =================
// Compile-time defaults (used only if no NVS credentials found)
static const char* DEFAULT_WIFI_SSID = "YOUR_SSID";
static const char* DEFAULT_WIFI_PASS = "YOUR_PASSWORD";

static const char* NTP_SERVER1 = "pool.ntp.org";
static const char* NTP_SERVER2 = "time.nist.gov";
// CET/CEST example; adjust for your time zone if needed
static const char* TZ_INFO     = "CET-1CEST,M3.5.0,M10.5.0/3";

// ================= GLOBALS =================

GreenhouseConfig gConfig;
SensorState      gSensors;
RelayState       gRelays;

HistorySample gHistoryBuf[HISTORY_SIZE];
size_t        gHistoryIndex = 0;
bool          gHistoryFull  = false;

static unsigned long lastSensorUpdateMs  = 0;
static const unsigned long SENSOR_PERIOD = 2000; // 2 seconds

static unsigned long lastHistoryLogMs = 0;

// Pump internal timing
static bool          pumpRunning    = false;
static unsigned long pumpStartMs    = 0;
static unsigned long lastPumpStopMs = 0;

// Time state
static struct tm gTimeInfo;
static bool      gTimeAvailable        = false;
static unsigned long lastTimeUpdateMs  = 0;
static const unsigned long TIME_UPDATE_INTERVAL_MS = 60000; // 60 s

// Preferences for config persistence
static Preferences prefs;

// Sensors / display
static Adafruit_SHT4x sht4;
// WE-DA-361: 0.91" 128x32 SSD1306 I2C
static U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ================= Helpers =================

static void applyRelay(int pin, bool logicalOn) {
  digitalWrite(pin, logicalOn ? RELAY_ACTIVE_LEVEL : RELAY_INACTIVE_LEVEL);
}

static void syncRelays() {
  applyRelay(RELAY_LIGHT1_PIN, gRelays.light1);
  applyRelay(RELAY_LIGHT2_PIN, gRelays.light2);
  applyRelay(RELAY_FAN_PIN,    gRelays.fan);
  applyRelay(RELAY_PUMP_PIN,   gRelays.pump);
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

// schedule true → ON at nowMin
bool scheduleIsOn(int onMin, int offMin, int nowMin) {
  onMin  = (onMin  + 1440) % 1440;
  offMin = (offMin + 1440) % 1440;
  nowMin = (nowMin + 1440) % 1440;

  if (onMin == offMin) {
    return false; // degenerate, treat as always off
  }

  if (onMin < offMin) {
    // 08:00–20:00
    return (nowMin >= onMin && nowMin < offMin);
  } else {
    // crosses midnight: 20:00–06:00
    return (nowMin >= onMin || nowMin < offMin);
  }
}

bool greenhouseGetTime(struct tm &outTime, bool &available) {
  outTime   = gTimeInfo;
  available = gTimeAvailable;
  return gTimeAvailable;
}

// ================= Wi-Fi credentials (NVS) =================

void loadWifiCredentials(String &ssidOut, String &passOut) {
  ssidOut = "";
  passOut = "";

  if (!prefs.begin("gh_wifi", true)) {
    Serial.println("[WiFiCFG] Preferences begin failed (read)");
  } else {
    String s = prefs.getString("ssid", "");
    String p = prefs.getString("pass", "");
    prefs.end();
    ssidOut = s;
    passOut = p;
  }

  // Fallback to compile-time defaults if no SSID stored
  if (ssidOut.isEmpty() && DEFAULT_WIFI_SSID && strlen(DEFAULT_WIFI_SSID) > 0) {
    ssidOut = DEFAULT_WIFI_SSID;
    passOut = DEFAULT_WIFI_PASS ? DEFAULT_WIFI_PASS : "";
  }

  Serial.print("[WiFiCFG] Using SSID: ");
  Serial.println(ssidOut);
}

void saveWifiCredentials(const String &ssid, const String &password) {
  if (!prefs.begin("gh_wifi", false)) {
    Serial.println("[WiFiCFG] Preferences begin failed (write)");
    return;
  }

  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();

  Serial.print("[WiFiCFG] Saved SSID: ");
  Serial.println(ssid);
}

// ================= Config load/save =================

void loadConfig() {
  // Defaults
  gConfig.env.fanOnTemp        = 28.0f;
  gConfig.env.fanOffTemp       = 26.0f;
  gConfig.env.soilDryThreshold = 35;
  gConfig.env.soilWetThreshold = 45;
  gConfig.env.pumpMinOffSec    = 5 * 60; // 5 minutes
  gConfig.env.pumpMaxOnSec     = 30;     // 30 seconds

  gConfig.light1.onMinutes  = 8 * 60;
  gConfig.light1.offMinutes = 20 * 60;
  gConfig.light1.enabled    = false;

  gConfig.light2.onMinutes  = 8 * 60;
  gConfig.light2.offMinutes = 20 * 60;
  gConfig.light2.enabled    = false;

  gConfig.autoFan  = true;
  gConfig.autoPump = true;

  if (!prefs.begin("gh_cfg", true)) {
    Serial.println("[CFG] Preferences begin failed; using defaults");
    return;
  }

  gConfig.env.fanOnTemp        = prefs.getFloat("fanOn",  gConfig.env.fanOnTemp);
  gConfig.env.fanOffTemp       = prefs.getFloat("fanOff", gConfig.env.fanOffTemp);
  gConfig.env.soilDryThreshold = prefs.getInt  ("soilDry", gConfig.env.soilDryThreshold);
  gConfig.env.soilWetThreshold = prefs.getInt  ("soilWet", gConfig.env.soilWetThreshold);
  gConfig.env.pumpMinOffSec    = prefs.getULong("pumpOff", gConfig.env.pumpMinOffSec);
  gConfig.env.pumpMaxOnSec     = prefs.getULong("pumpOn",  gConfig.env.pumpMaxOnSec);

  gConfig.light1.onMinutes  = prefs.getInt("l1OnMin", gConfig.light1.onMinutes);
  gConfig.light1.offMinutes = prefs.getInt("l1OffMin", gConfig.light1.offMinutes);
  gConfig.light1.enabled    = prefs.getBool("l1Auto", gConfig.light1.enabled);

  gConfig.light2.onMinutes  = prefs.getInt("l2OnMin", gConfig.light2.onMinutes);
  gConfig.light2.offMinutes = prefs.getInt("l2OffMin", gConfig.light2.offMinutes);
  gConfig.light2.enabled    = prefs.getBool("l2Auto", gConfig.light2.enabled);

  gConfig.autoFan  = prefs.getBool("autoFan",  gConfig.autoFan);
  gConfig.autoPump = prefs.getBool("autoPump", gConfig.autoPump);

  prefs.end();

  // Basic sanity checks
  if (gConfig.env.fanOffTemp >= gConfig.env.fanOnTemp) {
    gConfig.env.fanOnTemp  = 28.0f;
    gConfig.env.fanOffTemp = 26.0f;
  }
  gConfig.env.soilDryThreshold = constrain(gConfig.env.soilDryThreshold, 0, 100);
  gConfig.env.soilWetThreshold = constrain(gConfig.env.soilWetThreshold, 0, 100);
  if (gConfig.env.soilWetThreshold <= gConfig.env.soilDryThreshold) {
    gConfig.env.soilDryThreshold = 35;
    gConfig.env.soilWetThreshold = 45;
  }
  if (gConfig.env.pumpMinOffSec < 10) gConfig.env.pumpMinOffSec = 5 * 60;
  if (gConfig.env.pumpMaxOnSec  < 5)  gConfig.env.pumpMaxOnSec  = 30;

  gConfig.light1.onMinutes  = constrain(gConfig.light1.onMinutes,  0, 24 * 60 - 1);
  gConfig.light1.offMinutes = constrain(gConfig.light1.offMinutes, 0, 24 * 60 - 1);
  gConfig.light2.onMinutes  = constrain(gConfig.light2.onMinutes,  0, 24 * 60 - 1);
  gConfig.light2.offMinutes = constrain(gConfig.light2.offMinutes, 0, 24 * 60 - 1);
}

void saveConfig() {
  if (!prefs.begin("gh_cfg", false)) {
    Serial.println("[CFG] Preferences begin failed (write)");
    return;
  }

  prefs.putFloat("fanOn",  gConfig.env.fanOnTemp);
  prefs.putFloat("fanOff", gConfig.env.fanOffTemp);
  prefs.putInt  ("soilDry", gConfig.env.soilDryThreshold);
  prefs.putInt  ("soilWet", gConfig.env.soilWetThreshold);
  prefs.putULong("pumpOff", gConfig.env.pumpMinOffSec);
  prefs.putULong("pumpOn",  gConfig.env.pumpMaxOnSec);

  prefs.putInt ("l1OnMin", gConfig.light1.onMinutes);
  prefs.putInt ("l1OffMin", gConfig.light1.offMinutes);
  prefs.putBool("l1Auto",  gConfig.light1.enabled);

  prefs.putInt ("l2OnMin", gConfig.light2.onMinutes);
  prefs.putInt ("l2OffMin", gConfig.light2.offMinutes);
  prefs.putBool("l2Auto",  gConfig.light2.enabled);

  prefs.putBool("autoFan",  gConfig.autoFan);
  prefs.putBool("autoPump", gConfig.autoPump);

  prefs.end();
}

// ================= Time handling =================

void updateTime() {
  unsigned long nowMs = millis();
  if (nowMs - lastTimeUpdateMs < TIME_UPDATE_INTERVAL_MS) return;
  lastTimeUpdateMs = nowMs;

  struct tm t;
  if (getLocalTime(&t)) {
    gTimeInfo      = t;
    gTimeAvailable = true;
  } else {
    gTimeAvailable = false;
  }
}

// ================= Sensors =================

void updateSensors() {
  unsigned long nowMs = millis();
  if (nowMs - lastSensorUpdateMs < SENSOR_PERIOD) return;
  lastSensorUpdateMs = nowMs;

  // SHT40
  sensors_event_t hum, temp;
  if (sht4.getEvent(&hum, &temp)) {
    gSensors.temperatureC = temp.temperature;
    gSensors.humidityRH   = hum.relative_humidity;
  } else {
    gSensors.temperatureC = NAN;
    gSensors.humidityRH   = NAN;
  }

  // Soil sensors: raw 0..4095 -> 0..100 % (rough approximation)
  int raw1 = analogRead(SOIL1_PIN);
  int raw2 = analogRead(SOIL2_PIN);

  gSensors.soil1Percent = map(raw1, 0, 4095, 100, 0);
  gSensors.soil2Percent = map(raw2, 0, 4095, 100, 0);

  gSensors.soil1Percent = constrain(gSensors.soil1Percent, 0, 100);
  gSensors.soil2Percent = constrain(gSensors.soil2Percent, 0, 100);
}

// ================= Control logic =================

void updateControlLogic() {
  unsigned long nowMs = millis();

  // Light schedules
  if (gTimeAvailable) {
    int nowMin = gTimeInfo.tm_hour * 60 + gTimeInfo.tm_min;

    if (gConfig.light1.enabled) {
      gRelays.light1 = scheduleIsOn(gConfig.light1.onMinutes,
                                    gConfig.light1.offMinutes,
                                    nowMin);
    }
    if (gConfig.light2.enabled) {
      gRelays.light2 = scheduleIsOn(gConfig.light2.onMinutes,
                                    gConfig.light2.offMinutes,
                                    nowMin);
    }
  }

  // Fan (auto by temperature)
  if (gConfig.autoFan && !isnan(gSensors.temperatureC)) {
    if (!gRelays.fan && gSensors.temperatureC >= gConfig.env.fanOnTemp) {
      gRelays.fan = true;
    } else if (gRelays.fan && gSensors.temperatureC <= gConfig.env.fanOffTemp) {
      gRelays.fan = false;
    }
  }

  // Pump (auto by soil moisture + timing)
  if (gConfig.autoPump) {
    bool tooDry    = (gSensors.soil1Percent < gConfig.env.soilDryThreshold) ||
                     (gSensors.soil2Percent < gConfig.env.soilDryThreshold);
    bool wetEnough = (gSensors.soil1Percent > gConfig.env.soilWetThreshold) &&
                     (gSensors.soil2Percent > gConfig.env.soilWetThreshold);

    if (!pumpRunning) {
      if (tooDry && (nowMs - lastPumpStopMs > gConfig.env.pumpMinOffSec * 1000UL)) {
        pumpRunning = true;
        pumpStartMs = nowMs;
        gRelays.pump = true;
      }
    } else {
      if (wetEnough || (nowMs - pumpStartMs > gConfig.env.pumpMaxOnSec * 1000UL)) {
        pumpRunning    = false;
        gRelays.pump   = false;
        lastPumpStopMs = nowMs;
      }
    }
  }

  syncRelays();
}

// ================= Display =================

void updateDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  // Line 1: T/H
  u8g2.setCursor(0, 10);
  u8g2.print("T:");
  if (!isnan(gSensors.temperatureC)) {
    u8g2.print(gSensors.temperatureC, 1);
    u8g2.print("C ");
  } else {
    u8g2.print("--.-C ");
  }
  u8g2.print("H:");
  if (!isnan(gSensors.humidityRH)) {
    u8g2.print((int)gSensors.humidityRH);
    u8g2.print("%");
  } else {
    u8g2.print("--%");
  }

  // Line 2: Soil
  u8g2.setCursor(0, 20);
  u8g2.print("S1:");
  u8g2.print(gSensors.soil1Percent);
  u8g2.print("% S2:");
  u8g2.print(gSensors.soil2Percent);
  u8g2.print("%");

  // Line 3: Relays & modes (L1 L2 F P)
  u8g2.setCursor(0, 30);
  u8g2.print("L1:");
  u8g2.print(gRelays.light1 ? "1" : "0");
  u8g2.print(gConfig.light1.enabled ? "A " : "M ");

  u8g2.print("L2:");
  u8g2.print(gRelays.light2 ? "1" : "0");
  u8g2.print(gConfig.light2.enabled ? "A " : "M ");

  u8g2.print("F:");
  u8g2.print(gRelays.fan ? "1" : "0");
  u8g2.print(gConfig.autoFan ? "A " : "M ");

  u8g2.print("P:");
  u8g2.print(gRelays.pump ? "1" : "0");
  u8g2.print(gConfig.autoPump ? "A" : "M");

  u8g2.sendBuffer();
}

// ================= History logging =================

void logHistorySample() {
  unsigned long nowMs = millis();
  if (nowMs - lastHistoryLogMs < HISTORY_INTERVAL_MS) return;
  lastHistoryLogMs = nowMs;

  HistorySample &s = gHistoryBuf[gHistoryIndex];

  if (gTimeAvailable) {
    time_t nowSec;
    time(&nowSec);
    s.timestamp = nowSec;
  } else {
    s.timestamp = 0;
  }

  s.temp   = gSensors.temperatureC;
  s.hum    = gSensors.humidityRH;
  s.light1 = gRelays.light1;
  s.light2 = gRelays.light2;

  gHistoryIndex++;
  if (gHistoryIndex >= HISTORY_SIZE) {
    gHistoryIndex = 0;
    gHistoryFull  = true;
  }
}

// ================= Hardware init =================

void initHardware() {
  // GPIO
  pinMode(RELAY_LIGHT1_PIN, OUTPUT);
  pinMode(RELAY_LIGHT2_PIN, OUTPUT);
  pinMode(RELAY_FAN_PIN,    OUTPUT);
  pinMode(RELAY_PUMP_PIN,   OUTPUT);

  gRelays.light1 = gRelays.light2 = gRelays.fan = gRelays.pump = false;
  syncRelays();

  // LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed");
  } else {
    Serial.println("[FS] LittleFS mounted");
  }

  // Config
  loadConfig();

  // I2C
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // SHT40
  if (!sht4.begin()) {
    Serial.println("[SHT40] Not found");
  } else {
    Serial.println("[SHT40] OK");
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
  }

  // OLED
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Greenhouse boot...");
  u8g2.sendBuffer();

  // Wi-Fi credentials from NVS (or defaults)
  String ssid, pass;
  loadWifiCredentials(ssid, pass);

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  if (!ssid.isEmpty()) {
    Serial.print("[WiFi] Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    Serial.println("[WiFi] No SSID configured");
  }

  unsigned long start = millis();
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected, IP: ");
    Serial.println(WiFi.localIP());

    // Show IP on display
    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print("IP:");
    u8g2.setCursor(0, 20);
    u8g2.print(WiFi.localIP().toString().c_str());
    u8g2.sendBuffer();
    delay(2000);
  } else {
    Serial.println("[WiFi] Failed to connect");
    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print("WiFi not conn.");
    u8g2.sendBuffer();
    delay(2000);
  }

  // NTP / time zone
  setenv("TZ", TZ_INFO, 1);
  tzset();
  configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
}
