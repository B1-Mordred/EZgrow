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

static const unsigned long WIFI_CONNECT_TIMEOUT_MS   = 15000;
static const unsigned long WIFI_RETRY_INTERVAL_MS    = 60000;
static const unsigned long WIFI_AP_RESTART_DELAY_MS  = 120000;

static const char* NTP_SERVER1 = "pool.ntp.org";
static const char* NTP_SERVER2 = "time.nist.gov";

struct TzOption {
  const char* label;
  const char* iana;
  const char* tz;
};

static const TzOption TZ_OPTIONS[] = {
  { "UTC",           "UTC",             "UTC0" },
  { "Europe/Berlin", "Europe/Berlin",   "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Europe/London", "Europe/London",   "GMT0BST,M3.5.0/1,M10.5.0" },
  { "US/Eastern",    "America/New_York",    "EST5EDT,M3.2.0,M11.1.0" },
  { "US/Central",    "America/Chicago",     "CST6CDT,M3.2.0,M11.1.0" },
  { "US/Mountain",   "America/Denver",      "MST7MDT,M3.2.0,M11.1.0" },
  { "US/Pacific",    "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0" },
};
static const size_t TZ_COUNT = sizeof(TZ_OPTIONS) / sizeof(TZ_OPTIONS[0]);


// ================= GLOBALS =================

GreenhouseConfig gConfig;
SensorState      gSensors;
RelayState       gRelays;

static String       sWifiSsid;
static String       sWifiPass;
static bool         sApStarted             = false;
static bool         sStaAttemptInProgress  = false;
static unsigned long sStaAttemptStartMs    = 0;
static unsigned long sLastStaAttemptMs     = 0;
static unsigned long sDisconnectedSinceMs  = 0;
static wl_status_t   sLastWifiStatus       = WL_DISCONNECTED;

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
static uint8_t       pumpActiveDryMask = 0;
static unsigned long pumpDryStartMs = 0;

// Automation hold timing
static const unsigned long FAN_TRIGGER_HOLD_MS  = 120000UL;
static const unsigned long PUMP_TRIGGER_HOLD_MS = 120000UL;
static unsigned long fanTriggerStartMs = 0;

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

static String sanitizeChamberName(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '<' || c == '>' || (uint8_t)c < 0x20) continue;
    out += c;
  }
  out.trim();
  return out;
}

bool normalizeChamberConfig(ChamberConfig &c, const char* defaultName) {
  bool changed = false;

  String originalName = c.name;
  c.name = sanitizeChamberName(c.name);
  if (c.name != originalName) {
    changed = true;
  }
  if (c.name.length() < 1 || c.name.length() > 24) {
    c.name = defaultName;
    changed = true;
  }

  int originalDry = c.soilDryThreshold;
  int originalWet = c.soilWetThreshold;

  c.soilDryThreshold = constrain(c.soilDryThreshold, 0, 100);
  c.soilWetThreshold = constrain(c.soilWetThreshold, 0, 100);

  if (c.soilWetThreshold <= c.soilDryThreshold) {
    c.soilDryThreshold = DEFAULT_SOIL_DRY;
    c.soilWetThreshold = DEFAULT_SOIL_WET;
    changed = true;
  } else if (originalDry != c.soilDryThreshold || originalWet != c.soilWetThreshold) {
    changed = true;
  }

  if (c.profileId < -1) {
    c.profileId = -1;
    changed = true;
  }

  return changed;
}

bool greenhouseGetTime(struct tm &outTime, bool &available) {
  outTime   = gTimeInfo;
  available = gTimeAvailable;
  return gTimeAvailable;
}

static const TzOption& currentTzOption() {
  int idx = gConfig.tzIndex;
  if (idx < 0) idx = 0;
  if ((size_t)idx >= TZ_COUNT) idx = TZ_COUNT - 1;
  return TZ_OPTIONS[idx];
}

const char* greenhouseTimezoneLabel() {
  return currentTzOption().label;
}

const char* greenhouseTimezoneIana() {
  return currentTzOption().iana;
}

const char* greenhouseTimezoneLabelAt(size_t idx) {
  if (idx >= TZ_COUNT) return "";
  return TZ_OPTIONS[idx].label;
}

const char* greenhouseTimezoneIanaAt(size_t idx) {
  if (idx >= TZ_COUNT) return "";
  return TZ_OPTIONS[idx].iana;
}

size_t greenhouseTimezoneCount() {
  return TZ_COUNT;
}

// ================= Wi-Fi credentials (NVS) =================

void loadWifiCredentials(String &ssidOut, String &passOut, bool logSsid) {
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

  if (logSsid) {
    Serial.print("[WiFiCFG] Using SSID: ");
    Serial.println(ssidOut);
  }
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

// ================= Web UI authentication (NVS) =================

// Namespace: "gh_auth"
// Keys: "user", "pass"
// Default: "admin" / "admin"
// If username is empty after load, auth is treated as disabled.

void loadWebAuthConfig(String &userOut, String &passOut) {
  // Defaults
  userOut = "admin";
  passOut = "admin";

  if (!prefs.begin("gh_auth", true)) {
    Serial.println("[AUTH] Preferences begin failed (read), using defaults");
    return;
  }

  String u = prefs.getString("user", userOut);
  String p = prefs.getString("pass", passOut);
  prefs.end();

  // If nothing stored, this keeps defaults "admin"/"admin"
  userOut = u;
  passOut = p;

  // If user is empty, we treat auth as disabled (no Basic Auth)
  // Caller can decide whether to enforce that or not.
  Serial.print("[AUTH] Loaded web auth user: ");
  Serial.println(userOut.length() ? userOut : String("<disabled>"));
}

void saveWebAuthConfig(const String &user, const String &pass) {
  if (!prefs.begin("gh_auth", false)) {
    Serial.println("[AUTH] Preferences begin failed (write)");
    return;
  }

  prefs.putString("user", user);
  prefs.putString("pass", pass);
  prefs.end();

  Serial.print("[AUTH] Saved web auth user: ");
  Serial.println(user.length() ? user : String("<disabled>"));
}

// ================= Config load/save =================

void loadConfig() {
  // Defaults
  gConfig.env.fanOnTemp        = 28.0f;
  gConfig.env.fanOffTemp       = 26.0f;
  gConfig.env.fanHumOn         = 80;      // 80 %RH ON
  gConfig.env.fanHumOff        = 70;      // 70 %RH OFF
  gConfig.env.pumpMinOffSec    = 5 * 60;  // 5 minutes
  gConfig.env.pumpMaxOnSec     = 30;      // 30 seconds

  gConfig.light1.onMinutes  = 8 * 60;
  gConfig.light1.offMinutes = 20 * 60;
  gConfig.light1.enabled    = false;

  gConfig.light2.onMinutes  = 8 * 60;
  gConfig.light2.offMinutes = 20 * 60;
  gConfig.light2.enabled    = false;

  gConfig.autoFan  = true;
  gConfig.autoPump = true;

  gConfig.tzIndex  = 0;

  gConfig.charts.tempMinC = 10.0f;
  gConfig.charts.tempMaxC = 40.0f;
  gConfig.charts.humMinPct = 0;
  gConfig.charts.humMaxPct = 100;

  gConfig.chamber1.name              = DEFAULT_CHAMBER1_NAME;
  gConfig.chamber1.soilDryThreshold  = DEFAULT_SOIL_DRY;
  gConfig.chamber1.soilWetThreshold  = DEFAULT_SOIL_WET;
  gConfig.chamber1.profileId         = -1;
  gConfig.chamber2.name              = DEFAULT_CHAMBER2_NAME;
  gConfig.chamber2.soilDryThreshold  = DEFAULT_SOIL_DRY;
  gConfig.chamber2.soilWetThreshold  = DEFAULT_SOIL_WET;
  gConfig.chamber2.profileId         = -1;

  if (!prefs.begin("gh_cfg", true)) {
    Serial.println("[CFG] Preferences begin failed; using defaults");
    return;
  }

  bool migratedLegacySoil = false;

  gConfig.env.fanOnTemp        = prefs.getFloat("fanOn",    gConfig.env.fanOnTemp);
  gConfig.env.fanOffTemp       = prefs.getFloat("fanOff",   gConfig.env.fanOffTemp);
  gConfig.env.fanHumOn         = prefs.getInt  ("fanHumOn", gConfig.env.fanHumOn);
  gConfig.env.fanHumOff        = prefs.getInt  ("fanHumOff",gConfig.env.fanHumOff);
  gConfig.env.pumpMinOffSec    = prefs.getULong("pumpOff",  gConfig.env.pumpMinOffSec);
  gConfig.env.pumpMaxOnSec     = prefs.getULong("pumpOn",   gConfig.env.pumpMaxOnSec);

  int legacySoilDry = prefs.getInt("soilDry", DEFAULT_SOIL_DRY);
  int legacySoilWet = prefs.getInt("soilWet", DEFAULT_SOIL_WET);

  gConfig.light1.onMinutes  = prefs.getInt("l1OnMin",  gConfig.light1.onMinutes);
  gConfig.light1.offMinutes = prefs.getInt("l1OffMin", gConfig.light1.offMinutes);
  gConfig.light1.enabled    = prefs.getBool("l1Auto",  gConfig.light1.enabled);

  gConfig.light2.onMinutes  = prefs.getInt("l2OnMin",  gConfig.light2.onMinutes);
  gConfig.light2.offMinutes = prefs.getInt("l2OffMin", gConfig.light2.offMinutes);
  gConfig.light2.enabled    = prefs.getBool("l2Auto",  gConfig.light2.enabled);

  gConfig.autoFan  = prefs.getBool("autoFan",  gConfig.autoFan);
  gConfig.autoPump = prefs.getBool("autoPump", gConfig.autoPump);

  gConfig.tzIndex  = prefs.getInt("tzIdx",   gConfig.tzIndex);

  gConfig.charts.tempMinC = prefs.getFloat("chartTMin", gConfig.charts.tempMinC);
  gConfig.charts.tempMaxC = prefs.getFloat("chartTMax", gConfig.charts.tempMaxC);
  gConfig.charts.humMinPct = prefs.getInt("chartHMin", gConfig.charts.humMinPct);
  gConfig.charts.humMaxPct = prefs.getInt("chartHMax", gConfig.charts.humMaxPct);

  bool hasC1Name = prefs.isKey("c1Name");
  bool hasC2Name = prefs.isKey("c2Name");
  bool hasC1Dry  = prefs.isKey("c1Dry");
  bool hasC2Dry  = prefs.isKey("c2Dry");
  bool hasC1Wet  = prefs.isKey("c1Wet");
  bool hasC2Wet  = prefs.isKey("c2Wet");
  bool hasC1Prof = prefs.isKey("c1Prof");
  bool hasC2Prof = prefs.isKey("c2Prof");

  gConfig.chamber1.name             = prefs.getString("c1Name", gConfig.chamber1.name);
  gConfig.chamber1.soilDryThreshold = prefs.getInt("c1Dry", gConfig.chamber1.soilDryThreshold);
  gConfig.chamber1.soilWetThreshold = prefs.getInt("c1Wet", gConfig.chamber1.soilWetThreshold);
  gConfig.chamber1.profileId        = prefs.getInt("c1Prof", gConfig.chamber1.profileId);

  gConfig.chamber2.name             = prefs.getString("c2Name", gConfig.chamber2.name);
  gConfig.chamber2.soilDryThreshold = prefs.getInt("c2Dry", gConfig.chamber2.soilDryThreshold);
  gConfig.chamber2.soilWetThreshold = prefs.getInt("c2Wet", gConfig.chamber2.soilWetThreshold);
  gConfig.chamber2.profileId        = prefs.getInt("c2Prof", gConfig.chamber2.profileId);

  bool hasNewChamberKeys = hasC1Name || hasC2Name || hasC1Dry || hasC2Dry || hasC1Wet || hasC2Wet || hasC1Prof || hasC2Prof;

  prefs.end();

  if (!hasNewChamberKeys) {
    gConfig.chamber1.name             = DEFAULT_CHAMBER1_NAME;
    gConfig.chamber2.name             = DEFAULT_CHAMBER2_NAME;
    gConfig.chamber1.soilDryThreshold = legacySoilDry;
    gConfig.chamber1.soilWetThreshold = legacySoilWet;
    gConfig.chamber2.soilDryThreshold = legacySoilDry;
    gConfig.chamber2.soilWetThreshold = legacySoilWet;
    gConfig.chamber1.profileId        = -1;
    gConfig.chamber2.profileId        = -1;
    migratedLegacySoil = true;
  }

  // Basic sanity checks
  if (gConfig.env.fanOffTemp >= gConfig.env.fanOnTemp) {
    gConfig.env.fanOnTemp  = 28.0f;
    gConfig.env.fanOffTemp = 26.0f;
  }

  gConfig.env.fanHumOn  = constrain(gConfig.env.fanHumOn,  0, 100);
  gConfig.env.fanHumOff = constrain(gConfig.env.fanHumOff, 0, 100);
  if (gConfig.env.fanHumOff >= gConfig.env.fanHumOn) {
    gConfig.env.fanHumOn  = 80;
    gConfig.env.fanHumOff = 70;
  }
  if (gConfig.env.pumpMinOffSec < 10) gConfig.env.pumpMinOffSec = 5 * 60;
  if (gConfig.env.pumpMaxOnSec  < 5)  gConfig.env.pumpMaxOnSec  = 30;

  bool chamberValidated = false;
  chamberValidated |= normalizeChamberConfig(gConfig.chamber1, DEFAULT_CHAMBER1_NAME);
  chamberValidated |= normalizeChamberConfig(gConfig.chamber2, DEFAULT_CHAMBER2_NAME);

  gConfig.light1.onMinutes  = constrain(gConfig.light1.onMinutes,  0, 24 * 60 - 1);
  gConfig.light1.offMinutes = constrain(gConfig.light1.offMinutes, 0, 24 * 60 - 1);
  gConfig.light2.onMinutes  = constrain(gConfig.light2.onMinutes,  0, 24 * 60 - 1);
  gConfig.light2.offMinutes = constrain(gConfig.light2.offMinutes, 0, 24 * 60 - 1);

  if (gConfig.tzIndex < 0 || (size_t)gConfig.tzIndex >= TZ_COUNT) {
    gConfig.tzIndex = 0;
  }

  auto clampFloat = [](float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
  };

  gConfig.charts.tempMinC = clampFloat(gConfig.charts.tempMinC, -40.0f, 120.0f);
  gConfig.charts.tempMaxC = clampFloat(gConfig.charts.tempMaxC, -40.0f, 120.0f);
  if (gConfig.charts.tempMaxC <= gConfig.charts.tempMinC) {
    gConfig.charts.tempMinC = 10.0f;
    gConfig.charts.tempMaxC = 40.0f;
  }

  gConfig.charts.humMinPct = constrain(gConfig.charts.humMinPct, 0, 100);
  gConfig.charts.humMaxPct = constrain(gConfig.charts.humMaxPct, 0, 100);
  if (gConfig.charts.humMaxPct <= gConfig.charts.humMinPct) {
    gConfig.charts.humMinPct = 0;
    gConfig.charts.humMaxPct = 100;
  }

  if (migratedLegacySoil || chamberValidated) {
    saveConfig();
  }
}

void saveConfig() {
  if (!prefs.begin("gh_cfg", false)) {
    Serial.println("[CFG] Preferences begin failed (write)");
    return;
  }

  prefs.putFloat("fanOn",    gConfig.env.fanOnTemp);
  prefs.putFloat("fanOff",   gConfig.env.fanOffTemp);
  prefs.putInt  ("fanHumOn", gConfig.env.fanHumOn);
  prefs.putInt  ("fanHumOff",gConfig.env.fanHumOff);
  prefs.putULong("pumpOff",  gConfig.env.pumpMinOffSec);
  prefs.putULong("pumpOn",   gConfig.env.pumpMaxOnSec);

  prefs.putString("c1Name", gConfig.chamber1.name);
  prefs.putInt   ("c1Dry",  gConfig.chamber1.soilDryThreshold);
  prefs.putInt   ("c1Wet",  gConfig.chamber1.soilWetThreshold);
  prefs.putInt   ("c1Prof", gConfig.chamber1.profileId);

  prefs.putString("c2Name", gConfig.chamber2.name);
  prefs.putInt   ("c2Dry",  gConfig.chamber2.soilDryThreshold);
  prefs.putInt   ("c2Wet",  gConfig.chamber2.soilWetThreshold);
  prefs.putInt   ("c2Prof", gConfig.chamber2.profileId);

  prefs.putInt ("l1OnMin", gConfig.light1.onMinutes);
  prefs.putInt ("l1OffMin",gConfig.light1.offMinutes);
  prefs.putBool("l1Auto",  gConfig.light1.enabled);

  prefs.putInt ("l2OnMin", gConfig.light2.onMinutes);
  prefs.putInt ("l2OffMin",gConfig.light2.offMinutes);
  prefs.putBool("l2Auto",  gConfig.light2.enabled);

  prefs.putBool("autoFan",  gConfig.autoFan);
  prefs.putBool("autoPump", gConfig.autoPump);

  prefs.putInt("tzIdx", gConfig.tzIndex);

  prefs.putFloat("chartTMin", gConfig.charts.tempMinC);
  prefs.putFloat("chartTMax", gConfig.charts.tempMaxC);
  prefs.putInt  ("chartHMin", gConfig.charts.humMinPct);
  prefs.putInt  ("chartHMax", gConfig.charts.humMaxPct);

  prefs.end();
}

struct GrowProfilePreset {
  const char* label;
  EnvConfig   env;
  struct ChamberProfilePreset {
    int soilDry;
    int soilWet;
    int lightOnMinutes;
    int lightOffMinutes;
    bool lightAuto;
  } chambers[2];
  bool setAutoFan;
  bool setAutoPump;
  bool autoFan;
  bool autoPump;
};

static const GrowProfilePreset kGrowProfiles[] = {
  { "Custom",
    { 0, 0, 0, 0, 0, 0 },
    {
      { DEFAULT_SOIL_DRY, DEFAULT_SOIL_WET, 8*60, 20*60, true },
      { DEFAULT_SOIL_DRY, DEFAULT_SOIL_WET, 8*60, 20*60, true },
    },
    false, false, false, false
  },
  { "Seedling",
    { 27.0f, 25.0f, 78, 68, 240, 20 },
    {
      { 40, 55, 6*60, 24*60-1, true },
      { 40, 55, 6*60, 24*60-1, true },
    },
    true, true, true, true
  },
  { "Vegetative",
    { 28.0f, 26.0f, 75, 65, 300, 25 },
    {
      { 38, 52, 6*60, 24*60-1, true },
      { 38, 52, 6*60, 24*60-1, true },
    },
    true, true, true, true
  },
  { "Flowering",
    { 27.0f, 25.0f, 72, 62, 420, 20 },
    {
      { 35, 50, 8*60, 20*60, true },
      { 35, 50, 8*60, 20*60, true },
    },
    true, true, true, true
  },
};

static GrowProfileInfo profileInfoFromPreset(const GrowProfilePreset &p){
  GrowProfileInfo info;
  info.label = p.label;
  info.env   = p.env;
  info.light1 = { p.chambers[0].lightOnMinutes, p.chambers[0].lightOffMinutes, p.chambers[0].lightAuto };
  info.light2 = { p.chambers[1].lightOnMinutes, p.chambers[1].lightOffMinutes, p.chambers[1].lightAuto };
  info.autoFan = p.autoFan;
  info.autoPump = p.autoPump;
  info.setsAutoFan = p.setAutoFan;
  info.setsAutoPump = p.setAutoPump;
  info.chamber1 = { String(DEFAULT_CHAMBER1_NAME), p.chambers[0].soilDry, p.chambers[0].soilWet, -1 };
  info.chamber2 = { String(DEFAULT_CHAMBER2_NAME), p.chambers[1].soilDry, p.chambers[1].soilWet, -1 };
  return info;
}

bool applyGrowProfileToChamber(int chamberIdx, int profileId, String &appliedName) {
  if (profileId < 0 || (size_t)profileId >= (sizeof(kGrowProfiles)/sizeof(kGrowProfiles[0]))) {
    return false;
  }
  if (chamberIdx < 0 || chamberIdx > 1) {
    return false;
  }

  const GrowProfilePreset &p = kGrowProfiles[profileId];
  appliedName = p.label;
  if (profileId == 0) {
    return true; // Custom: no changes
  }

  ChamberConfig* chamber = (chamberIdx == 0) ? &gConfig.chamber1 : &gConfig.chamber2;
  LightSchedule* light   = (chamberIdx == 0) ? &gConfig.light1   : &gConfig.light2;
  const GrowProfilePreset::ChamberProfilePreset &chPreset = p.chambers[chamberIdx];

  chamber->soilDryThreshold = chPreset.soilDry;
  chamber->soilWetThreshold = chPreset.soilWet;
  chamber->profileId = (profileId > 0) ? profileId : -1;
  normalizeChamberConfig(*chamber, chamberIdx == 0 ? DEFAULT_CHAMBER1_NAME : DEFAULT_CHAMBER2_NAME);

  light->onMinutes  = chPreset.lightOnMinutes;
  light->offMinutes = chPreset.lightOffMinutes;
  light->enabled    = chPreset.lightAuto;

  if (profileId > 0) {
    if (p.setAutoFan)  gConfig.autoFan  = p.autoFan;
    if (p.setAutoPump) gConfig.autoPump = p.autoPump;
  }

  return true;
}

bool applyGrowProfile(int profileId, String &appliedName) {
  if (profileId < 0 || (size_t)profileId >= (sizeof(kGrowProfiles)/sizeof(kGrowProfiles[0]))) {
    return false;
  }

  const GrowProfilePreset &p = kGrowProfiles[profileId];
  if (profileId == 0) {
    appliedName = p.label;
    return true; // Custom: no changes
  }

  gConfig.env = p.env;

  bool c1 = applyGrowProfileToChamber(0, profileId, appliedName);
  bool c2 = applyGrowProfileToChamber(1, profileId, appliedName);
  if (!c1 || !c2) return false;

  if (p.setAutoFan)  gConfig.autoFan  = p.autoFan;
  if (p.setAutoPump) gConfig.autoPump = p.autoPump;

  appliedName = p.label;
  return true;
}

size_t growProfileCount(){
  return sizeof(kGrowProfiles) / sizeof(kGrowProfiles[0]);
}

const GrowProfileInfo* growProfileInfoAt(size_t idx){
  static GrowProfileInfo infos[sizeof(kGrowProfiles) / sizeof(kGrowProfiles[0])];
  static bool initialized = false;
  if (!initialized){
    for (size_t i = 0; i < sizeof(kGrowProfiles) / sizeof(kGrowProfiles[0]); i++) {
      infos[i] = profileInfoFromPreset(kGrowProfiles[i]);
    }
    initialized = true;
  }
  if (idx >= (sizeof(kGrowProfiles) / sizeof(kGrowProfiles[0]))) return nullptr;
  return &infos[idx];
}

void applyTimezoneFromConfig() {
  const TzOption &tz = currentTzOption();
  setenv("TZ", tz.tz, 1);
  tzset();
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

  // Fan (auto by temperature OR humidity)
  if (gConfig.autoFan) {
    bool haveTemp = !isnan(gSensors.temperatureC);
    bool haveHum  = !isnan(gSensors.humidityRH);

    bool hot   = false;
    bool cool  = false;
    bool humid = false;
    bool dry   = false;

    if (haveTemp) {
      hot  = (gSensors.temperatureC >= gConfig.env.fanOnTemp);
      cool = (gSensors.temperatureC <= gConfig.env.fanOffTemp);
    }
    if (haveHum) {
      humid = ((int)gSensors.humidityRH >= gConfig.env.fanHumOn);
      dry   = ((int)gSensors.humidityRH <= gConfig.env.fanHumOff);
    }

    bool fanHotOrHumid = (haveTemp && hot) || (haveHum && humid);

    if (!gRelays.fan) {
      // Turn fan ON if temperature OR humidity exceed ON thresholds
      if (fanHotOrHumid) {
        if (fanTriggerStartMs == 0) {
          fanTriggerStartMs = nowMs;
        }
        if (nowMs - fanTriggerStartMs >= FAN_TRIGGER_HOLD_MS) {
          gRelays.fan = true;
        }
      } else {
        fanTriggerStartMs = 0;
      }
    } else {
      // Turn fan OFF when BOTH are back in safe range (or missing)
      bool tempOk = !haveTemp || cool;
      bool humOk  = !haveHum  || dry;

      if (tempOk && humOk) {
        gRelays.fan = false;
        fanTriggerStartMs = 0;
      }
    }
  } else {
    fanTriggerStartMs = 0;
  }

  // Pump (auto by soil moisture + timing)
  if (gConfig.autoPump) {
    bool chamber1Dry = gSensors.soil1Percent < gConfig.chamber1.soilDryThreshold;
    bool chamber2Dry = gSensors.soil2Percent < gConfig.chamber2.soilDryThreshold;
    bool chamber1Wet = gSensors.soil1Percent > gConfig.chamber1.soilWetThreshold;
    bool chamber2Wet = gSensors.soil2Percent > gConfig.chamber2.soilWetThreshold;

    if (!pumpRunning) {
      bool tooDry    = chamber1Dry || chamber2Dry;
      bool minOffMet = (nowMs - lastPumpStopMs) > (gConfig.env.pumpMinOffSec * 1000UL);

      if (tooDry) {
        if (pumpDryStartMs == 0) {
          pumpDryStartMs = nowMs;
        }
      } else {
        pumpDryStartMs = 0;
      }

      bool holdMet = pumpDryStartMs && (nowMs - pumpDryStartMs >= PUMP_TRIGGER_HOLD_MS);

      if (tooDry && minOffMet && holdMet) {
        pumpRunning      = true;
        pumpStartMs      = nowMs;
        pumpActiveDryMask = 0;
        if (chamber1Dry) pumpActiveDryMask |= 0x01;
        if (chamber2Dry) pumpActiveDryMask |= 0x02;
        gRelays.pump     = true;
      }
    } else {
      bool chamber1Satisfied = !(pumpActiveDryMask & 0x01) || chamber1Wet;
      bool chamber2Satisfied = !(pumpActiveDryMask & 0x02) || chamber2Wet;
      bool maxOnElapsed      = (nowMs - pumpStartMs) > (gConfig.env.pumpMaxOnSec * 1000UL);

      if ((chamber1Satisfied && chamber2Satisfied) || maxOnElapsed) {
        pumpRunning       = false;
        gRelays.pump      = false;
        lastPumpStopMs    = nowMs;
        pumpActiveDryMask = 0;
        pumpDryStartMs    = 0;
      }
    }
  } else {
    pumpDryStartMs = 0;
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
  s.soil1  = gSensors.soil1Percent;
  s.soil2  = gSensors.soil2Percent;
  s.light1 = gRelays.light1;
  s.light2 = gRelays.light2;

  gHistoryIndex++;
  if (gHistoryIndex >= HISTORY_SIZE) {
    gHistoryIndex = 0;
    gHistoryFull  = true;
  }
}

// ================= Hardware init (with AP fallback) =================

static void showStaIpOnDisplay() {
  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("IP:");
  u8g2.setCursor(0, 20);
  u8g2.print(WiFi.localIP().toString().c_str());
  u8g2.sendBuffer();
  delay(2000);
}

static void showApOnDisplay(const char* apSsid, const IPAddress& apIP) {
  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("AP:");
  u8g2.setCursor(0, 20);
  u8g2.print(apSsid);
  u8g2.setCursor(0, 30);
  u8g2.print(apIP.toString().c_str());
  u8g2.sendBuffer();
  delay(2000);
}

static void startApFallback() {
  if (sApStarted) return;

  const char* apSsid = "EZgrow-Setup";
  const char* apPass = ""; // open AP; set a password if you prefer

  WiFi.mode(WIFI_AP_STA);

  bool apRes;
  if (strlen(apPass) > 0) {
    apRes = WiFi.softAP(apSsid, apPass);
  } else {
    apRes = WiFi.softAP(apSsid);
  }

  if (apRes) {
    sApStarted = true;
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("[WiFi] AP started: ");
    Serial.print(apSsid);
    Serial.print(" IP=");
    Serial.println(apIP);
    showApOnDisplay(apSsid, apIP);
  } else {
    Serial.println("[WiFi] AP start failed");
    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print("WiFi/AP failed");
    u8g2.sendBuffer();
    delay(2000);
  }
}

static void startStaConnect(const char* reason = nullptr) {
  if (sWifiSsid.isEmpty()) {
    Serial.println("[WiFi] No SSID configured");
    return;
  }
  if (sStaAttemptInProgress) return;

  Serial.print("[WiFi] Connecting to ");
  Serial.println(sWifiSsid);
  if (reason && strlen(reason) > 0) {
    Serial.print("[WiFi] Reason: ");
    Serial.println(reason);
  }
  WiFi.begin(sWifiSsid.c_str(), sWifiPass.c_str());
  sStaAttemptInProgress = true;
  sStaAttemptStartMs    = millis();
  sLastStaAttemptMs     = sStaAttemptStartMs;
}

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
  loadWifiCredentials(ssid, pass, true);
  sWifiSsid = ssid;
  sWifiPass = pass;

  // Wi-Fi AP+STA mode for AP fallback
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);

  bool staConnected = false;

  if (!ssid.isEmpty()) {
    startStaConnect("boot");

    Serial.print("[WiFi] Connecting");
    while (sStaAttemptInProgress && (millis() - sStaAttemptStartMs) < WIFI_CONNECT_TIMEOUT_MS) {
      if (WiFi.status() == WL_CONNECTED) {
        break;
      }
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      staConnected        = true;
      sStaAttemptInProgress = false;
      sLastWifiStatus     = WL_CONNECTED;
      sDisconnectedSinceMs = 0;
      Serial.print("[WiFi] Connected, IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("[WiFi] STA connect failed");
      sStaAttemptInProgress = false;
      sLastWifiStatus = WiFi.status();
    }
  } else {
    Serial.println("[WiFi] No SSID configured");
    sLastWifiStatus = WL_DISCONNECTED;
  }

  if (!staConnected) {
    startApFallback();
  } else {
    showStaIpOnDisplay();
  }

  // NTP / time zone
  applyTimezoneFromConfig();
  configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
}

void updateWifi() {
  unsigned long now = millis();
  wl_status_t status = WiFi.status();
  bool wasConnected = (sLastWifiStatus == WL_CONNECTED);
  bool isConnected  = (status == WL_CONNECTED);

  if (isConnected && !wasConnected) {
    Serial.print("[WiFi] Connected, IP: ");
    Serial.println(WiFi.localIP());
    sDisconnectedSinceMs = 0;
    sStaAttemptInProgress = false;
    sLastStaAttemptMs = now;

    if (sApStarted) {
      WiFi.softAPdisconnect(false);
      WiFi.mode(WIFI_STA);
      sApStarted = false;
    }
  }

  if (isConnected) {
    sLastWifiStatus = status;
    return;
  }

  if (wasConnected && sDisconnectedSinceMs == 0) {
    sDisconnectedSinceMs = now;
    Serial.println("[WiFi] Disconnected, retrying soon");
  } else if (sDisconnectedSinceMs == 0) {
    sDisconnectedSinceMs = now;
  }

  sLastWifiStatus = status;

  if (sStaAttemptInProgress) {
    if ((now - sStaAttemptStartMs) >= WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("[WiFi] STA connect timeout; will retry after backoff");
      WiFi.disconnect(false, false);
      sStaAttemptInProgress = false;
      sLastStaAttemptMs     = now;
    }
    return;
  }

  if (!sWifiSsid.length()) return;

  if (!sApStarted && sDisconnectedSinceMs && (now - sDisconnectedSinceMs) >= WIFI_AP_RESTART_DELAY_MS) {
    startApFallback();
  }

  if ((now - sLastStaAttemptMs) < WIFI_RETRY_INTERVAL_MS) return;

  startStaConnect("retry");
}
