# ESP32 Greenhouse Controller (ESP32-4R-A2, Multi-file, LittleFS, Wi-Fi Config + AP Fallback)

This project implements a greenhouse controller based on the **ESP32-4R-A2** relay board.

It controls:

- 2 lights  
- 1 fan (12 V)  
- 1 pump (12 V)

It reads:

- Temperature + humidity from an **SHT40** (I²C)  
- Soil moisture from **2× HD38** analog sensors  

It displays status on a small **0.91" WE-DA-361 I²C OLED** and exposes a web UI (via Wi-Fi) with:

- Dashboard (live sensors, relay states, modes)  
- Configuration (thresholds, timings, light schedules)  
- History charts (temperature, humidity, light states)  
- **Wi-Fi configuration** (scan SSIDs, select, store SSID/password in NVS)  

All charts work **offline**, using **LittleFS** to serve Chart.js from the ESP32.

The device supports:

- **Station (STA) mode**: connects to your existing Wi-Fi network.  
- **Access Point (AP) fallback**: starts `EZgrow-Setup` AP if STA connection fails or no SSID is configured.

---

## Quick Start

1. **Create the project folder**

   ```text
   controller/
     controller.ino
     Greenhouse.h
     Greenhouse.cpp
     WebUI.h
     WebUI.cpp

     data/
       chart.umd.min.js
   ```

2. **Install Arduino support**

   - Install the **ESP32** board package via the Boards Manager (Espressif Systems).
   - In the Arduino IDE, select:
     - Board: `ESP32 Dev Module` (or the closest ESP32-4R-A2 equivalent)
     - Correct COM port.

3. **Install required libraries (Library Manager)**

   - `Adafruit SHT4X`
   - `Adafruit Unified Sensor`
   - `U8g2`

4. **Download Chart.js and place it in `data/`**

   - Download a recent **Chart.js 4 UMD build** (e.g. from JSDelivr).
   - Save it as:

     ```text
     controller/data/chart.umd.min.js
     ```

5. **Optional: Set compile-time default Wi-Fi (bootstrap only)**

   In `Greenhouse.cpp`, you can set initial default credentials:

   ```cpp
   static const char* DEFAULT_WIFI_SSID = "YOUR_SSID";
   static const char* DEFAULT_WIFI_PASS = "YOUR_PASSWORD";
   ```

   These are **only used if no Wi-Fi credentials are stored in NVS**.  
   Once you save SSID/password via the `/wifi` page, those NVS values override defaults.

6. **Upload LittleFS data**

   - Install the **ESP32 LittleFS Data Upload** plugin for Arduino IDE (if not already installed).
   - In Arduino IDE, open the `controller` sketch.
   - Use the **“ESP32 LittleFS Data Upload”** menu to upload the `data/` folder to the ESP32.

7. **Compile and upload the firmware**

   - Click **Verify** to compile.
   - Click **Upload** to flash the ESP32-4R-A2.

8. **First-time Wi-Fi setup**

   After boot, one of two things will happen:

   ### 8.1 If valid Wi-Fi credentials are available

   - The ESP32 connects to your network.
   - The IP address is:
     - Printed to the Serial Monitor (115200 baud).
     - Shown briefly on the OLED.

   **Open the dashboard**:

   ```text
   http://<esp32-ip-address>/
   ```

   - `/` : dashboard and charts
   - `/config` : control thresholds, timings, schedules
   - `/wifi` : Wi-Fi configuration (scan, select SSID, save)

   ### 8.2 If connection fails or no SSID configured

   - The ESP32 starts an **access point (AP)**:

     - SSID: `EZgrow-Setup`  
     - Password: _empty_ (open AP, set a password in the code if desired)

   - Connect your phone or laptop to `EZgrow-Setup`.
   - Open:

     ```text
     http://192.168.4.1/wifi
     ```

   - On the **Wi-Fi page**:
     - Click a network in the table to populate the SSID.
     - Enter your Wi-Fi password.
     - Save.
   - The device will **reboot** and attempt to connect to the selected Wi-Fi network.

---

## 1. Features Overview

### Hardware

- **Controller board**: ESP32-4R-A2 (4-relay ESP32 module)
- **Actuators** (via onboard relays + external 12 V supply):
  - Light 1 (Relay 1)
  - Light 2 (Relay 2)
  - 12 V fan (Relay 3)
  - 12 V pump (Relay 4)
- **Sensors**:
  - 1× SHT40 (I²C) – temperature and humidity
  - 2× HD38 soil moisture sensors (analog output, powered at 3.3 V)
- **Display**:
  - WE-DA-361 – 0.91" 128×32 I²C OLED (SSD1306 compatible)

### Control Logic

- **Fan control** (automatic):
  - Temperature-based control with configurable ON/OFF thresholds (hysteresis).
- **Pump control** (automatic):
  - Soil moisture-based control using 2 sensors and configurable:
    - Dry/wet thresholds.
    - Minimum OFF time.
    - Maximum ON time.
- **Lights**:
  - Each light can run:
    - In **AUTO** mode using daily schedules (ON/OFF times).
    - In **MANUAL** mode with direct toggling via web UI.
  - Schedules allow intervals that cross midnight (e.g. 20:00–06:00).

### Network Modes

- **STA (station) mode**:
  - Connects to an existing Wi-Fi network using SSID/password stored in **NVS**.
  - Shows assigned IP on Serial and OLED.

- **AP fallback**:
  - If STA connection fails, or no SSID is configured:
    - Starts AP `EZgrow-Setup` (open by default).
    - AP IP is typically `192.168.4.1`.
    - Wi-Fi setup is done via `/wifi`.

### Web Interface

- **Dashboard (`/`)**:
  - Current time (from NTP).
  - Temperature (°C), humidity (%).
  - Soil moisture 1/2 (%).
  - States and modes of Light 1, Light 2, Fan, Pump (ON/OFF + AUTO/MAN).
  - Light schedule summary.
  - History charts:
    - Temperature and humidity (line chart).
    - Light 1 and Light 2 states (step chart).
  - Link to `/wifi` for network configuration.

- **Configuration (`/config`)**:
  - Fan ON temperature (°C).
  - Fan OFF temperature (°C).
  - Soil DRY/WET thresholds (%).
  - Pump minimum OFF time (s).
  - Pump maximum ON time (s).
  - Light 1/2 schedules (ON/OFF time + “use schedule” flags).
  - AUTO/MANUAL for fan and pump.

- **Wi-Fi configuration (`/wifi`)**:
  - Lists available SSIDs (scanned with `WiFi.scanNetworks()`).
  - Shows current connection (if any).
  - Form:
    - SSID (clicking a row in the table fills this).
    - Password.
  - On submit:
    - Credentials are saved to NVS.
    - Device reboots and attempts connection with new credentials.

- **History API (`/api/history`)**:
  - JSON feed of the last 24 hours (1 point per minute) with:
    - Timestamp.
    - Temperature.
    - Humidity.
    - Light 1/2 states.

- **Static asset from LittleFS**:
  - `/chart.umd.min.js` – Chart.js UMD bundle served from LittleFS for offline charts.

### Data Logging

- In-memory **ring buffer** for history:
  - **1440 samples** (1 day at 1-minute interval).
  - Each sample contains time, temperature, humidity, Light 1, Light 2.
- Periodic logging every 1 minute.

### Configuration Persistence

- All settings stored in **NVS** via `Preferences`:
  - Fan thresholds.
  - Soil thresholds.
  - Pump timings.
  - Light 1/2 schedules and enabled flags.
  - Fan and pump AUTO/MAN flags.
  - Wi-Fi SSID and password.
- Settings are reloaded at boot; no recompile needed to adjust behaviour.

---

## 2. Project Structure

```text
controller/
  controller.ino        # Main entry point (setup/loop)
  Greenhouse.h          # Config/state structs, function declarations
  Greenhouse.cpp        # Hardware init, sensors, control logic, Wi-Fi/AP, history
  WebUI.h               # Web server API declarations
  WebUI.cpp             # HTTP routes, HTML, config UI, Wi-Fi UI, charts

  data/
    chart.umd.min.js    # Chart.js UMD bundle (served via LittleFS)
```

> The `data/` directory is uploaded to the ESP32’s LittleFS partition using the **ESP32 LittleFS Data Upload** tool.

---

## 3. Hardware Wiring

### 3.1 Relays → Loads

The ESP32-4R-A2 board maps its relays to the following GPIOs:

| Function | Relay | ESP32 GPIO |
|----------|--------|-----------|
| Light 1  | RLY1   | 25        |
| Light 2  | RLY2   | 26        |
| Fan      | RLY3   | 32        |
| Pump     | RLY4   | 33        |

Typical wiring:

- 12 V+ → relay **COM**
- Relay **NO** → load + (light/fan/pump +)
- Load − → 12 V GND
- 12 V GND → ESP32 GND (common ground)

Relays are configured as **active LOW** in the code:

- `LOW` = relay ON (energized).
- `HIGH` = relay OFF.

If your board uses active HIGH relays, you can invert `RELAY_ACTIVE_LEVEL` in `Greenhouse.cpp`.

### 3.2 I²C Bus: SHT40 + WE-DA-361 OLED

The SHT40 and OLED share the ESP32 I²C bus:

| Signal | ESP32 pin | Connected devices          |
|--------|-----------|----------------------------|
| SDA    | 21        | SHT40 SDA, WE-DA-361 SDA   |
| SCL    | 22        | SHT40 SCL, WE-DA-361 SCL   |

Power:

- **SHT40:**
  - VCC → 3.3 V
  - GND → GND
- **WE-DA-361 OLED:**
  - VCC → 3.3 V (module supports 3.3–5 V)
  - GND → GND

If the modules **do not** have pull-up resistors on SDA/SCL:

- Add ~4.7 kΩ from SDA → 3.3 V.
- Add ~4.7 kΩ from SCL → 3.3 V.

### 3.3 Soil Moisture Sensors (HD38)

Use ADC1 pins (Wi-Fi-safe ADC):

| Sensor         | ESP32 pin | Notes                 |
|----------------|-----------|-----------------------|
| Soil sensor 1  | 34        | ADC1_CH6 (input only) |
| Soil sensor 2  | 35        | ADC1_CH7 (input only) |

Power for each HD38:

- VCC → 3.3 V  
- GND → GND  
- AO (analog out) → respective ADC pin (34 or 35)

> Use 3.3 V for HD38 to keep the analog output within ESP32 ADC limits.

---

## 4. Web UI Endpoints

### 4.1 Dashboard (`/`)

Main interface:

- Time (if NTP synced, otherwise “syncing...”).
- Temperature, humidity.
- Soil 1/2 moisture.
- Relay states and modes:
  - Light 1, Light 2, Fan, Pump (ON/OFF + AUTO/MAN).
- Light schedules (L1 and L2).
- Buttons:
  - **Toggle** for each device (in MANUAL mode).
  - **Switch to AUTO/MANUAL** for each device.
- History charts:
  - **Temperature & Humidity** (line chart, dual axis).
  - **Light 1 & Light 2** (step-like 0/1 chart).
- Direct links to:
  - `/config` (greenhouse logic)
  - `/wifi` (network config)

### 4.2 Configuration (`/config`)

- Fan ON temperature (°C).
- Fan OFF temperature (°C).
- Soil DRY threshold (%).
- Soil WET threshold (%).
- Pump minimum OFF time (seconds).
- Pump maximum ON time (seconds).
- Light 1 schedule:
  - Use schedule (checkbox).
  - ON time, OFF time (`<input type="time">`).
- Light 2 schedule:
  - Use schedule (checkbox).
  - ON time, OFF time.
- AUTO/MANUAL checkboxes for:
  - Fan.
  - Pump.

On submit:

- Values are validated (basic sanity).
- Configuration is saved to NVS (via `Preferences`).
- New values are applied immediately (no reboot required).

### 4.3 Wi-Fi configuration (`/wifi`)

- **Current connection**:
  - Shows whether the ESP32 is connected and to which SSID.
  - Shows current IP and RSSI.

- **SSID/password form**:
  - SSID (`<input type="text">`).
  - Password (`<input type="password">`).
  - Credentials persist in NVS (`gh_wifi` namespace).

- **Network scan**:
  - Table of nearby SSIDs, RSSI, and encryption type (open / secured).
  - Clicking a row populates the SSID field.

On submit:

- SSID/password are stored in NVS.
- Device responds with a simple “saved” page.
- After a short delay, the device **restarts**.
- On next boot, the device attempts STA connection using the saved credentials.

### 4.4 Control endpoints

- `GET /toggle?id=light1|light2|fan|pump`  
  Toggles the specified relay **only if** that device is in MANUAL mode.

- `GET /mode?id=light1|light2|fan|pump&auto=0|1`  
  Switches the specified device between AUTO and MANUAL:
  - For lights: toggles use of schedule (AUTO) vs manual relay control.
  - For fan/pump: toggles automatic control logic vs manual relay control.

### 4.5 History API (`/api/history`)

- Returns a JSON payload containing an array of historical points for the last 24 hours, one per minute.
- Used by the dashboard’s JavaScript to render charts.

---

## 5. Control Logic Details

### 5.1 Fan (Temperature-based)

If `autoFan` is enabled:

- Fan turns **ON** when:
  - `temperature ≥ fanOnTemp`
- Fan turns **OFF** when:
  - `temperature ≤ fanOffTemp`

Hysteresis (`fanOnTemp` > `fanOffTemp`) prevents rapid cycling.

### 5.2 Pump (Soil + Timing-based)

If `autoPump` is enabled:

- “Too dry” if **either** soil sensor `< soilDryThreshold`.
- “Wet enough” if **both** soil sensors `> soilWetThreshold`.
- Pump turns **ON** when:
  - Not currently running, AND
  - “Too dry”, AND
  - At least `pumpMinOffSec` seconds elapsed since the last stop.
- Pump turns **OFF** when:
  - “Wet enough”, OR
  - Pump has been ON for more than `pumpMaxOnSec` seconds.

### 5.3 Lights (Schedules + Manual)

Each light has:

- `onMinutes` and `offMinutes` (minutes since midnight).
- `enabled` flag:
  - `true`: AUTO (schedule active).
  - `false`: MANUAL.

Schedules support intervals that **cross midnight**:

- Example 1: 08:00–20:00 (day).
- Example 2: 20:00–06:00 (night).

When a light is in MANUAL mode, its relay is controlled solely by the web UI `Toggle` button.

---

## 6. OLED Display Content

The WE-DA-361 0.91" OLED (128×32) shows:

- **Line 1:** `T:xx.xC H:yy%`
- **Line 2:** `S1:aa% S2:bb%`
- **Line 3:** `L1xA/M L2xA/M FxA/M PxA/M`

Where:

- `x` = `1` (ON) or `0` (OFF).
- `A` = AUTO / `M` = MANUAL.

On boot:

- Initially: `"Greenhouse boot..."`.
- Then:
  - If STA connected: IP address of the ESP32.
  - If AP fallback: AP SSID (`EZgrow-Setup`) and AP IP (e.g. `192.168.4.1`).

---

## 7. Calibration Notes

### 7.1 Soil Moisture

Default mapping:

```cpp
soilPercent = map(raw, 0, 4095, 100, 0);
soilPercent = constrain(soilPercent, 0, 100);
```

To calibrate:

1. Record `raw_dry` in dry medium and `raw_wet` in fully wet medium.
2. Use:

```cpp
soilPercent = map(raw, raw_wet, raw_dry, 100, 0);
soilPercent = constrain(soilPercent, 0, 100);
```

Then adjust `soilDryThreshold` and `soilWetThreshold` via `/config`.

### 7.2 Temperature Thresholds

Example configuration:

- `fanOnTemp` = 28 °C  
- `fanOffTemp` = 26 °C  

These can be tuned in the config UI to better fit your greenhouse.

---

## 8. Security Considerations

- The controller uses **HTTP without authentication** by default.
- Recommended precautions:
  - Keep it on a local network, not directly exposed to the internet.
  - For AP mode, you may want to configure a password in `initHardware()`:
    - Set a non-empty password for `WiFi.softAP(apSsid, apPass)`.
  - If needed:
    - Put it behind a firewall or in a separate VLAN.
    - Use a reverse proxy with HTTPS and optional authentication.
    - Extend the code with HTTP Basic Auth for sensitive endpoints.

---

## 9. Possible Extensions

- Add HTTP Basic Auth / token-based auth for `/config`, `/wifi`, `/toggle`, and `/mode`.
- MQTT integration (for Home Assistant, etc.).
- Long-term data logging to SD card or an external database.
- Additional sensors:
  - CO₂
  - Light intensity
  - Pressure, etc.
- BLE / Bluetooth-based control UI.
- Captive portal for Wi-Fi onboarding (redirect to `/wifi` when connected to AP).

---

## 10. License

Choose an appropriate license for your project (for example, MIT):

```text
MIT License

Copyright (c) 2025 Marco Horstmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
