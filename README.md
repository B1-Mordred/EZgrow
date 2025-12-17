# ESP32 Greenhouse Controller (ESP32-4R-A2, Multi-file, LittleFS, Wi-Fi Config, Auth + AP Captive Portal)

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
- Configuration (thresholds, timings, light schedules, grow profiles, timezone, web-auth config)
- Timezone dropdown includes UTC, Europe/Berlin, Europe/London, US/Eastern, US/Central, US/Mountain, and US/Pacific options.
- History charts (temperature, humidity, light states)
- History chart labels use the device timezone when supported by the browser.
- Live sparklines for sensors on the dashboard
- Grow profile tab with preset previews, chamber-targeted apply (Ch1→Light1, Ch2→Light2), plus system tab showing current device time
- Relay controls that disable while requests are processing, with toast feedback for mode/toggle actions
- **Wi-Fi configuration** (scan SSIDs, select, store SSID/password in NVS)
- **HTTP Basic Authentication** (credentials stored in NVS, configurable in UI)
- **Captive portal** for Wi-Fi onboarding in AP mode (auto-redirects to `/wifi`)

All charts work **offline**, using **LittleFS** to serve Chart.js from the ESP32.

The device supports:

- **Station (STA) mode**: connects to your existing Wi-Fi network.  
- **Access Point (AP) fallback + Captive Portal**: starts `EZgrow-Setup` AP and captive portal if STA connection fails or no SSID is configured.

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
   - (Core libraries `WiFi.h`, `WebServer.h`, `DNSServer.h`, `Preferences.h` are part of the ESP32 core.)

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

8. **First-time Wi-Fi setup / Captive portal**

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

   - Default web credentials (when first booting):  
     - User: `admin`  
     - Password: `admin`

   Endpoints:

   - `/` : dashboard and charts
   - `/config` : control thresholds, timings, schedules, auth config
   - `/wifi` : Wi-Fi configuration (scan, select SSID, save)

   ### 8.2 If connection fails or no SSID configured (AP + Captive Portal)

   - The ESP32 starts an **access point (AP)**:

     - SSID: `EZgrow-Setup`  
     - Password: _empty_ (open AP, set a password in `initHardware()` if desired)

   - The controller enables a **captive portal**:
     - A DNS server resolves all hostnames to the ESP32 AP IP.
     - Any request to an unknown path is redirected to `/wifi`.

   - Connect your phone or laptop to `EZgrow-Setup`.
   - Most devices will automatically pop up a Wi-Fi sign-in page.  
     If not, open:

     ```text
     http://192.168.4.1/wifi
     ```

   - On the **Wi-Fi page**:
     - Click a network in the table to populate the SSID.
     - Enter your Wi-Fi password.
     - Save.
   - The device will **reboot** and attempt to connect to the selected Wi-Fi network in STA mode.

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
  - Controlled by **temperature OR humidity** with configurable hysteresis:
    - `fanOnTemp`, `fanOffTemp` (°C).
    - `fanHumOn`, `fanHumOff` (% RH).
  - Fan turns ON if temperature ≥ `fanOnTemp` **or** humidity ≥ `fanHumOn`.
  - Fan turns OFF when **both** are back in safe range:
    - temperature ≤ `fanOffTemp` **and** humidity ≤ `fanHumOff`.

- **Pump control** (automatic):
  - Soil moisture-based control using 2 sensors and configurable:
    - Per-chamber configs (`ChamberConfig`): name, dry/wet thresholds, and optional profile IDs.
    - Minimum OFF time (`pumpMinOffSec`).
    - Maximum ON time (`pumpMaxOnSec`).
  - When the pump starts, it records which chambers were below their dry thresholds and only requires those chambers to reach their wet thresholds before shutting off (or when `pumpMaxOnSec` elapses), respecting the minimum OFF interval between cycles.

- **Lights**:
  - Each light can run:
    - In **AUTO** mode using daily schedules (ON/OFF times).
    - In **MANUAL** mode with direct toggling via web UI.
  - Schedules allow intervals that cross midnight (e.g. 20:00–06:00).

### Network Modes

- **STA (station) mode**:
  - Connects to an existing Wi-Fi network using SSID/password stored in **NVS**.
  - Shows assigned IP on Serial and OLED.
  - Web pages are protected by **HTTP Basic Auth** (unless disabled).

- **AP fallback + Captive Portal**:
  - If STA connection fails, or no SSID is configured:
    - Starts AP `EZgrow-Setup` (open by default).
    - AP IP is typically `192.168.4.1`.

## Manual Tests

- **Timezone change is applied only when updated**
  1. Open `/config` and note the current timezone display.
  2. Submit the form without changing the timezone and confirm that NTP/timezone updates are not triggered.
  3. Change the timezone selection, save, and verify the device applies the new timezone once after the configuration is stored.
    - Captive portal redirects all requests to `/wifi`.
    - **No auth is required in AP-only mode** to simplify onboarding.

### Web Interface

- **Dashboard (`/`)** (Basic Auth protected in STA mode):
  - Current time (from NTP).
  - Temperature (°C), humidity (%).
  - Soil moisture 1/2 (%).
  - States and modes of Light 1, Light 2, Fan, Pump (ON/OFF + AUTO/MAN).
  - Light schedule summary.
  - History charts:
    - Temperature and humidity (line chart).
    - Light 1 and Light 2 states (step chart).
  - Links to `/config` and `/wifi`.

- **Configuration (`/config`)** (Basic Auth protected in STA mode):
  - Environment thresholds:
    - Fan ON/OFF temperature.
    - Fan ON/OFF humidity.
    - Soil DRY/WET thresholds.
    - Pump minimum OFF time.
    - Pump maximum ON time.
  - Light 1/2 schedules (ON/OFF time + “use schedule” flags).
  - AUTO/MANUAL for fan and pump.
  - **Web UI authentication section**:
    - Username:
      - If empty, **HTTP authentication is disabled**.
    - Password:
      - If blank on submit, the existing password is kept.
      - If non-empty, updates the stored password.

- **Wi-Fi configuration (`/wifi`)**:
  - Lists available SSIDs (scanned with `WiFi.scanNetworks()`).
  - Shows current connection (if any).
  - Form:
    - SSID (clicking a row in the table fills this).
    - Password.
  - On submit:
    - Credentials are stored in NVS (`gh_wifi`).
    - Device responds with a “saved” page.
    - Device **restarts** and attempts connection with new credentials.

- **History API (`/api/history`)**:
  - JSON feed of the last 24 hours (1 point per minute) with:
    - Timestamp.
    - Temperature.
    - Humidity.
    - Light 1/2 states.

- **Static asset from LittleFS**:
  - `/chart.umd.min.js` – Chart.js UMD bundle served from LittleFS for offline charts.

### Authentication behaviour summary

- **Credentials storage**:
  - Username/password stored in NVS (`gh_auth` namespace).
  - Initial default (when nothing stored):
    - user: `admin`, pass: `admin`.

- **When auth is enforced**:
  - In STA mode (ESP32 connected to a Wi-Fi network), all pages except Chart.js:
    - `/`, `/config`, `/wifi`, `/api/history`, `/toggle`, `/mode` require Basic Auth.
  - If **username is empty** in `/config`, auth is considered disabled:
    - No Basic Auth challenge, all pages are open (not recommended on shared networks).

- **AP + captive portal mode**:
  - When only AP mode is active (STA not connected), **auth is disabled regardless of stored credentials**:
    - The goal is to make onboarding easy.
    - After STA connection is established, auth applies again (if username is non-empty).

---

## 2. Project Structure

```text
controller/
  controller.ino        # Main entry point (setup/loop)
  Greenhouse.h          # Config/state structs, function declarations
  Greenhouse.cpp        # Hardware init, sensors, control logic, Wi-Fi/AP, NTP, history, NVS helpers
  WebUI.h               # Web server API declarations
  WebUI.cpp             # HTTP routes, HTML, config UI, Wi-Fi UI, history, auth, captive portal

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

Main interface (Basic Auth protected in STA mode):

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
  - `/config` (greenhouse logic + auth settings)
  - `/wifi` (network config)

### 4.2 Configuration (`/config`)

Protected by Basic Auth in STA mode. The page uses tabs:

- **Environment**
  - Fan ON/OFF temperature thresholds (°C).
  - Fan ON/OFF humidity thresholds (%RH).
  - Soil DRY/WET thresholds (%).
  - Pump minimum OFF time and maximum ON time (seconds).
- **Lights**
  - Use schedule (AUTO) vs MANUAL toggle for each light.
  - ON/OFF times for Light 1 and Light 2 (`<input type="time">`).
- **Automation**
  - AUTO/MANUAL toggles for the fan and pump.
- **Grow profile**
  - Select Seedling/Vegetative/Flowering presets and apply them per chamber (Ch1 → Light 1, Ch2 → Light 2) or across both chambers + environment.
  - Per-chamber apply only adjusts that chamber's soil thresholds and mapped light schedule/auto flag, leaving other chambers and automation settings untouched.
  - Preview table shows preset values before applying.
- **System**
  - Displays the current device time.
  - Timezone dropdown (UTC, Europe/Berlin, Europe/London, US/Eastern, US/Central, US/Mountain, US/Pacific). Changes apply immediately to NTP/time display.
- **Security**
  - HTTP Basic Auth credentials: username (empty disables auth) and password (blank keeps existing password).

On submit:

- Values are validated (basic sanity).
- Configuration is saved to NVS (via `Preferences`).
- New values are applied immediately (no reboot required).
- Auth changes take effect on the next request.

### 4.3 Wi-Fi configuration (`/wifi`)

Protected by Basic Auth in STA mode, **open in AP-only mode** (onboarding):

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
  Protected by Basic Auth in STA mode.

- `GET /mode?id=light1|light2|fan|pump&auto=0|1`  
  Switches the specified device between AUTO and MANUAL:
  - For lights: toggles use of schedule (AUTO) vs manual relay control.
  - For fan/pump: toggles automatic control logic vs manual relay control.  
  Protected by Basic Auth in STA mode.

### 4.5 History API (`/api/history`)

- Returns a JSON payload containing an array of historical points for the last 24 hours, one per minute.
- Used by the dashboard’s JavaScript to render charts.
- Protected by Basic Auth in STA mode.

---

## 5. Control Logic Details

### 5.1 Fan (Temperature + Humidity)

If `autoFan` is enabled:

- Let `T` = measured temperature, `H` = measured relative humidity.
- Fan turns **ON** when:
  - `T ≥ fanOnTemp` **OR** `H ≥ fanHumOn`
- Fan turns **OFF** when:
  - `T ≤ fanOffTemp` **AND** `H ≤ fanHumOff`  
  (or the respective values are unavailable, in which case they are ignored).

Hysteresis ensures stable behaviour (`fanOnTemp > fanOffTemp` and `fanHumOn > fanHumOff`).

### 5.2 Pump (Soil + Timing-based)

If `autoPump` is enabled:

- “Too dry” if soil sensor 1 `< chamber1.soilDryThreshold` **or** soil sensor 2 `< chamber2.soilDryThreshold`.
- “Wet enough” if soil sensor 1 `> chamber1.soilWetThreshold` **and** soil sensor 2 `> chamber2.soilWetThreshold`.
- Chamber names (max 24 chars) and optional profile IDs are stored per chamber; thresholds are clamped to 0–100 with wet > dry enforced.
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

Then adjust per-chamber dry/wet thresholds (and optional chamber names) via `/config`.

### 7.2 Temperature & Humidity Thresholds

Example configuration:

- `fanOnTemp`  = 28 °C  
- `fanOffTemp` = 26 °C  
- `fanHumOn`   = 80 %  
- `fanHumOff`  = 70 %  

These can be tuned in the config UI to better fit your greenhouse.

---

## 8. Security Considerations

- **Web UI Auth**:
  - HTTP Basic Auth; credentials stored in ESP32 NVS (plain text).
  - Strongly recommended to set a reasonably strong password.
  - You can disable auth by clearing the username field in `/config`.

- **AP / captive portal**:
  - AP is open by default (`EZgrow-Setup` with no password).
  - For more security, set a password in `initHardware()` when calling `WiFi.softAP(...)`.

- **Transport security**:
  - The controller uses plain HTTP (no TLS).
  - Recommended precautions:
    - Keep it on a trusted local network, not exposed directly to the internet.
    - Optionally place it behind a reverse proxy that terminates HTTPS and enforces additional auth.

---

## 9. Possible Extensions

- Use HTTPS (with reverse proxy or ESP32 TLS if resources allow).
- Add multi-user roles or API tokens for automation.
- MQTT integration (for Home Assistant, etc.).
- Long-term data logging to SD card or an external database.
- Additional sensors:
  - CO₂
  - Light intensity
  - ...
- BLE / Bluetooth-based control UI.

---

## 10. License

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
