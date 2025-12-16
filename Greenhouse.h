#pragma once
#include <Arduino.h>
#include <time.h>

// ========== Configuration structures ==========

struct LightSchedule {
  // minutes since midnight [0..1439]
  int onMinutes;   // schedule ON time
  int offMinutes;  // schedule OFF time
  bool enabled;    // use schedule (AUTO) vs manual
};

struct EnvConfig {
  float         fanOnTemp;        // °C
  float         fanOffTemp;       // °C
  int           fanHumOn;         // %RH - fan ON humidity threshold
  int           fanHumOff;        // %RH - fan OFF humidity threshold
  int           soilDryThreshold; // %
  int           soilWetThreshold; // %
  unsigned long pumpMinOffSec;    // seconds
  unsigned long pumpMaxOnSec;     // seconds
};

struct GreenhouseConfig {
  EnvConfig     env;
  LightSchedule light1;
  LightSchedule light2;
  bool          autoFan;
  bool          autoPump;
  int           tzIndex; // selectable time zone index
};

// ========== Runtime state structures ==========

struct SensorState {
  float temperatureC;
  float humidityRH;
  int   soil1Percent;
  int   soil2Percent;
};

struct RelayState {
  bool light1;
  bool light2;
  bool fan;
  bool pump;
};

// History sample for charts
struct HistorySample {
  time_t timestamp; // unix time (seconds), 0 if unknown
  float  temp;
  float  hum;
  bool   light1;
  bool   light2;
};

// History configuration
constexpr size_t        HISTORY_SIZE        = 1440;          // 1 day @ 1-min interval
constexpr unsigned long HISTORY_INTERVAL_MS = 60UL * 1000UL; // 1 minute

// ========== Global config/state (defined in Greenhouse.cpp) ==========

extern GreenhouseConfig gConfig;
extern SensorState      gSensors;
extern RelayState       gRelays;

extern HistorySample gHistoryBuf[HISTORY_SIZE];
extern size_t        gHistoryIndex;
extern bool          gHistoryFull;

// Time state getter
bool greenhouseGetTime(struct tm &outTime, bool &available);
const char* greenhouseTimezoneLabel();

// ========== Hardware / logic API ==========

// Initialize pins, LittleFS, config, WiFi (with AP fallback), NTP, sensors, display
void initHardware();

// Periodically update time from system clock (uses NTP in background)
void updateTime();

// Read sensors (SHT40 + HD38) into gSensors
void updateSensors();

// Apply automatic control for lights (schedules), fan (temp+humidity), pump (soil)
void updateControlLogic();

// Update WE-DA-361 OLED display
void updateDisplay();

// Add one point to history ring buffer (for charts)
void logHistorySample();

// Load / save configuration (env thresholds, pump timings, light schedules)
void loadConfig();
void saveConfig();
void applyTimezoneFromConfig();

// Apply a grow profile preset by ID (0=Custom/no-op, 1=Seedling, 2=Vegetative, 3=Flowering)
// Returns true if applied and fills appliedName with the profile label.
bool applyGrowProfile(int profileId, String &appliedName);

// Convenience: convert (minutes since midnight) to "HH:MM"
String minutesToTimeStr(int minutes);

// Check if, for a schedule, the light should be ON at nowMinutes
bool scheduleIsOn(int onMin, int offMin, int nowMin);

// ========== Web UI authentication (Basic Auth in NVS) ==========

// Load web UI credentials (username/password) from NVS.
// If nothing is stored, defaults "admin"/"admin" are returned.
void loadWebAuthConfig(String &userOut, String &passOut);

// Save web UI credentials (username/password) to NVS.
// If userOut is empty, authentication will effectively be disabled.
void saveWebAuthConfig(const String &user, const String &pass);

// ========== Wi-Fi credentials storage (NVS) ==========

// Load Wi-Fi credentials from NVS; falls back to compiled defaults if empty.
// Outputs into ssidOut and passOut.
void loadWifiCredentials(String &ssidOut, String &passOut);

// Save Wi-Fi credentials to NVS.
void saveWifiCredentials(const String &ssid, const String &password);
