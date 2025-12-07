# EZgrow
# ESP32 Greenhouse Controller (ESP32-4R-A2)

`controller.ino` implements a small greenhouse controller based on the **ESP32-4R-A2** relay board.  
It controls two lights, a 12 V fan, and a 12 V pump; reads temperature/humidity, soil moisture, and exposes a web UI for monitoring and configuration.

---

## 1. Features

### Hardware

- **Controller board**: ESP32-4R-A2 (4-relay ESP32 module)
- **Actuators** (via onboard relays + external 12 V supply):
  - Light 1 (Relay 1)
  - Light 2 (Relay 2)
  - 12 V fan (Relay 3)
  - 12 V pump (Relay 4)
- **Sensors**:
  - 1× SHT40 (I²C) – temperature and humidity
  - 2× HD38 soil moisture sensors (analog)
- **Display**:
  - WE-DA-361 – 0.91" 128×32 I²C OLED

### Software / Control Logic

- Wi-Fi **station** mode with browser-based UI:
  - Status dashboard at `/`
  - Configuration page at `/config`
- **Automatic control**:
  - Fan: temperature-based, with configurable ON/OFF thresholds (hysteresis)
  - Pump: soil moisture-based, with configurable dry/wet thresholds and ON/OFF timing
  - Lights: optional daily schedules (ON/OFF times, per light) with AUTO/MANUAL modes
- **Manual control**:
  - Web buttons to toggle each relay when in MANUAL mode
- **Configuration stored in flash (NVS)**, no recompile required for:
  - Fan ON temperature (°C)
  - Fan OFF temperature (°C)
  - Soil dry / wet thresholds (%)
  - Pump minimum OFF time (s)
  - Pump maximum ON time (s)
  - Light 1/2 ON/OFF times and “use schedule” flags
  - AUTO/MAN modes for lights, fan, pump
- **Display content** (WE-DA-361):
  - Line 1: Temperature + humidity
  - Line 2: Soil moisture 1 and 2 (%)
  - Line 3: Relay states and modes (L1, L2, F, P with 1/0 and A/M flags)
- Time synchronisation via **NTP** with basic time zone for CET/CEST (Europe/Berlin style)

---

## 2. Hardware Requirements

- ESP32-4R-A2 relay board (LC-Relay-ESP32-4R-A2 or equivalent)
- 12 V power supply for:
  - 2 × lights
  - 1 × fan (12 V)
  - 1 × pump (12 V)
- 1 × SHT40 temperature/humidity sensor (I²C, 3.3 V)
- 2 × HD38 soil moisture sensors (analog output, powered at 3.3 V)
- 1 × WE-DA-361 0.91" OLED (128×32, I²C, 3.3 V)
- Wiring, connectors, and common ground between 12 V power and the ESP32 board

---

## 3. Pinout and Wiring

### 3.1 Relays → Loads

Relays are already wired on the ESP32-4R-A2 board to the following GPIOs:

| Function | Relay | ESP32 GPIO |
|----------|--------|-----------|
| Light 1  | RLY1   | 25        |
| Light 2  | RLY2   | 26        |
| Fan      | RLY3   | 32        |
| Pump     | RLY4   | 33        |

Typical wiring for each load (light/fan/pump):

- 12 V+ → relay **COM**
- Relay **NO** → load +
- Load − → 12 V GND
- 12 V GND → ESP32 GND (common ground)

> The relays are treated as **active LOW** in the code:  
> `LOW` = ON, `HIGH` = OFF (adjustable via `RELAY_ACTIVE_LEVEL` if needed).

### 3.2 I²C Bus (SHT40 + WE-DA-361 OLED)

Use the ESP32’s I²C hardware pins:

| Signal | ESP32 pin | Connected devices          |
|--------|-----------|----------------------------|
| SDA    | 21        | SHT40 SDA, WE-DA-361 SDA   |
| SCL    | 22        | SHT40 SCL, WE-DA-361 SCL   |

Power:

- SHT40:
  - VCC → 3.3 V
  - GND → GND
- WE-DA-361:
  - VCC → 3.3 V (supports 3.3–5 V)
  - GND → GND

If modules do not already include pull-ups on SDA/SCL, add ~4.7 kΩ from SDA to 3.3 V and from SCL to 3.3 V.

### 3.3 Soil Moisture Sensors (HD38)

Use ADC1 pins (required when Wi-Fi is enabled):

| Sensor         | ESP32 pin | Notes          |
|----------------|-----------|----------------|
| Soil sensor 1  | 34        | ADC1_CH6, input only |
| Soil sensor 2  | 35        | ADC1_CH7, input only |

Power both HD38 modules from **3.3 V**:

- VCC → 3.3 V  
- GND → GND  
- AO (analog out) → 34 / 35 respectively

> Powering the HD38 at 3.3 V ensures its analog output never exceeds the ADC input range of the ESP32.

---

## 4. Software Setup

### 4.1 File Structure

For Arduino IDE:

```text
controller/
  controller.ino
```

Ensure the folder name matches the `.ino` file name.

### 4.2 Required Libraries

Install these via **Arduino Library Manager**:

- `Adafruit SHT4X`  
- `Adafruit Unified Sensor`  
- `U8g2`

And ensure you have the **ESP32 board support** installed (e.g. via Espressif’s board manager URL).

### 4.3 Board Settings (Arduino IDE)

- **Board**: `ESP32 Dev Module` (or a specific ESP32-4R-A2 variant if available)
- **Flash size**, **Partition scheme**, **Upload speed**: typical ESP32 defaults are fine.
- **Port**: the COM port corresponding to your board.

### 4.4 Wi-Fi Credentials

In `controller.ino`, set:

```cpp
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";
```

Then compile and upload.

---

## 5. Runtime Behaviour

### 5.1 Web UI

After boot:

1. ESP32 connects to your Wi-Fi network.
2. The serial console prints the assigned IP address.
3. The IP address is also briefly displayed on the OLED.

Open a browser and navigate to:

```text
http://<esp32-ip-address>/
```

#### 5.1.1 Status Page (`/`)

Shows:

- Current time (once NTP is synced)
- Temperature (°C)
- Humidity (%)
- Soil moisture 1 and 2 (%)
- Relay states and modes:
  - Light 1 (L1), Light 2 (L2)
  - Fan (F)
  - Pump (P)
- For each:
  - Status: ON/OFF
  - Mode: AUTO/MANUAL
- Light schedule summary:
  - L1: HH:MM–HH:MM
  - L2: HH:MM–HH:MM

Buttons:

- **Toggle** (when in MANUAL mode)  
- **Switch to AUTO/MANUAL** to change control mode

#### 5.1.2 Configuration Page (`/config`)

Access via the “Configuration” button or directly:

```text
http://<esp32-ip-address>/config
```

Configurable parameters:

- **Fan control**:
  - Fan ON temperature (°C)
  - Fan OFF temperature (°C)
- **Soil moisture thresholds**:
  - Soil DRY threshold (%)
  - Soil WET threshold (%)
- **Pump timing**:
  - Pump minimum OFF time (seconds)
  - Pump maximum ON time (seconds)
- **Light schedules**:
  - “Use schedule for Light 1” (checkbox)
  - Light 1 ON time (HTML `<input type="time">`, e.g. 08:00)
  - Light 1 OFF time
  - “Use schedule for Light 2”
  - Light 2 ON/OFF times

When you press **“Save”**:

- Values are validated (basic sanity checks)
- Parameters are persisted in ESP32 **NVS** via `Preferences`
- No reboot is required; new values are applied immediately

### 5.2 Automatic Control Logic

#### 5.2.1 Fan

- Used when `autoFan == true`:
  - **ON** when `temperature ≥ fanOnTemp`
  - **OFF** when `temperature ≤ fanOffTemp`
- Hysteresis avoids frequent switching.

#### 5.2.2 Pump

- Used when `autoPump == true`:
  - Soil moisture is mapped from raw ADC (0–4095) to 0–100 % (rough scale; calibrate in practice).
  - “Too dry” if either soil 1 or soil 2 `< soilDryThreshold`.
  - “Wet enough` if both soil 1 and soil 2 `> soilWetThreshold`.
- Pump algorithm:
  - If **not running** and:
    - “Too dry” AND
    - Last pump stop > `pumpMinOffSec`
    - → Pump turns **ON**
  - If **running** and:
    - “Wet enough” OR
    - Pump ON time > `pumpMaxOnSec`
    - → Pump turns **OFF**

This protects against rapid cycling and over-watering.

#### 5.2.3 Lights

Two modes per light:

- **AUTO (scheduled)**:
  - Uses NTP time and configured `ON/OFF` times.
  - Times are in minutes since midnight; the code supports schedules that **cross midnight** (e.g. 20:00–06:00).
  - If schedule says “ON” at current time, light state is forced ON; OFF otherwise.
- **MANUAL**:
  - Web UI “Toggle” button directly controls the relay.
  - Schedule is ignored.

You can switch between AUTO and MANUAL from the status page or the config page (for schedules).

---

## 6. Time Zone and NTP

By default, the code uses:

```cpp
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";
```

This is suitable for **Central Europe (CET/CEST)**. If you are in a different time zone, adjust `TZ_INFO` to a correct POSIX time zone string.

NTP servers can also be customised:

```cpp
const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.nist.gov";
```

Time is used only for light schedules and the clock display.

---

## 7. Calibration Notes

### 7.1 Soil Moisture (HD38)

The mapping in the code is a simple:

```cpp
soilPercent = map(rawADC, 0, 4095, 100, 0);
```

You should calibrate for your specific sensors and soil:

1. Insert sensor in **dry** medium, note ADC value `raw_dry`.
2. Insert in **fully wet** medium, note ADC value `raw_wet`.
3. Replace the `map()` with a custom mapping:

```cpp
soilPercent = map(raw, raw_wet, raw_dry, 100, 0);
soilPercent = constrain(soilPercent, 0, 100);
```

Adjust `soilDryThreshold` and `soilWetThreshold` accordingly via the web config.

### 7.2 Temperature / Fan

Choose `fanOnTemp` and `fanOffTemp` appropriate for your plants and environment, e.g.:

- `fanOnTemp` = 28 °C
- `fanOffTemp` = 26 °C

---

## 8. Security Considerations

- The built-in web server uses **plain HTTP**, no authentication or HTTPS.
- For a typical small standalone greenhouse network this may be acceptable, but:
  - Avoid exposing it directly to the public internet.
  - If needed, add:
    - HTTP Basic Auth
    - Firewall rules / VLAN isolation
    - Reverse proxy with TLS in front of the ESP32

---

## 9. Troubleshooting

1. **Display stays blank**
   - Confirm WE-DA-361 is wired:
     - VCC → 3.3 V
     - GND → GND
     - SDA → GPIO 21
     - SCL → GPIO 22
   - Check library constructor: the code assumes `SSD1306 128×32`.
   - Try slower I²C or check for address conflicts if you have multiple I²C devices.

2. **Soil moisture always 0 or 100 %**
   - Verify HD38 sensors are powered correctly (3.3 V).
   - Confirm AO pin is connected to GPIO 34/35, not digital pins.
   - Use `Serial.print(analogRead(...))` to see raw values for calibration.

3. **Relays inverted (ON when they should be OFF)**
   - Adjust `RELAY_ACTIVE_LEVEL` in the code:
     - If `LOW` = ON, keep as is.
     - If `HIGH` = ON, change:
       ```cpp
       const bool RELAY_ACTIVE_LEVEL   = HIGH;
       const bool RELAY_INACTIVE_LEVEL = !RELAY_ACTIVE_LEVEL;
       ```

4. **No NTP time / schedules not working**
   - Ensure the ESP32 can reach the NTP servers (internet access).
   - Check Wi-Fi connectivity and that time shows up on the main page.
   - If your network blocks NTP, use a local NTP server and configure `NTP_SERVER1` accordingly.

5. **Web page not reachable**
   - Check the IP on serial monitor and OLED.
   - Ensure your computer is on the same network.
   - Confirm no firewall is blocking access.

---

## 10. Future Extensions

- Add **JSON API** (`/api/status`) for integration with Home Assistant or other systems.
- Add **MQTT** publish/subscribe for remote monitoring and control.
- Implement **authentication** and optional HTTPS (via a TLS-terminating proxy).
- Log sensor data to an SD card or remote database.
- Add additional sensors (CO₂, light intensity, etc.) on the same I²C bus.

---

## 11. License

Choose an appropriate license for your project (for example, MIT):

```text
MIT License

Copyright (c) <year> <your name>

Permission is hereby granted, free of charge, to any person obtaining a copy
...
```

Replace with your preferred license text and owner.
