# MediCool v2.0 – Smart IoT & Mobile Telemetry Firmware

**Document Version:** 2.0  
**Project:** MediCool (Portable Medicine Cooling System)  
**Tinkerers' Lab / Vishwakarma Awards**  
**Location:** `codes/version2.0/medicool_v2_firmware.ino`

---

## 1. Overview & Strategy

Version 2.0 upgrades MediCool from a standalone hardware unit into an **IoT-Connected Smart Medical Cold Chain Device**, while strictly preserving the proven safety cutoffs, DPM governor, and LC inverter drive from v2.4.

### Why This New Folder Exists:
* **Zero Risk:** Your rock-solid v2.4 firmware (`codes/medicool_firmware.ino`) remains completely untouched as a proven fallback.
* **Demonstration Readiness:** If time runs tight before stage submission, you can demonstrate the base unit; if v2.0 is loaded, you have a smartphone-controlled medical device that stands out during judging.

---

## 2. Mobile App vs Embedded WebApp: Why We Chose the WebApp

| Criterion | Native Mobile App (APK / iOS) | MediCool v2.0 Embedded WebApp |
| :--- | :--- | :--- |
| **Installation** | Requires installing untrusted APKs on Android; nearly impossible on judges' iPhones. | **ZERO INSTALLATION.** Works on ANY smartphone instantly via browser. |
| **Jury / Evaluator Experience** | High friction. Judges will refuse to install random APKs on their personal phones. | **Flawless.** Ask the judge: *"Please connect to 'MediCool-Smart' or scan this QR code"*. The medical telemetry dashboard opens instantly! |
| **Offline Reliability** | Bluetooth crashes, location permission errors, background OS killing. | **100% Offline.** The ESP32 acts as its own autonomous Wi-Fi server. Works in ambulances, rural clinics, or flights. |
| **UI Quality** | Standard system controls. | **Medical Glassmorphism UI** with live temperature dials, glowing battery bars, and live SVG trend charts. |

---

## 3. How Pairing & Mobile Connection Works

```text
[ESP32 MediCool]
      │
      ├── 1. Broadcasts Wi-Fi Access Point: "MediCool-Smart"
      │
      ▼
[Doctor / Judge Smartphone]
      │
      ├── 2. Connects to Wi-Fi (Password: "medicool123")
      │
      ▼
[Automatic Captive Portal]
      │
      └── 3. Phone immediately prompts: "Sign in to network"
             (or open browser to http://192.168.4.1 or http://medicool.local)
             ► Full Medical Telemetry Dashboard appears on screen!
```

---

## 4. Key Features of the Mobile Dashboard

1. **Live Temperature Gauges:**
   * Big digital chamber readout with dynamic status badges:
     * **Green:** `SAFE 2-8°C` (Medical cold-chain compliant).
     * **Amber/Red:** `COLD WARNING` or `HIGH TEMP`.
2. **Multi-Probe Visibility:**
   * Instant readouts of **Top Chamber Probe**, **Bottom Chamber Probe**, and **Hot Heatsink Probe**.
3. **Live SVG Rolling Trend Chart:**
   * Real-time rolling graph of the last 30 seconds of temperature readings relative to the target setpoint.
4. **Power & Battery Telemetry:**
   * Battery percentage bar, exact voltage readout ($10.0\text{V} - 12.6\text{V}$).
   * Dynamic power indicator: `⚡ 20V USB-PD (Charging Active)` vs `🔋 Battery Backup (Discharging)`.
5. **Interactive Controls:**
   * One-touch preset buttons: **Insulin ($4.0^\circ\text{C}$)**, **Vaccine ($5.0^\circ\text{C}$)**, **Eye Drops ($6.0^\circ\text{C}$)**.
   * Smooth slider for **Custom ($2.0^\circ\text{C} - 8.0^\circ\text{C}$)** with immediate hardware synchronization.
6. **Remote Key-Lock:**
   * Doctors can lock the physical buttons on the cooler box remotely from their phone to prevent unauthorized tampering during transit!

---

## 5. Live REST API Endpoints

The ESP32 firmware hosts lightweight JSON REST endpoints:

* **`GET /api/status`**
  Returns complete real-time telemetry:
  ```json
  {
    "chamberTemp": 4.1,
    "topTemp": 4.3,
    "bottomTemp": 3.9,
    "sinkTemp": 38.5,
    "setpoint": 4.0,
    "preset": "INSULIN",
    "presetIndex": 0,
    "vbat": 12.18,
    "vinPd": 20.1,
    "batPercent": 84,
    "charging": false,
    "peltierDuty": 120,
    "hotFanDuty": 150,
    "coldFanDuty": 0,
    "locked": false
  }
  ```

* **`POST /api/control?preset=0`** (0 = Insulin, 1 = Vaccine, 2 = Eye Drops)
* **`POST /api/control?temp=4.5`** (Sets custom temperature)
* **`POST /api/control?toggleLock=1`** (Toggles physical key-lock)
* **`POST /api/control?manualHotFan=1&hotDuty=200`** (Manual Hot Fan PWM override 0-255)
* **`POST /api/control?coldFanMode=1`** (0 = Auto Destratification, 1 = Always ON, 2 = Forced OFF)
* **`POST /api/control?turbo=1`** (Super Advanced 100% Turbo Overdrive Mode)

---

## 6. Advanced Hardware & Turbo Overdrive Capabilities

### A. Manual Fan Speed Overrides
* **Hot Blower Fan (6028/7530):** Can be switched from **Auto Thermal Governor** to **Manual Slider (0% - 100%)** to force maximum airflow during testing or set to quiet mode.
* **Cold Blower Fan (5010):** Three selectable modes:
  * `AUTO DIFF`: Runs dynamically only when vertical gradient exceeds $1.2^\circ\text{C}$ (preserves battery).
  * `ALWAYS ON`: Constant gentle internal air circulation.
  * `FORCED OFF`: For zero-vibration transport.

### B. Super Advanced 100% Turbo Pull-Down Overdrive
* Pushes the TEC1-12706 to **$100\%$ full PWM duty cycle ($\approx 55\text{W}$)** for rapid pull-down from ambient ($35^\circ\text{C} \to 4^\circ\text{C}$).
* **Hardware Safety Guardrails Built-in:**
  1. Auto-disengages if heatsink temperature exceeds **$60^\circ\text{C}$**.
  2. Auto-disengages if battery drops below **$11.0\text{V}$** (protects Li-ion cells from brownout).
  3. Forces the Hot Blower to **$100\%$ full blast** to aggressively reject heat.

---

## 7. How to Flash & Run

1. Open `codes/version2.0/medicool_v2_firmware.ino` in Arduino IDE.
2. Ensure you have the standard board package: **esp32 by Espressif Systems**.
3. Libraries used (all standard, no obscure dependencies):
   * `WiFi.h`, `WebServer.h`, `DNSServer.h`, `ESPmDNS.h` (Built into ESP32 core).
   * `Adafruit SSD1306` & `Adafruit GFX`.
   * `OneWire` & `DallasTemperature`.
   * `Preferences` (Built into ESP32 core).
4. Flash to ESP32:
   * Board: **ESP32 Dev Module**
   * Upload Speed: **921600**
   * Flash Frequency: **80MHz**
   * Partition Scheme: **Default 4MB with SPIFFS (1.2MB APP / 1.5MB SPIFFS)**

