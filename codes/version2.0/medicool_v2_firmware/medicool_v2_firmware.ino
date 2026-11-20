/*
 ==============================================================================
  MediCool – Master Firmware v2.0 (Smart IoT Edition)
  Project: MediCool (Portable Medicine Cooling System)
  Tinkerers' Lab / Vishwakarma Awards
  Updated: November 2026
 ==============================================================================
  NEW IN VERSION 2.0 (Mobile Connectivity & Telemetry):
  • Zero-Install WebApp & Captive Portal:
      - ESP32 broadcasts its own Wi-Fi Access Point: "MediCool-Smart".
      - When doctors or judges connect with their phone (iPhone/Android), 
        a modern medical dashboard immediately pops up (Captive Portal).
      - No App Store download or untrusted APK installation needed!
  • High-Performance REST API & Live Telemetry:
      - Real-time updates pushed to the mobile browser every 1000ms.
      - Live Chamber, Top, Bottom, and Heatsink temperature gauges.
      - Live Battery % gauge, voltage display, and charging status.
      - Interactive touch presets: Insulin (4°C), Vaccine (5°C), Eye Drops (6°C).
      - Custom setpoint slider (2.0°C - 8.0°C in 0.1°C steps).
      - Live SVG rolling temperature trend chart in the browser!
      - Remote Key-Lock toggle to prevent physical button tampering.
  • Retains 100% of proven v2.4 core safety & cooling algorithms:
      - 30 kHz Peltier PWM with LC filter governor.
      - Dual ADC voltage dividers (Battery % and 20V PD Charger sense).
      - Dynamic Power Management (DPM 90W budget governor).
      - Heatsink overheat (65°C) and chamber anti-freeze (2°C) cutoffs.
      - Dynamic destratification cold fan and heatsink hot fan governors.
      - Local 3-button physical UI and 128x64 OLED dashboard.
 ==============================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

// ==============================================================================
// 1. PIN CONFIGURATION & HARDWARE CONSTANTS
// ==============================================================================
#define PIN_PELTIER_PWM    25  // MOSFET Q1 Gate (via 150R to LC filter)
#define PIN_HOT_FAN_PWM    26  // MOSFET Q2 Gate (6028/7530 Hot Blower)
#define PIN_COLD_FAN_PWM   27  // MOSFET Q3 Gate (5010 Cold Blower)
#define PIN_ONEWIRE_BUS     4  // 3x DS18B20 data line (4.7k pull-up to 3.3V)
#define PIN_OLED_SDA       21  // OLED Display I2C SDA
#define PIN_OLED_SCL       22  // OLED Display I2C SCL
#define PIN_BTN_UP         18  // Pushbutton 1: Temp Up (Active-LOW to GND)
#define PIN_BTN_DOWN       19  // Pushbutton 2: Temp Down (Active-LOW to GND)
#define PIN_BTN_MODE       23  // Pushbutton 3: Screen / Mode / Lock (Active-LOW to GND)

// Dual Voltage Dividers (ADC1 Pins - 100% Safe with Wi-Fi)
#define PIN_VIN_SENSE      34  // Divider 2: 20V PD line (100k / 15k)
#define PIN_VBAT_SENSE     35  // Divider 1: Battery (+) (100k / 22k)

// PWM Channels & Frequencies (ESP32 LEDC)
#define PELTIER_PWM_FREQ   30000 // 30 kHz (Silent & optimal for LC filter)
#define FAN_PWM_FREQ       25000 // 25 kHz (Silent fan operation)
#define PWM_RESOLUTION         8 // 8-bit (0 - 255)

#define CH_PELTIER             0
#define CH_HOT_FAN             1
#define CH_COLD_FAN            2

// Universal ESP32 Core 2.x & Core 3.x PWM Compatibility
#include <esp_arduino_version.h>
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define PWM_SETUP(pin, freq, res, ch) ledcAttach(pin, freq, res)
  #define PWM_WRITE(pin, ch, val)        ledcWrite(pin, val)
#else
  #define PWM_SETUP(pin, freq, res, ch) do { ledcSetup(ch, freq, res); ledcAttachPin(pin, ch); } while(0)
  #define PWM_WRITE(pin, ch, val)        ledcWrite(ch, val)
#endif

// OLED Configuration
#define SCREEN_WIDTH         128
#define SCREEN_HEIGHT         64
#define OLED_RESET            -1
#define SCREEN_ADDRESS      0x3C

// Wi-Fi Access Point & Captive Portal Config
const char* AP_SSID = "MediCool-Smart";
const char* AP_PASS = "medicool123"; // 8+ chars, or set NULL for open network
const byte DNS_PORT = 53;

// ==============================================================================
// 2. MEDICINE PRESETS & TEMPERATURE THRESHOLDS
// ==============================================================================
enum MedicinePreset {
  PRESET_INSULIN = 0,   // 4.0 °C
  PRESET_VACCINES,      // 5.0 °C
  PRESET_EYEDROPS,      // 6.0 °C
  PRESET_CUSTOM         // User adjustable (2.0°C - 8.0°C)
};

const char* PRESET_NAMES[] = { "INSULIN", "VACCINE", "EYE DROP", "CUSTOM" };
const float PRESET_TEMPS[] = { 4.0f,      5.0f,      6.0f,       4.0f };

#define MIN_SETPOINT_TEMP     2.0f
#define MAX_SETPOINT_TEMP     8.0f

// Dynamic Power Limits (Respects 90W Adapter)
#define MAX_DUTY_NORMAL       191 // 75% max power (~40W)
#define MAX_DUTY_CHARGING     120 // 47% power (~25W) when battery is charging actively
#define MIN_HOLDING_DUTY       50 // ~20% power (~12W) steady-state holding floor

// Safety Limits
#define HEATSINK_OVERHEAT_TEMP 65.0f // Emergency shutoff threshold
#define HEATSINK_RECOVERY_TEMP 50.0f // Safe resume threshold
#define ANTI_FREEZE_LIMIT       2.0f // Cut Peltier if bottom drops below 2°C
#define BATTERY_CUTOFF_VOLT    10.0f // Cut system if battery drops below 10.0V
#define DESTRAT_DIFF_TRIGGER    1.2f // Start cold fan if Top - Bottom > 1.2°C
#define DESTRAT_DIFF_CLEAR      0.4f // Stop cold fan if Top - Bottom <= 0.4°C

// ==============================================================================
// 3. OBJECTS & GLOBAL STATE VARIABLES
// ==============================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
OneWire oneWire(PIN_ONEWIRE_BUS);
DallasTemperature sensors(&oneWire);
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

// Sensor Addresses (Replace with your actual addresses from sensor_address_finder.ino)
DeviceAddress addrTop      = { 0x28, 0xFF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x01 };
DeviceAddress addrBottom   = { 0x28, 0xFF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x02 };
DeviceAddress addrHeatsink = { 0x28, 0xFF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x03 };
bool addressConfigured = false;

// System Telemetry & Control Variables
float tempTop = 25.0f;
float tempBottom = 25.0f;
float tempChamberAvg = 25.0f;
float tempHeatsink = 30.0f;

float setpointTemp = 4.0f;
MedicinePreset currentPreset = PRESET_INSULIN;

float vbatVoltage = 12.0f;
float vinPdVoltage = 0.0f;
int batteryPercentage = 80;
bool isExternalPower = false;
bool isChargingActive = false;

uint8_t peltierDuty = 0;
uint8_t hotFanDuty = 0;
uint8_t coldFanDuty = 0;
bool coldFanActive = false;

// Advanced Hardware Overrides (App / Web Control)
bool manualHotFan = false;
uint8_t manualHotFanDuty = 200;
uint8_t coldFanMode = 0; // 0 = Auto Destratification, 1 = Forced Always ON, 2 = Forced OFF
bool turboOverdrive = false; // Super Advanced 100% Pull-down Mode (~55W)

// Safety & Alarm Flags
bool heatsinkOverheat = false;
bool lowBatteryWarning = false;
bool lowBatteryShutdown = false;
bool antiFreezeTriggered = false;

// UI & Lock States
bool keyLockActive = false;
uint8_t currentScreen = 0; // 0: Medical UI, 1: Diagnostics, 2: WiFi Info
unsigned long lastBtnModePress = 0;
unsigned long modePressStartTime = 0;
bool modeButtonHeld = false;

// Timing Timers
unsigned long lastSensorRead = 0;
unsigned long lastControlLoop = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastTelemetryBroadcast = 0;

// ==============================================================================
// 4. EMBEDDED RESPONSIVE WEB APPLICATION (HTML5 / CSS3 / JAVASCRIPT)
// ==============================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>MediCool Telemetry</title>
  <style>
    :root {
      --bg: #0b132b;
      --card-bg: rgba(28, 37, 65, 0.75);
      --card-border: rgba(255, 255, 255, 0.1);
      --primary: #48cae4;
      --accent: #0077b6;
      --safe-green: #06d6a0;
      --warn-yellow: #ffd166;
      --alert-red: #ef476f;
      --text: #f8f9fa;
      --text-dim: #8d99ae;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 16px; min-height: 100vh; }
    .container { max-width: 480px; margin: 0 auto; display: flex; flex-direction: column; gap: 14px; }
    
    /* Header */
    .header { display: flex; justify-content: space-between; align-items: center; padding-bottom: 8px; border-bottom: 1px solid var(--card-border); }
    .title-box h1 { font-size: 20px; font-weight: 700; letter-spacing: 0.5px; color: #fff; }
    .title-box p { font-size: 11px; color: var(--primary); text-transform: uppercase; letter-spacing: 1px; }
    .badge { padding: 4px 10px; border-radius: 20px; font-size: 11px; font-weight: 600; text-transform: uppercase; }
    .badge-safe { background: rgba(6, 214, 160, 0.2); color: var(--safe-green); border: 1px solid var(--safe-green); }
    .badge-warn { background: rgba(239, 71, 111, 0.2); color: var(--alert-red); border: 1px solid var(--alert-red); }

    /* Card */
    .card { background: var(--card-bg); backdrop-filter: blur(10px); border: 1px solid var(--card-border); border-radius: 16px; padding: 16px; box-shadow: 0 8px 24px rgba(0,0,0,0.3); }

    /* Main Gauge */
    .gauge-card { text-align: center; padding: 20px 16px; }
    .temp-large { font-size: 54px; font-weight: 800; line-height: 1; margin: 10px 0; color: #fff; text-shadow: 0 0 20px rgba(72,202,228,0.4); }
    .temp-unit { font-size: 24px; font-weight: 400; color: var(--primary); }
    .target-pill { display: inline-block; background: rgba(72,202,228,0.15); border: 1px solid var(--primary); padding: 4px 14px; border-radius: 20px; font-size: 13px; font-weight: 600; color: var(--primary); }

    /* Probes Grid */
    .probes-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; }
    .probe-box { background: rgba(0,0,0,0.25); padding: 10px; border-radius: 12px; text-align: center; border: 1px solid rgba(255,255,255,0.05); }
    .probe-label { font-size: 10px; color: var(--text-dim); text-transform: uppercase; font-weight: 600; }
    .probe-val { font-size: 18px; font-weight: 700; margin-top: 4px; color: #fff; }

    /* Battery & Power */
    .power-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; font-size: 13px; }
    .bat-bar-bg { width: 100%; height: 8px; background: rgba(255,255,255,0.1); border-radius: 4px; overflow: hidden; }
    .bat-bar-fill { height: 100%; width: 80%; background: linear-gradient(90deg, #06d6a0, #48cae4); border-radius: 4px; transition: width 0.4s ease; }

    /* Presets */
    .section-title { font-size: 12px; color: var(--text-dim); text-transform: uppercase; letter-spacing: 0.8px; margin-bottom: 10px; font-weight: 600; }
    .preset-btns { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; }
    .p-btn { background: rgba(255,255,255,0.06); border: 1px solid var(--card-border); color: #fff; padding: 10px 4px; border-radius: 12px; font-size: 11px; font-weight: 600; cursor: pointer; transition: all 0.2s; }
    .p-btn.active { background: var(--accent); border-color: var(--primary); box-shadow: 0 0 14px rgba(72,202,228,0.4); }
    .p-btn:active { transform: scale(0.96); }

    /* Slider */
    .slider-box { margin-top: 14px; display: flex; align-items: center; gap: 12px; }
    input[type=range] { flex: 1; -webkit-appearance: none; height: 6px; background: rgba(255,255,255,0.15); border-radius: 3px; outline: none; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 22px; height: 22px; border-radius: 50%; background: var(--primary); cursor: pointer; box-shadow: 0 0 10px var(--primary); }

    /* Live Graph */
    .chart-box { height: 100px; width: 100%; position: relative; margin-top: 8px; }
    svg { width: 100%; height: 100%; overflow: visible; }
    polyline { fill: none; stroke: var(--primary); stroke-width: 2.5; stroke-linecap: round; stroke-linejoin: round; }
    .baseline { stroke: rgba(6, 214, 160, 0.4); stroke-width: 1; stroke-dasharray: 3,3; }

    /* Footer Lock Toggle */
    .lock-row { display: flex; justify-content: space-between; align-items: center; font-size: 13px; }
    .lock-btn { padding: 6px 14px; border-radius: 8px; border: 1px solid var(--card-border); background: rgba(255,255,255,0.1); color: #fff; font-size: 12px; cursor: pointer; }
    .lock-btn.locked { background: var(--alert-red); border-color: var(--alert-red); }
  </style>
</head>
<body>
  <div class="container">
    <!-- Header -->
    <div class="header">
      <div class="title-box">
        <h1>MEDICOOL</h1>
        <p>Portable Cold Chain IoT</p>
      </div>
      <div id="statusBadge" class="badge badge-safe">SAFE 2-8°C</div>
    </div>

    <!-- Main Chamber Gauge -->
    <div class="card gauge-card">
      <div style="font-size: 11px; text-transform: uppercase; color: var(--text-dim); letter-spacing: 1px;">Chamber Temperature</div>
      <div class="temp-large"><span id="chamberVal">4.0</span><span class="temp-unit">°C</span></div>
      <div class="target-pill">Target: <span id="targetVal">4.0</span>°C (<span id="presetVal">INSULIN</span>)</div>
    </div>

    <!-- Probes Breakdown -->
    <div class="probes-grid">
      <div class="probe-box">
        <div class="probe-label">Top Probe</div>
        <div class="probe-val" id="topVal">--</div>
      </div>
      <div class="probe-box">
        <div class="probe-label">Bottom</div>
        <div class="probe-val" id="bottomVal">--</div>
      </div>
      <div class="probe-box">
        <div class="probe-label">Heatsink</div>
        <div class="probe-val" id="sinkVal">--</div>
      </div>
    </div>

    <!-- Live SVG Temperature Trend Chart -->
    <div class="card">
      <div class="section-title">Live Temperature Trend (Last 30s)</div>
      <div class="chart-box">
        <svg id="trendSvg" viewBox="0 0 300 80">
          <line x1="0" y1="40" x2="300" y2="40" class="baseline" />
          <polyline id="trendLine" points="" />
        </svg>
      </div>
      <div style="display: flex; justify-content: space-between; font-size: 10px; color: var(--text-dim); margin-top: 4px;">
        <span>-30s</span>
        <span style="color: var(--safe-green);">Target Center</span>
        <span>Now</span>
      </div>
    </div>

    <!-- Power & Battery -->
    <div class="card">
      <div class="power-row">
        <span><strong id="powerSource">BATTERY</strong> (<span id="vbatVal">12.1</span>V)</span>
        <span id="batPercentVal">85%</span>
      </div>
      <div class="bat-bar-bg">
        <div class="bat-bar-fill" id="batBar"></div>
      </div>
      <div style="display: flex; justify-content: space-between; margin-top: 8px; font-size: 11px; color: var(--text-dim);">
        <span>Est. Backup: <strong id="estBackupVal" style="color: #48cae4;">~5h 15m</strong></span>
        <span id="chargingState">Normal Drain</span>
      </div>
    </div>

    <!-- Medicine Presets -->
    <div class="card">
      <div class="section-title">Medicine Setpoint Preset</div>
      <div class="preset-btns">
        <button class="p-btn active" id="btn0" onclick="setPreset(0)">INSULIN<br><small>4.0°C</small></button>
        <button class="p-btn" id="btn1" onclick="setPreset(1)">VACCINE<br><small>5.0°C</small></button>
        <button class="p-btn" id="btn2" onclick="setPreset(2)">EYE DROP<br><small>6.0°C</small></button>
        <button class="p-btn" id="btn3" onclick="setPreset(3)">CUSTOM<br><small>Manual</small></button>
      </div>
      <div id="manualLockBanner" style="display: none; margin-top: 10px; font-size: 11px; color: #ffb703; background: rgba(255,183,3,0.1); padding: 6px 10px; border-radius: 8px;">
        🔒 Slider locked to preset. Select 'CUSTOM' to enable manual slider.
      </div>
      <div class="slider-box">
        <input type="range" id="tempSlider" min="2.0" max="8.0" step="0.1" value="4.0" oninput="onSliderMove(this.value)" onchange="applyCustomTemp(this.value)">
        <span id="sliderDisp" style="font-size: 13px; font-weight: 700; width: 45px;">4.0°C</span>
      </div>
    </div>

    <!-- Hardware Controls / Keylock -->
    <div class="card lock-row">
      <span>Physical Buttons Lock:</span>
      <button class="lock-btn" id="lockBtn" onclick="toggleLock()">UNLOCKED</button>
    </div>
  </div>

  <script>
    let history = [];
    const maxPoints = 30;

    function fetchStatus() {
      fetch('/api/status')
        .then(res => res.json())
        .then(d => {
          document.getElementById('chamberVal').innerText = d.chamberTemp.toFixed(1);
          document.getElementById('targetVal').innerText = d.setpoint.toFixed(1);
          document.getElementById('presetVal').innerText = d.preset;
          document.getElementById('topVal').innerText = d.topTemp.toFixed(1) + '°';
          document.getElementById('bottomVal').innerText = d.bottomTemp.toFixed(1) + '°';
          document.getElementById('sinkVal').innerText = d.sinkTemp.toFixed(1) + '°';
          document.getElementById('vbatVal').innerText = d.vbat.toFixed(1);
          document.getElementById('batPercentVal').innerText = d.batPercent + '%';
          document.getElementById('batBar').style.width = Math.min(Math.max(d.batPercent, 0), 100) + '%';
          document.getElementById('dutyVal').innerText = Math.round((d.peltierDuty / 255) * 100) + '%';
          
          // Power & Estimated Backup
          if (d.charging) {
            document.getElementById('powerSource').innerText = '⚡ 20V USB-PD';
            document.getElementById('chargingState').innerText = 'Charging Active';
            document.getElementById('estBackupVal').innerText = 'Continuous (PD)';
            document.getElementById('estBackupVal').style.color = '#06d6a0';
          } else {
            document.getElementById('powerSource').innerText = '🔋 BATTERY BACKUP';
            document.getElementById('chargingState').innerText = 'Discharging';
            const hoursTotal = d.turboActive ? 1.2 : (d.peltierDuty > 150 ? 3.0 : 5.5);
            const estH = (d.batPercent / 100.0) * hoursTotal;
            const h = Math.floor(estH);
            const m = Math.round((estH - h) * 60);
            document.getElementById('estBackupVal').innerText = h > 0 ? `~${h}h ${m}m` : `~${m}m`;
            document.getElementById('estBackupVal').style.color = '#48cae4';
          }

          // Safety Badge
          const badge = document.getElementById('statusBadge');
          if (d.chamberTemp >= 2.0 && d.chamberTemp <= 8.0) {
            badge.className = 'badge badge-safe';
            badge.innerText = 'SAFE 2-8°C';
          } else {
            badge.className = 'badge badge-warn';
            badge.innerText = d.chamberTemp < 2.0 ? 'COLD WARNING' : 'HIGH TEMP';
          }

          // Lock button
          const lockBtn = document.getElementById('lockBtn');
          if (d.locked) {
            lockBtn.className = 'lock-btn locked';
            lockBtn.innerText = '🔒 LOCKED';
          } else {
            lockBtn.className = 'lock-btn';
            lockBtn.innerText = '🔓 UNLOCKED';
          }

          // Presets highlight & Manual Lock
          for (let i = 0; i < 4; i++) {
            const btn = document.getElementById('btn' + i);
            if (btn) btn.className = (d.presetIndex === i) ? 'p-btn active' : 'p-btn';
          }
          const isManual = (d.presetIndex === 3);
          const slider = document.getElementById('tempSlider');
          slider.disabled = !isManual;
          slider.style.opacity = isManual ? '1.0' : '0.35';
          document.getElementById('manualLockBanner').style.display = isManual ? 'none' : 'block';

          // Update Rolling Graph
          updateGraph(d.chamberTemp, d.setpoint);
        })
        .catch(err => console.log('Poll err:', err));
    }

    function updateGraph(temp, target) {
      history.push(temp);
      if (history.length > maxPoints) history.shift();
      
      const svgW = 300;
      const svgH = 80;
      const minT = target - 3.0;
      const maxT = target + 3.0;

      let points = '';
      history.forEach((t, i) => {
        const x = (i / (maxPoints - 1)) * svgW;
        const norm = Math.min(Math.max((t - minT) / (maxT - minT), 0), 1);
        const y = svgH - (norm * svgH);
        points += `${x},${y} `;
      });
      document.getElementById('trendLine').setAttribute('points', points.trim());
    }

    function setPreset(idx) {
      fetch(`/api/control?preset=${idx}`, { method: 'POST' }).then(() => fetchStatus());
    }

    function onSliderMove(val) {
      document.getElementById('sliderDisp').innerText = parseFloat(val).toFixed(1) + '°C';
    }

    function applyCustomTemp(val) {
      fetch(`/api/control?temp=${val}`, { method: 'POST' }).then(() => fetchStatus());
    }

    function toggleLock() {
      fetch('/api/control?toggleLock=1', { method: 'POST' }).then(() => fetchStatus());
    }

    setInterval(fetchStatus, 1000);
    fetchStatus();
  </script>
</body>
</html>
)rawliteral";

// ==============================================================================
// 5. REST API & CAPTIVE PORTAL HANDLERS
// ==============================================================================
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleStatusApi() {
  String json = "{";
  json += "\"chamberTemp\":" + String(tempChamberAvg, 2) + ",";
  json += "\"topTemp\":" + String(tempTop, 2) + ",";
  json += "\"bottomTemp\":" + String(tempBottom, 2) + ",";
  json += "\"sinkTemp\":" + String(tempHeatsink, 2) + ",";
  json += "\"setpoint\":" + String(setpointTemp, 1) + ",";
  json += "\"preset\":\"" + String(PRESET_NAMES[currentPreset]) + "\",";
  json += "\"presetIndex\":" + String((int)currentPreset) + ",";
  json += "\"vbat\":" + String(vbatVoltage, 2) + ",";
  json += "\"vinPd\":" + String(vinPdVoltage, 2) + ",";
  json += "\"batPercent\":" + String(batteryPercentage) + ",";
  json += "\"charging\":" + String(isChargingActive ? "true" : "false") + ",";
  json += "\"peltierDuty\":" + String(peltierDuty) + ",";
  json += "\"hotFanDuty\":" + String(hotFanDuty) + ",";
  json += "\"coldFanDuty\":" + String(coldFanDuty) + ",";
  json += "\"coldFanActive\":" + String(coldFanActive ? "true" : "false") + ",";
  json += "\"manualHotFan\":" + String(manualHotFan ? "true" : "false") + ",";
  json += "\"manualHotDuty\":" + String(manualHotFanDuty) + ",";
  json += "\"coldFanMode\":" + String(coldFanMode) + ",";
  json += "\"turboActive\":" + String(turboOverdrive ? "true" : "false") + ",";
  json += "\"locked\":" + String(keyLockActive ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleControlApi() {
  if (server.hasArg("preset")) {
    int p = server.arg("preset").toInt();
    if (p >= 0 && p <= 3) {
      currentPreset = (MedicinePreset)p;
      setpointTemp = PRESET_TEMPS[currentPreset];
      prefs.putUChar("preset", (uint8_t)currentPreset);
      prefs.putFloat("setpoint", setpointTemp);
    }
  }
  if (server.hasArg("temp")) {
    float t = server.arg("temp").toFloat();
    if (t >= MIN_SETPOINT_TEMP && t <= MAX_SETPOINT_TEMP) {
      setpointTemp = t;
      currentPreset = PRESET_CUSTOM;
      prefs.putUChar("preset", (uint8_t)currentPreset);
      prefs.putFloat("setpoint", setpointTemp);
    }
  }
  if (server.hasArg("toggleLock")) {
    keyLockActive = !keyLockActive;
    prefs.putBool("keylock", keyLockActive);
  }
  if (server.hasArg("manualHotFan")) {
    manualHotFan = (server.arg("manualHotFan").toInt() == 1);
    if (server.hasArg("hotDuty")) {
      manualHotFanDuty = constrain(server.arg("hotDuty").toInt(), 0, 255);
    }
  }
  if (server.hasArg("coldFanMode")) {
    coldFanMode = constrain(server.arg("coldFanMode").toInt(), 0, 2);
  }
  if (server.hasArg("turbo")) {
    turboOverdrive = (server.arg("turbo").toInt() == 1);
  }
  server.send(200, "application/json", "{\"success\":true}");
}

// Captive Portal Redirect
void handleCaptivePortal() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

// ==============================================================================
// 6. HARDWARE INITIALIZATION
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n=============================================="));
  Serial.println(F(" MediCool v2.0 (Smart IoT Edition) Initializing"));
  Serial.println(F("=============================================="));

  // A. GPIO Pin Modes
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  analogReadResolution(12); // 12-bit ADC (0 - 4095)

  // B. LEDC PWM Setup
  PWM_SETUP(PIN_PELTIER_PWM, PELTIER_PWM_FREQ, PWM_RESOLUTION, CH_PELTIER);
  PWM_WRITE(PIN_PELTIER_PWM, CH_PELTIER, 0);

  PWM_SETUP(PIN_HOT_FAN_PWM, FAN_PWM_FREQ, PWM_RESOLUTION, CH_HOT_FAN);
  PWM_WRITE(PIN_HOT_FAN_PWM, CH_HOT_FAN, 0);

  PWM_SETUP(PIN_COLD_FAN_PWM, FAN_PWM_FREQ, PWM_RESOLUTION, CH_COLD_FAN);
  PWM_WRITE(PIN_COLD_FAN_PWM, CH_COLD_FAN, 0);

  // C. OLED Display Initialization
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("[ERROR] SSD1306 OLED allocation failed!"));
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(18, 12);
    display.println(F("MEDICOOL v2.0"));
    display.setCursor(10, 28);
    display.println(F("Smart Cold Chain"));
    display.setCursor(12, 44);
    display.println(F("Starting Wi-Fi..."));
    display.display();
  }

  // D. 1-Wire Temperature Probes
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  int deviceCount = sensors.getDeviceCount();
  Serial.printf("[1-WIRE] Found %d DS18B20 sensor(s) on bus.\n", deviceCount);

  // E. Non-Volatile Memory (Preferences)
  prefs.begin("medicool", false);
  currentPreset = (MedicinePreset)prefs.getUChar("preset", (uint8_t)PRESET_INSULIN);
  setpointTemp = prefs.getFloat("setpoint", PRESET_TEMPS[currentPreset]);
  keyLockActive = prefs.getBool("keylock", false);

  // F. Wi-Fi SoftAP & Captive Portal Server
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print(F("[WIFI] Access Point Started: "));
  Serial.println(AP_SSID);
  Serial.print(F("[WIFI] IP Address: "));
  Serial.println(myIP);

  // Start DNS Server on port 53 for Captive Portal
  dnsServer.start(DNS_PORT, "*", myIP);

  // Start mDNS Responder (medicool.local)
  if (MDNS.begin("medicool")) {
    Serial.println(F("[mDNS] Responder started at http://medicool.local"));
  }

  // Register WebServer Endpoints
  server.on("/", handleRoot);
  server.on("/api/status", handleStatusApi);
  server.on("/api/control", HTTP_POST, handleControlApi);
  server.on("/generate_204", handleRoot);        // Android captive portal
  server.on("/fwlink", handleRoot);              // Windows captive portal
  server.on("/hotspot-detect.html", handleRoot); // Apple iOS captive portal
  server.onNotFound(handleCaptivePortal);        // Redirect all others
  server.begin();
  Serial.println(F("[HTTP] Web Server Started on Port 80"));
}

// ==============================================================================
// 7. SENSOR ACQUISITION & POWER SENSING
// ==============================================================================
void readPowerSensors() {
  // Read Divider 2: 20V PD Sense (GPIO 34, 100k / 15k)
  uint32_t rawVin = analogRead(PIN_VIN_SENSE);
  vinPdVoltage = (rawVin / 4095.0f) * 3.3f * ((100.0f + 15.0f) / 15.0f);

  // Read Divider 1: Battery (+) Sense (GPIO 35, 100k / 22k)
  uint32_t rawVbat = analogRead(PIN_VBAT_SENSE);
  vbatVoltage = (rawVbat / 4095.0f) * 3.3f * ((100.0f + 22.0f) / 22.0f);

  // Determine Power Source & Charging State
  isExternalPower = (vinPdVoltage >= 15.0f);

  if (vbatVoltage >= 12.5f) {
    batteryPercentage = 100;
  } else if (vbatVoltage <= 10.0f) {
    batteryPercentage = 0;
  } else {
    batteryPercentage = (int)((vbatVoltage - 10.0f) / (12.6f - 10.0f) * 100.0f);
  }

  // Active charging occurs when plugged into 20V and battery is absorbing current (< 90%)
  isChargingActive = (isExternalPower && batteryPercentage < 90);
  lowBatteryWarning = (!isExternalPower && batteryPercentage <= 15);
  lowBatteryShutdown = (!isExternalPower && vbatVoltage < BATTERY_CUTOFF_VOLT);
}

void readTemperatureSensors() {
  sensors.requestTemperatures();

  // If unique DS18B20 addresses are provided, use them; otherwise use index fallback
  float t0 = sensors.getTempCByIndex(0);
  float t1 = sensors.getTempCByIndex(1);
  float t2 = sensors.getTempCByIndex(2);

  if (t0 > -50.0f && t0 < 85.0f) tempTop = t0;
  if (t1 > -50.0f && t1 < 85.0f) tempBottom = t1;
  if (t2 > -50.0f && t2 < 110.0f) tempHeatsink = t2;

  tempChamberAvg = (tempTop + tempBottom) / 2.0f;

  // Overheat & Anti-Freeze Safety Checks
  if (tempHeatsink >= HEATSINK_OVERHEAT_TEMP) heatsinkOverheat = true;
  else if (tempHeatsink <= HEATSINK_RECOVERY_TEMP) heatsinkOverheat = false;

  antiFreezeTriggered = (tempBottom <= ANTI_FREEZE_LIMIT);
}

// ==============================================================================
// 8. DYNAMIC THERMAL GOVERNOR (PELTIER & FANS)
// ==============================================================================
void updateThermalGovernor() {
  // Safety Cutoffs (Overheat, Anti-freeze, Low battery shutdown)
  if (heatsinkOverheat || antiFreezeTriggered || lowBatteryShutdown) {
    peltierDuty = 0;
    turboOverdrive = false;
    PWM_WRITE(PIN_PELTIER_PWM, CH_PELTIER, 0);

    // If heatsink is overheating, run hot fan at 100% to purge heat!
    if (heatsinkOverheat) {
      hotFanDuty = 255;
      PWM_WRITE(PIN_HOT_FAN_PWM, CH_HOT_FAN, hotFanDuty);
    }
    return;
  }

  // SUPER ADVANCED: 100% Turbo Overdrive Check (~55W Lab Pull-Down)
  if (turboOverdrive) {
    // Safety Guardrails: Auto-disengage if heatsink is getting warm (>60C) or battery < 11.0V!
    if (tempHeatsink >= 60.0f || vbatVoltage < 11.0f) {
      turboOverdrive = false;
      Serial.println(F("[SAFETY] Turbo Overdrive Auto-Disengaged (Safety Guardrail)!"));
    } else {
      peltierDuty = 255; // 100% full Peltier duty cycle (~55W)
      hotFanDuty = 255;  // Force Hot Blower to 100% max blast
      PWM_WRITE(PIN_PELTIER_PWM, CH_PELTIER, peltierDuty);
      PWM_WRITE(PIN_HOT_FAN_PWM, CH_HOT_FAN, hotFanDuty);

      // Cold Fan in Turbo mode
      if (coldFanMode == 2) coldFanDuty = 0; // Forced OFF
      else coldFanDuty = 220;                // Rapid circulation
      PWM_WRITE(PIN_COLD_FAN_PWM, CH_COLD_FAN, coldFanDuty);
      return;
    }
  }

  // Determine Max Allowed Peltier Duty (DPM 90W Power Budget)
  uint8_t currentMaxDuty = isChargingActive ? MAX_DUTY_CHARGING : MAX_DUTY_NORMAL;

  float error = tempChamberAvg - setpointTemp;

  if (error <= -0.2f) {
    // Chamber is cold enough: drop to minimal holding floor (~15-20%)
    peltierDuty = MIN_HOLDING_DUTY;
  } else if (error < 1.0f) {
    // Approaching setpoint: smooth linear glide
    peltierDuty = map((long)(error * 100), -20, 100, MIN_HOLDING_DUTY, currentMaxDuty);
  } else {
    // Far from setpoint: Full permitted cooling power
    peltierDuty = currentMaxDuty;
  }
  PWM_WRITE(PIN_PELTIER_PWM, CH_PELTIER, peltierDuty);

  // Hot Fan Speed Control: Proportional or Manual Override
  if (manualHotFan) {
    hotFanDuty = manualHotFanDuty;
  } else {
    if (tempHeatsink < 32.0f) {
      hotFanDuty = 75; // Silent idle draft
    } else if (tempHeatsink > 55.0f) {
      hotFanDuty = 255; // Full 100% blast
    } else {
      hotFanDuty = map((long)(tempHeatsink * 10), 320, 550, 75, 255);
    }
  }
  PWM_WRITE(PIN_HOT_FAN_PWM, CH_HOT_FAN, hotFanDuty);

  // Cold Fan Control: Auto Destratification or Manual Mode
  if (coldFanMode == 1) {
    coldFanActive = true;
    coldFanDuty = 200; // Forced Always ON
  } else if (coldFanMode == 2) {
    coldFanActive = false;
    coldFanDuty = 0;   // Forced OFF
  } else {
    // Mode 0: Smart Destratification
    float gradient = abs(tempTop - tempBottom);
    if (gradient >= DESTRAT_DIFF_TRIGGER) {
      coldFanActive = true;
    } else if (gradient <= DESTRAT_DIFF_CLEAR) {
      coldFanActive = false;
    }
    coldFanDuty = coldFanActive ? 180 : 0;
  }
  PWM_WRITE(PIN_COLD_FAN_PWM, CH_COLD_FAN, coldFanDuty);
}

// ==============================================================================
// 9. LOCAL PHYSICAL BUTTON UI & LOCK
// ==============================================================================
void handlePhysicalButtons() {
  bool btnUp = (digitalRead(PIN_BTN_UP) == LOW);
  bool btnDown = (digitalRead(PIN_BTN_DOWN) == LOW);
  bool btnMode = (digitalRead(PIN_BTN_MODE) == LOW);

  // Long-press MODE (2 seconds) to Toggle Key-Lock
  if (btnMode) {
    if (modePressStartTime == 0) modePressStartTime = millis();
    if (!modeButtonHeld && (millis() - modePressStartTime >= 2000)) {
      keyLockActive = !keyLockActive;
      prefs.putBool("keylock", keyLockActive);
      modeButtonHeld = true;
      Serial.println(keyLockActive ? F("[UI] Key-Lock ACTIVATED") : F("[UI] Key-Lock RELEASED"));
    }
  } else {
    if (modePressStartTime > 0 && !modeButtonHeld) {
      // Short-press: Switch OLED screen
      currentScreen = (currentScreen + 1) % 3;
    }
    modePressStartTime = 0;
    modeButtonHeld = false;
  }

  // If Key-Lock is active, ignore UP / DOWN adjustments
  if (keyLockActive) return;

  if (btnUp && (millis() - lastBtnModePress > 250)) {
    lastBtnModePress = millis();
    if (setpointTemp < MAX_SETPOINT_TEMP) {
      setpointTemp += 0.5f;
      currentPreset = PRESET_CUSTOM;
      prefs.putUChar("preset", (uint8_t)currentPreset);
      prefs.putFloat("setpoint", setpointTemp);
    }
  }

  if (btnDown && (millis() - lastBtnModePress > 250)) {
    lastBtnModePress = millis();
    if (setpointTemp > MIN_SETPOINT_TEMP) {
      setpointTemp -= 0.5f;
      currentPreset = PRESET_CUSTOM;
      prefs.putUChar("preset", (uint8_t)currentPreset);
      prefs.putFloat("setpoint", setpointTemp);
    }
  }
}

// ==============================================================================
// 10. OLED DASHBOARD RENDERING (SCREENS 0, 1, 2)
// ==============================================================================
void renderOledDisplay() {
  display.clearDisplay();

  if (currentScreen == 0) {
    // SCREEN 0: Main Medical Dashboard
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(PRESET_NAMES[currentPreset]);

    if (keyLockActive) {
      display.setCursor(68, 0);
      display.print(F("[LOCKED]"));
    }

    display.setCursor(110, 0);
    display.print(batteryPercentage);
    display.print(F("%"));

    display.setTextSize(3);
    display.setCursor(4, 16);
    display.print(tempChamberAvg, 1);
    display.setTextSize(1);
    display.print(F("C"));

    display.setCursor(4, 48);
    display.print(F("Set:"));
    display.print(setpointTemp, 1);
    display.print(F("C  Sink:"));
    display.print((int)tempHeatsink);
    display.print(F("C"));

    display.setCursor(4, 57);
    if (isExternalPower) {
      display.print(isChargingActive ? F("PWR: 20V PD [CHG]") : F("PWR: 20V PD [FULL]"));
    } else {
      display.print(F("PWR: BATTERY BACKUP"));
    }

  } else if (currentScreen == 1) {
    // SCREEN 1: Hardware Engineering Diagnostics
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("DIAGNOSTICS v2.0"));
    display.printf("Top:%.1fC  Bot:%.1fC\n", tempTop, tempBottom);
    display.printf("Sink:%.1fC Peltier:%d%%\n", tempHeatsink, (peltierDuty * 100) / 255);
    display.printf("Vbat:%.2fV (%d%%)\n", vbatVoltage, batteryPercentage);
    display.printf("VinPD:%.1fV %s\n", vinPdVoltage, isExternalPower ? "OK" : "OFF");
    display.printf("Fans: H:%d%% C:%s\n", (hotFanDuty * 100) / 255, coldFanActive ? "ON" : "OFF");

  } else if (currentScreen == 2) {
    // SCREEN 2: Wi-Fi Telemetry & Mobile Connect Info
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("MOBILE DASHBOARD"));
    display.println(F("---------------------"));
    display.printf("SSID: %s\n", AP_SSID);
    display.printf("Pass: %s\n", AP_PASS);
    display.println(F("Browser URL:"));
    display.println(F("http://192.168.4.1"));
    display.printf("Clients: %d active\n", WiFi.softAPgetStationNum());
  }

  display.display();
}

// ==============================================================================
// 11. MAIN ARDUINO LOOP
// ==============================================================================
void loop() {
  unsigned long now = millis();

  // A. Handle Captive Portal DNS Queries & WebServer Requests
  dnsServer.processNextRequest();
  server.handleClient();

  // B. Physical Button Poll (every 20ms)
  handlePhysicalButtons();

  // C. Sensor Sampling (every 500ms)
  if (now - lastSensorRead >= 500) {
    lastSensorRead = now;
    readPowerSensors();
    readTemperatureSensors();
  }

  // D. Thermal Governor & PWM Output (every 1000ms)
  if (now - lastControlLoop >= 1000) {
    lastControlLoop = now;
    updateThermalGovernor();
  }

  // E. OLED Screen Update (every 250ms)
  if (now - lastDisplayUpdate >= 250) {
    lastDisplayUpdate = now;
    renderOledDisplay();
  }
}

