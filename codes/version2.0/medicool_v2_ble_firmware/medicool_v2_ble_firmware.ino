/*
 ==============================================================================
  MediCool – Master Firmware v2.0 (Smart Bluetooth BLE Edition)
  Project: MediCool (Portable Medicine Cooling System)
  Tinkerers' Lab / Vishwakarma Awards
  Updated: November 2026
 ==============================================================================
  FEATURES:
  • Pure Native Bluetooth Low Energy (BLE 4.2 / 5.0):
      - Advertises as "MediCool-Smart".
      - Phone stays connected to normal 4G/5G/Wi-Fi internet while monitoring cooler!
      - Instant auto-pairing and ultra-low power consumption.
  • GATT Telemetry Service (Notify every 1000ms):
      - Live Chamber, Top, Bottom, and Heatsink temperatures.
      - Battery percentage, live voltage, and 20V USB-PD charge status.
      - Peltier duty cycle, hot blower RPM %, cold fan state.
      - Target setpoint, preset mode, turbo status, and physical keylock state.
  • GATT Remote Control Service (Write):
      - Instant preset selection: Insulin (4°C), Vaccine (5°C), Eye Drops (6°C).
      - Smooth target setpoint slider (2.0°C - 8.0°C).
      - Hot fan speed manual override and cold fan destratification modes.
      - 100% Turbo Overdrive pull-down mode with automatic safety disengage.
      - Remote button keylock toggle to prevent physical tampering.
  • Retains 100% of proven v2.4 core safety & cooling algorithms:
      - 30 kHz Peltier PWM with LC filter governor.
      - Heatsink overheat (65°C) and chamber anti-freeze (2°C) cutoffs.
      - Dynamic destratification cold fan and heatsink hot fan governors.
      - Local 3-button physical UI and 128x64 OLED dashboard.
 ==============================================================================
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <esp_arduino_version.h>

// ==============================================================================
// 1. PIN CONFIGURATION & HARDWARE CONSTANTS
// ==============================================================================
#define PIN_PELTIER_PWM    25  // Peltier Gate Driver via LC Filter
#define PIN_HOT_FAN_PWM    26  // Heatsink Exhaust Blower
#define PIN_COLD_FAN_PWM   27  // Chamber Destratification Fan
#define PIN_ONEWIRE_BUS     4  // DS18B20 1-Wire Bus (3x Sensors)
#define PIN_BTN_UP         18  // Physical Pushbutton: Increment / Up
#define PIN_BTN_DOWN       19  // Physical Pushbutton: Decrement / Down
#define PIN_BTN_MODE       23  // Physical Pushbutton: Mode Select / Long-Press Lock
#define PIN_OLED_SDA       21  // I2C OLED Display SDA
#define PIN_OLED_SCL       22  // I2C OLED Display SCL

// Dual Voltage Dividers (ADC1 Pins)
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

// BLE UUIDs (Matching Flutter Mobile Companion App)
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TELEMETRY_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CONTROL_CHAR_UUID   "0000ff01-0000-1000-8000-00805f9b34fb"

// Thermal Control & Safety Thresholds
const float HEATSINK_OVERHEAT_LIMIT = 65.0f; // Max safe heatsink temperature (°C)
const float CHAMBER_ANTIFREEZE_LIMIT = 2.0f; // Hard anti-freeze floor (°C)
const float DESTRAT_DIFF_TRIGGER    = 0.8f;  // Trigger cold fan if Top - Bottom >= 0.8°C
const float DESTRAT_DIFF_CLEAR      = 0.3f;  // Turn off cold fan if Top - Bottom <= 0.3°C
const uint8_t MAX_DUTY_NORMAL       = 204;   // 80% duty cycle on 12V battery (~42W)
const uint8_t MAX_DUTY_CHARGING     = 166;   // 65% duty cycle on 20V PD charger (~50W)
const uint8_t MIN_HOLDING_DUTY      = 38;    // ~15% duty cycle holding floor

// Medicine Presets
struct MedicinePreset {
  const char* name;
  float targetTemp;
};

const MedicinePreset PRESETS[] = {
  {"INSULIN",   4.0f},
  {"VACCINES",  5.0f},
  {"EYE DROPS", 6.0f},
  {"CUSTOM",    4.0f}
};
const uint8_t NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// ==============================================================================
// 2. HARDWARE INSTANCES & GLOBAL STATE
// ==============================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
OneWire oneWire(PIN_ONEWIRE_BUS);
DallasTemperature sensors(&oneWire);
Preferences prefs;

// BLE Server & Characteristics
BLEServer* pServer = nullptr;
BLECharacteristic* pTelemetryChar = nullptr;
BLECharacteristic* pControlChar = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// System Telemetry Variables
float tempChamberAvg = 4.0f;
float tempTop        = 4.0f;
float tempBottom     = 4.0f;
float tempHeatsink   = 30.0f;
float setpointTemp   = 4.0f;
uint8_t currentPreset = 0;

uint8_t peltierDuty  = 0;
uint8_t hotFanDuty   = 0;
uint8_t coldFanDuty  = 0;
bool coldFanActive   = false;

// Fan Control Modes (from Mobile App)
bool manualHotFan        = false;
uint8_t manualHotFanDuty = 200;
uint8_t coldFanMode      = 0; // 0=Auto Destrat, 1=Always ON, 2=Forced OFF

// Turbo Overdrive (100% Peltier ~55W)
bool turboOverdrive = false;

// Power & Battery
float vbatVoltage       = 12.0f;
float vinPdVoltage      = 0.0f;
uint8_t batteryPercent  = 100;
bool isChargingActive   = false;
bool lowBatteryShutdown = false;

// Safety & Alarm Flags
bool heatsinkOverheat     = false;
bool antiFreezeTriggered  = false;
bool isKeyLocked          = false;

// Loop Timing & Intervals
unsigned long lastSensorReadTime  = 0;
unsigned long lastBleNotifyTime   = 0;
unsigned long lastDisplayTime     = 0;
unsigned long uptimeSeconds       = 0;

// Forward Declarations
void updateThermalGovernor();
void sendBleTelemetry();
void updateOLED();
void handlePhysicalButtons();
void readPowerSensors();
void readTemperatures();

// ==============================================================================
// 3. BLE SERVER CALLBACKS & CONTROL RECEIVER
// ==============================================================================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println(F("[BLE] Mobile Phone Connected!"));
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println(F("[BLE] Mobile Phone Disconnected!"));
  }
};

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.print(F("[BLE COMMAND RECEIVED] "));
      Serial.println(rxValue);

      // Parse commands: e.g. "preset=1", "temp=4.5", "turbo=1", "hotDuty=220", "manualHot=1", "coldMode=1", "toggleLock=1"
      if (rxValue.startsWith("preset=")) {
        int idx = rxValue.substring(7).toInt();
        if (idx >= 0 && idx < NUM_PRESETS) {
          currentPreset = idx;
          setpointTemp = PRESETS[idx].targetTemp;
          prefs.putUChar("preset", currentPreset);
          prefs.putFloat("setpoint", setpointTemp);
        }
      } else if (rxValue.startsWith("temp=")) {
        float t = rxValue.substring(5).toFloat();
        if (t >= 2.0f && t <= 8.0f) {
          setpointTemp = t;
          currentPreset = 3; // CUSTOM
          prefs.putUChar("preset", currentPreset);
          prefs.putFloat("setpoint", setpointTemp);
        }
      } else if (rxValue.startsWith("turbo=")) {
        int val = rxValue.substring(6).toInt();
        turboOverdrive = (val == 1);
        Serial.print(F("[TURBO OVERDRIVE] "));
        Serial.println(turboOverdrive ? F("ENGAGED (100% Peltier)") : F("DISENGAGED"));
      } else if (rxValue.startsWith("hotDuty=")) {
        int duty = rxValue.substring(8).toInt();
        manualHotFanDuty = constrain(duty, 0, 255);
      } else if (rxValue.startsWith("manualHot=")) {
        int val = rxValue.substring(10).toInt();
        manualHotFan = (val == 1);
      } else if (rxValue.startsWith("coldMode=")) {
        int val = rxValue.substring(9).toInt();
        coldFanMode = constrain(val, 0, 2);
      } else if (rxValue == "toggleLock=1") {
        isKeyLocked = !isKeyLocked;
      }

      // Trigger immediate state update
      updateThermalGovernor();
      sendBleTelemetry();
    }
  }
};

// ==============================================================================
// 4. SETUP
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n=============================================="));
  Serial.println(F(" MediCool v2.0 (Smart BLE Edition) Booting"));
  Serial.println(F("=============================================="));

  // A. GPIO Pin Modes
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  analogReadResolution(12);

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
    display.setCursor(12, 28);
    display.println(F("Smart BLE Edition"));
    display.setCursor(20, 44);
    display.println(F("BLE Advertising..."));
    display.display();
  }

  // D. 1-Wire Temperature Sensors
  sensors.begin();
  sensors.setResolution(11);
  sensors.setWaitForConversion(false);

  // E. Non-Volatile Memory (Preferences)
  prefs.begin("medicool", false);
  currentPreset = prefs.getUChar("preset", 0);
  setpointTemp = prefs.getFloat("setpoint", 4.0f);
  if (currentPreset >= NUM_PRESETS) currentPreset = 0;
  if (setpointTemp < 2.0f || setpointTemp > 8.0f) setpointTemp = 4.0f;

  // F. Initialize Native Bluetooth Low Energy (BLE)
  BLEDevice::init("MediCool-Smart");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Telemetry Notify Characteristic
  pTelemetryChar = pService->createCharacteristic(
    TELEMETRY_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pTelemetryChar->addDescriptor(new BLE2902());

  // Control Write Characteristic
  pControlChar = pService->createCharacteristic(
    CONTROL_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pControlChar->setCallbacks(new ControlCallbacks());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println(F("[BLE] MediCool-Smart BLE Peripheral Advertising Started!"));
}

// ==============================================================================
// 5. MAIN LOOP
// ==============================================================================
void loop() {
  unsigned long now = millis();

  // Re-start advertising if disconnected
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println(F("[BLE] Disconnected -> Restarting Advertising"));
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // 1. Read Sensors every 1000ms
  if (now - lastSensorReadTime >= 1000) {
    lastSensorReadTime = now;
    uptimeSeconds++;
    readPowerSensors();
    readTemperatures();
    updateThermalGovernor();
  }

  // 2. Transmit BLE Telemetry every 1000ms
  if (now - lastBleNotifyTime >= 1000) {
    lastBleNotifyTime = now;
    sendBleTelemetry();
  }

  // 3. Physical UI Buttons
  handlePhysicalButtons();

  // 4. Update OLED Display every 250ms
  if (now - lastDisplayTime >= 250) {
    lastDisplayTime = now;
    updateOLED();
  }
}

// ==============================================================================
// 6. SENSORS & POWER ACQUISITION
// ==============================================================================
void readPowerSensors() {
  uint32_t rawVin = analogRead(PIN_VIN_SENSE);
  vinPdVoltage = (rawVin / 4095.0f) * 3.3f * ((100.0f + 15.0f) / 15.0f);
  isChargingActive = (vinPdVoltage >= 18.0f);

  uint32_t rawVbat = analogRead(PIN_VBAT_SENSE);
  vbatVoltage = (rawVbat / 4095.0f) * 3.3f * ((100.0f + 22.0f) / 22.0f);

  if (vbatVoltage >= 12.6f) batteryPercent = 100;
  else if (vbatVoltage <= 9.6f) batteryPercent = 0;
  else batteryPercent = (uint8_t)(((vbatVoltage - 9.6f) / (12.6f - 9.6f)) * 100.0f);

  if (vbatVoltage <= 9.6f && !isChargingActive) {
    lowBatteryShutdown = true;
  } else if (vbatVoltage >= 10.5f) {
    lowBatteryShutdown = false;
  }
}

void readTemperatures() {
  sensors.requestTemperatures();
  float t0 = sensors.getTempCByIndex(0);
  float t1 = sensors.getTempCByIndex(1);
  float t2 = sensors.getTempCByIndex(2);

  if (t0 > -50.0f && t0 < 85.0f) tempTop = t0;
  if (t1 > -50.0f && t1 < 85.0f) tempBottom = t1;
  if (t2 > -50.0f && t2 < 120.0f) tempHeatsink = t2;

  tempChamberAvg = (tempTop + tempBottom) / 2.0f;

  heatsinkOverheat = (tempHeatsink >= HEATSINK_OVERHEAT_LIMIT);
  antiFreezeTriggered = (tempChamberAvg <= CHAMBER_ANTIFREEZE_LIMIT);
}

// ==============================================================================
// 7. DYNAMIC THERMAL GOVERNOR (PELTIER & FANS)
// ==============================================================================
void updateThermalGovernor() {
  // Safety Cutoffs
  if (heatsinkOverheat || antiFreezeTriggered || lowBatteryShutdown) {
    peltierDuty = 0;
    turboOverdrive = false;
    PWM_WRITE(PIN_PELTIER_PWM, CH_PELTIER, 0);
    if (heatsinkOverheat) {
      hotFanDuty = 255;
      PWM_WRITE(PIN_HOT_FAN_PWM, CH_HOT_FAN, hotFanDuty);
    }
    return;
  }

  // 100% Turbo Overdrive (~55W Lab Pull-Down)
  if (turboOverdrive) {
    if (tempHeatsink >= 60.0f || vbatVoltage < 11.0f) {
      turboOverdrive = false;
      Serial.println(F("[SAFETY] Turbo Disengaged (Thermal/Voltage Guardrail)!"));
    } else {
      peltierDuty = 255;
      hotFanDuty = 255;
      PWM_WRITE(PIN_PELTIER_PWM, CH_PELTIER, peltierDuty);
      PWM_WRITE(PIN_HOT_FAN_PWM, CH_HOT_FAN, hotFanDuty);
      coldFanDuty = (coldFanMode == 2) ? 0 : 220;
      PWM_WRITE(PIN_COLD_FAN_PWM, CH_COLD_FAN, coldFanDuty);
      return;
    }
  }

  // Normal DPM Power Budgeting
  uint8_t currentMaxDuty = isChargingActive ? MAX_DUTY_CHARGING : MAX_DUTY_NORMAL;
  float error = tempChamberAvg - setpointTemp;

  if (error <= -0.2f) {
    peltierDuty = MIN_HOLDING_DUTY;
  } else if (error < 1.0f) {
    peltierDuty = map((long)(error * 100), -20, 100, MIN_HOLDING_DUTY, currentMaxDuty);
  } else {
    peltierDuty = currentMaxDuty;
  }
  PWM_WRITE(PIN_PELTIER_PWM, CH_PELTIER, peltierDuty);

  // Hot Exhaust Fan
  if (manualHotFan) {
    hotFanDuty = manualHotFanDuty;
  } else {
    if (tempHeatsink < 32.0f) hotFanDuty = 75;
    else if (tempHeatsink > 55.0f) hotFanDuty = 255;
    else hotFanDuty = map((long)(tempHeatsink * 10), 320, 550, 75, 255);
  }
  PWM_WRITE(PIN_HOT_FAN_PWM, CH_HOT_FAN, hotFanDuty);

  // Cold Destratification Fan
  if (coldFanMode == 1) {
    coldFanActive = true;
    coldFanDuty = 200;
  } else if (coldFanMode == 2) {
    coldFanActive = false;
    coldFanDuty = 0;
  } else {
    float gradient = abs(tempTop - tempBottom);
    if (gradient >= DESTRAT_DIFF_TRIGGER) coldFanActive = true;
    else if (gradient <= DESTRAT_DIFF_CLEAR) coldFanActive = false;
    coldFanDuty = coldFanActive ? 180 : 0;
  }
  PWM_WRITE(PIN_COLD_FAN_PWM, CH_COLD_FAN, coldFanDuty);
}

// ==============================================================================
// 8. BLE TELEMETRY TRANSMISSION (JSON NOTIFY)
// ==============================================================================
void sendBleTelemetry() {
  if (!deviceConnected) return;

  char buf[320];
  snprintf(buf, sizeof(buf),
    "{\"chamberTemp\":%.1f,\"topTemp\":%.1f,\"bottomTemp\":%.1f,\"sinkTemp\":%.1f,"
    "\"setpoint\":%.1f,\"preset\":\"%s\",\"presetIndex\":%d,\"vbat\":%.2f,\"batPercent\":%d,"
    "\"charging\":%s,\"peltierDuty\":%d,\"hotFanDuty\":%d,\"coldFanDuty\":%d,"
    "\"coldFanActive\":%s,\"manualHotFan\":%s,\"manualHotDuty\":%d,\"coldFanMode\":%d,"
    "\"turboActive\":%s,\"locked\":%s,\"uptime\":%lu}",
    tempChamberAvg, tempTop, tempBottom, tempHeatsink,
    setpointTemp, PRESETS[currentPreset].name, currentPreset, vbatVoltage, batteryPercent,
    isChargingActive ? "true" : "false", peltierDuty, hotFanDuty, coldFanDuty,
    coldFanActive ? "true" : "false", manualHotFan ? "true" : "false", manualHotFanDuty, coldFanMode,
    turboOverdrive ? "true" : "false", isKeyLocked ? "true" : "false", uptimeSeconds
  );

  pTelemetryChar->setValue((uint8_t*)buf, strlen(buf));
  pTelemetryChar->notify();
}

// ==============================================================================
// 9. LOCAL PHYSICAL BUTTON UI & LOCK
// ==============================================================================
void handlePhysicalButtons() {
  bool btnUp = (digitalRead(PIN_BTN_UP) == LOW);
  bool btnDown = (digitalRead(PIN_BTN_DOWN) == LOW);
  bool btnMode = (digitalRead(PIN_BTN_MODE) == LOW);

  static unsigned long lastDebounceTime = 0;
  static unsigned long modePressStart = 0;
  static bool modeLongPressed = false;

  if (btnMode) {
    if (modePressStart == 0) modePressStart = millis();
    else if (millis() - modePressStart > 2000 && !modeLongPressed) {
      modeLongPressed = true;
      isKeyLocked = !isKeyLocked;
      Serial.print(F("[KEYLOCK] Physical buttons "));
      Serial.println(isKeyLocked ? F("LOCKED") : F("UNLOCKED"));
    }
  } else {
    modePressStart = 0;
    modeLongPressed = false;
  }

  if (isKeyLocked) return;

  if (millis() - lastDebounceTime > 250) {
    if (btnUp) {
      lastDebounceTime = millis();
      setpointTemp = constrain(setpointTemp + 0.5f, 2.0f, 8.0f);
      currentPreset = 3;
    } else if (btnDown) {
      lastDebounceTime = millis();
      setpointTemp = constrain(setpointTemp - 0.5f, 2.0f, 8.0f);
      currentPreset = 3;
    } else if (btnMode && !modeLongPressed && (millis() - modePressStart < 500)) {
      lastDebounceTime = millis();
      currentPreset = (currentPreset + 1) % (NUM_PRESETS - 1);
      setpointTemp = PRESETS[currentPreset].targetTemp;
    }
  }
}

// ==============================================================================
// 10. OLED DISPLAY RENDERING (WITH BLE STATUS)
// ==============================================================================
void updateOLED() {
  display.clearDisplay();

  // Top Bar: Target & Battery & BLE Status
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(PRESETS[currentPreset].name);

  // BLE Connected indicator
  display.setCursor(68, 0);
  if (deviceConnected) display.print(F("[BT]"));
  else display.print(F("    "));

  // Battery / Charging
  display.setCursor(96, 0);
  if (isChargingActive) display.print(F("CHG"));
  else {
    display.print(batteryPercent);
    display.print(F("%"));
  }

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Large Center Temperature
  display.setTextSize(2);
  display.setCursor(14, 18);
  display.print(tempChamberAvg, 1);
  display.setTextSize(1);
  display.print((char)247);
  display.setTextSize(2);
  display.print(F("C"));

  // Target Indicator
  display.setTextSize(1);
  display.setCursor(88, 22);
  display.print(F("->"));
  display.print(setpointTemp, 1);

  display.drawLine(0, 38, 127, 38, SSD1306_WHITE);

  // Bottom Status: Fans, Safety, Turbo
  display.setCursor(0, 42);
  if (turboOverdrive) {
    display.print(F("TURBO 100% OVERDRIVE!"));
  } else if (heatsinkOverheat) {
    display.print(F("ALARM: SINK OVERHEAT!"));
  } else if (antiFreezeTriggered) {
    display.print(F("SAFETY: ANTI-FREEZE!"));
  } else if (lowBatteryShutdown) {
    display.print(F("CRITICAL LOW BATTERY!"));
  } else {
    display.print(F("Sink:"));
    display.print(tempHeatsink, 1);
    display.print(F("C "));
    display.print(F("Pelt:"));
    display.print((int)((peltierDuty / 255.0f) * 100));
    display.print(F("%"));
  }

  // Footer: Lock & Cold Fan
  display.setCursor(0, 54);
  if (isKeyLocked) display.print(F("[LOCKED] "));
  else display.print(F("[UNLOCKED] "));

  display.print(F("CFan:"));
  display.print(coldFanActive ? F("ON") : F("OFF"));

  display.display();
}

