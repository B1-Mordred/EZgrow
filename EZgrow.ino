#include <Arduino.h>
#include "Greenhouse.h"
#include "WebUI.h"
#include "HistoryStorage.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  // Hardware + config + Wi-Fi + LittleFS init
  initHardware();

  // Load persisted 7-day history (if available on LittleFS)
  initHistoryStorage();

  // Start HTTP server, Web UI, APIs (including /api/history)
  initWebServer();
}

void loop() {
  handleWebServer();
  updateWifi();
  updateTime();
  updateSensors();
  updateControlLogic();
  updateDisplay();

  // Keeps the in-RAM 7-day ring buffer filled
  logHistorySample();

  // Periodically flush history ring buffer to LittleFS (for reboot persistence)
  historyStorageLoop();
}

