#include <Arduino.h>
#include "Greenhouse.h"
#include "WebUI.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  initHardware();   // pins, LittleFS, config, WiFi + AP fallback, time, sensors, display
  initWebServer();  // HTTP server + routes (incl. offline Chart.js + Wi-Fi config)
}

void loop() {
  handleWebServer();
  updateTime();
  updateSensors();
  updateControlLogic();
  updateDisplay();
  logHistorySample();
}

