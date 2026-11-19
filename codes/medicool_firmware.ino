/*
 ==============================================================================
  MediCool – Master Firmware (ESP32) v2.4
  Project: MediCool (Portable Medicine Cooling System)
  Tinkerers' Lab / Vishwakarma Awards
  Updated: November 2026
 ==============================================================================
  Hardware & Sensing Features:
  • DUAL VOLTAGE DIVIDERS:
      - Divider 1 (GPIO 35): Battery (+) 100k/22k divider -> Reads true Battery % (0%-100%).
      - Divider 2 (GPIO 34): 20V PD 100k/15k divider -> Instant Hardware Charger Detection.
  • DYNAMIC POWER RELEASE (DPM):
      - If Charging & Battery < 90%: Peltier capped at 45% (~25W) to stay under 90W budget.
      - If Battery >= 90% (Full): Charger power drops to ~1W, freeing up full 75% cooling power!
      - If on Battery Backup: Full dynamic range available, with low-battery auto-cutoff.
  • INVERTER THERMAL GOVERNOR:
      - Variable-power 30 kHz PWM via LC filter (Boost -> Approach -> 20% Steady Holding Floor).
  • 1-Wire Digital Bus: 3x DS18B20 sensors (Top, Bottom, Heatsink) on single GPIO 4.
  • Dynamic Destratification Cold-Fan Control (turns OFF when mixed to save battery).
  • Proportional Hot-Fan Speed Control based on heatsink temperature.
  • Commercial-grade 3-Button UI with Key-Lock, Medicine Presets, and OLED Dashboard.
 ==============================================================================
*/

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
#define PIN_HOT_FAN_PWM    26  // MOSFET Q2 Gate (7530 Hot Blower)
#define PIN_COLD_FAN_PWM   27  // MOSFET Q3 Gate (5010 Cold Blower)
#define PIN_ONEWIRE_BUS     4  // 3x DS18B20 data line (4.7k pull-up to 3.3V)
#define PIN_OLED_SDA       21  // OLED Display I2C SDA
#define PIN_OLED_SCL       22  // OLED Display I2C SCL
#define PIN_BTN_UP         18  // Pushbutton 1: Temp Up (Active-LOW to GND)
#define PIN_BTN_DOWN       19  // Pushbutton 2: Temp Down (Active-LOW to GND)
#define PIN_BTN_MODE       23  // Pushbutton 3: Screen / Mode / Lock (Active-LOW to GND)

// Dual Voltage Dividers (ADC Pins)
#define PIN_VIN_SENSE      34  // Divider 2: 20V PD line (100k / 15k)
#define PIN_VBAT_SENSE     35  // Divider 1: Battery (+) (100k / 22k)

// PWM Channels & Frequencies (ESP32 LEDC)
#define PELTIER_PWM_FREQ   30000 // 30 kHz (Silent & optimal for 47uH inductor)
#define FAN_PWM_FREQ       25000 // 25 kHz (Silent fan operation)
#define PWM_RESOLUTION         8 // 8-bit (0 - 255)

#define CH_PELTIER             0
#define CH_HOT_FAN             1
#define CH_COLD_FAN            2

// OLED Configuration
#define SCREEN_WIDTH         128
#define SCREEN_HEIGHT         64
#define OLED_RESET            -1
#define SCREEN_ADDRESS      0x3C

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

// DYNAMIC POWER LIMITS (Respects 90W Adapter)
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

// Temperatures (°C)
float tempTop       = 25.0f;
float tempBottom    = 25.0f;
float tempHeatsink  = 25.0f;
float tempChamber   = 25.0f;
float setpointTemp  = 4.0f;
float customTemp    = 4.0f;
MedicinePreset activePreset = PRESET_INSULIN;

// Electrical & Power Measurements
float inputVoltage   = 0.0f; // Sensed 20V rail
float batteryVoltage = 12.0f;// Sensed Battery rail
int   batteryPercent = 100;  // 0% - 100%
bool  isExternalPower = false;
bool  isBatteryFull   = false;

// Actuator States (0 - 255)
uint8_t peltierDuty = 0;
uint8_t hotFanDuty  = 0;
uint8_t coldFanDuty = 0;

// System Flags
bool peltierActive    = false;
bool overheatAlarm    = false;
bool antifreezeActive = false;
bool sensorError      = false;
bool lowBatteryAlarm  = false;
bool destratActive    = false;

// Lock & UI Navigation State
bool isLocked           = true;
unsigned long unlockTime = 0;
const unsigned long AUTO_RELOCK_TIME = 15000;
const unsigned long LONG_PRESS_TIME  = 1800;

uint8_t currentScreen = 0; // 0 = Medical Dashboard, 1 = Diagnostics
unsigned long lastTempReadTime = 0;
unsigned long lastSerialTime   = 0;
unsigned long peltierOffTime   = 0;
const unsigned long TEMP_READ_INTERVAL = 1000;
const unsigned long POST_COOL_DURATION = 60000;

// Button Timing
int lastModeButtonState = HIGH;
unsigned long modePressStartTime = 0;
bool modeLongPressHandled = false;

unsigned long lastBtnUpTime   = 0;
unsigned long lastBtnDownTime = 0;
const unsigned long DEBOUNCE_DELAY = 180;

// ==============================================================================
// 4. HARDWARE PWM FUNCTIONS
// ==============================================================================
void setPWM(uint8_t channel, uint8_t pin, uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  ledcWrite(channel, duty);
#endif
}

void initPWM() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_PELTIER_PWM,  PELTIER_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(PIN_HOT_FAN_PWM,  FAN_PWM_FREQ,     PWM_RESOLUTION);
  ledcAttach(PIN_COLD_FAN_PWM, FAN_PWM_FREQ,     PWM_RESOLUTION);
#else
  ledcSetup(CH_PELTIER,  PELTIER_PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PIN_PELTIER_PWM,  CH_PELTIER);

  ledcSetup(CH_HOT_FAN,  FAN_PWM_FREQ,     PWM_RESOLUTION);
  ledcAttachPin(PIN_HOT_FAN_PWM,  CH_HOT_FAN);

  ledcSetup(CH_COLD_FAN, FAN_PWM_FREQ,     PWM_RESOLUTION);
  ledcAttachPin(PIN_COLD_FAN_PWM, CH_COLD_FAN);
#endif

  setPWM(CH_PELTIER,  PIN_PELTIER_PWM,  0);
  setPWM(CH_HOT_FAN,  PIN_HOT_FAN_PWM,  0);
  setPWM(CH_COLD_FAN, PIN_COLD_FAN_PWM, 0);
}

// ==============================================================================
// 5. DUAL VOLTAGE DIVIDERS & TEMPERATURE SENSING
// ==============================================================================
void readPowerSubsystem() {
  // Divider 2 (20V Line): R1 = 100k, R2 = 15k -> Factor = (100+15)/15 = 7.666
  int rawVin = analogRead(PIN_VIN_SENSE);
  inputVoltage = (rawVin / 4095.0f) * 3.3f * 7.666f;
  isExternalPower = (inputVoltage >= 15.0f); // True if 20V PD is connected

  // Divider 1 (Battery Line): R1 = 100k, R2 = 22k -> Factor = (100+22)/22 = 5.545
  int rawVbat = analogRead(PIN_VBAT_SENSE);
  batteryVoltage = (rawVbat / 4095.0f) * 3.3f * 5.545f;

  // Calculate Battery Percentage (10.0V = 0%, 12.6V = 100%)
  if (batteryVoltage <= 10.0f) {
    batteryPercent = 0;
  } else if (batteryVoltage >= 12.55f) {
    batteryPercent = 100;
  } else {
    batteryPercent = (int)(((batteryVoltage - 10.0f) / 2.55f) * 100.0f);
  }
  batteryPercent = constrain(batteryPercent, 0, 100);

  // Check if Battery is Full
  isBatteryFull = (batteryVoltage >= 12.50f);

  // Low Battery Alarm (on battery backup)
  if (!isExternalPower && batteryVoltage <= 10.4f) {
    lowBatteryAlarm = true;
  } else {
    lowBatteryAlarm = false;
  }
}

void readSensors() {
  sensors.requestTemperatures();
  
  float t0 = sensors.getTempCByIndex(0);
  float t1 = sensors.getTempCByIndex(1);
  float t2 = sensors.getTempCByIndex(2);

  if (t0 <= -100.0f || t0 >= 84.0f) {
    sensorError = true;
  } else {
    sensorError = false;
  }

  tempTop      = (t0 > -100.0f) ? t0 : 25.0f;
  tempBottom   = (t1 > -100.0f) ? t1 : tempTop;
  tempHeatsink = (t2 > -100.0f) ? t2 : 30.0f;

  tempChamber  = (tempTop + tempBottom) / 2.0f;
  
  readPowerSubsystem();
}

// ==============================================================================
// 6. TRUE DYNAMIC PELTIER GOVERNOR & CLOSED-LOOP CONTROL
// ==============================================================================
void updateThermalControl() {
  // A. Safety Checks
  if (tempHeatsink >= HEATSINK_OVERHEAT_TEMP) {
    overheatAlarm = true;
  } else if (tempHeatsink <= HEATSINK_RECOVERY_TEMP) {
    overheatAlarm = false;
  }

  if (tempBottom <= ANTI_FREEZE_LIMIT) {
    antifreezeActive = true;
  } else if (tempBottom >= (ANTI_FREEZE_LIMIT + 1.0f)) {
    antifreezeActive = false;
  }

  // Emergency Cutoffs
  if (overheatAlarm || antifreezeActive || sensorError || (!isExternalPower && batteryVoltage < BATTERY_CUTOFF_VOLT)) {
    peltierActive = false;
    peltierDuty   = 0;
    if (overheatAlarm) hotFanDuty = 255;
  } else {
    // -----------------------------------------------------------
    // B. Dynamic Power Limit (Smart Power Budgeting)
    // -----------------------------------------------------------
    uint8_t currentMaxDuty = MAX_DUTY_NORMAL; // Default 75% (~40W)

    // If external power is plugged in AND battery is still charging,
    // clamp to 47% (~25W) so Cooler + Charger stays safely under 65W!
    if (isExternalPower && !isBatteryFull) {
      currentMaxDuty = MAX_DUTY_CHARGING; // 47% Power
    } else {
      // Battery is full OR on battery backup: release full 75% power!
      currentMaxDuty = MAX_DUTY_NORMAL;
    }

    // -----------------------------------------------------------
    // C. Multi-Zone Inverter Governor
    // -----------------------------------------------------------
    float error = tempChamber - setpointTemp;

    if (error > 3.0f) {
      // Zone 1: Fast Boost Pull-down
      peltierActive = true;
      peltierDuty   = currentMaxDuty; 
    } 
    else if (error > 1.0f && error <= 3.0f) {
      // Zone 2: Medium Inverter Pull-Down
      peltierActive = true;
      float scale = (error - 1.0f) / 2.0f;
      peltierDuty = (uint8_t)(128 + scale * (currentMaxDuty - 128));
    } 
    else if (error > 0.2f && error <= 1.0f) {
      // Zone 3: Gentle Approach
      peltierActive = true;
      float scale = (error - 0.2f) / 0.8f;
      peltierDuty = (uint8_t)(MIN_HOLDING_DUTY + scale * (128 - MIN_HOLDING_DUTY));
    } 
    else if (error >= -0.2f && error <= 0.2f) {
      // Zone 4: Steady-State Holding Floor (~20% duty, ~12W)
      peltierActive = true;
      peltierDuty   = MIN_HOLDING_DUTY; 
    } 
    else {
      // Zone 5: Overcooled -> Shut OFF
      if (peltierActive) peltierOffTime = millis();
      peltierActive = false;
      peltierDuty   = 0;
    }
  }

  // D. Proportional Hot-Side Fan
  if (!overheatAlarm) {
    if (peltierActive) {
      if (tempHeatsink < 33.0f) {
        hotFanDuty = 90; // ~35% quiet
      } else if (tempHeatsink >= 52.0f) {
        hotFanDuty = 255; // 100%
      } else {
        hotFanDuty = map((long)(tempHeatsink * 10), 330, 520, 90, 255);
      }
    } else {
      if (millis() - peltierOffTime < POST_COOL_DURATION && tempHeatsink > 34.0f) {
        hotFanDuty = 110; // Post-cooling
      } else {
        hotFanDuty = 0;
      }
    }
  }

  // E. Cold-Side Fan (Dynamic Destratification)
  float deltaT = abs(tempTop - tempBottom);
  if (peltierActive && peltierDuty > MIN_HOLDING_DUTY) {
    coldFanDuty   = 165; // ~65% speed during active cooling
    destratActive = false;
  } else {
    if (deltaT >= DESTRAT_DIFF_TRIGGER) {
      destratActive = true;
    } else if (deltaT <= DESTRAT_DIFF_CLEAR) {
      destratActive = false;
    }
    coldFanDuty = destratActive ? 90 : 0;
  }

  // Apply to MOSFETs
  setPWM(CH_PELTIER,  PIN_PELTIER_PWM,  peltierDuty);
  setPWM(CH_HOT_FAN,  PIN_HOT_FAN_PWM,  hotFanDuty);
  setPWM(CH_COLD_FAN, PIN_COLD_FAN_PWM, coldFanDuty);
}

// ==============================================================================
// 7. KEY-LOCK & BUTTON CONTROL LOGIC
// ==============================================================================
void handleButtons() {
  unsigned long now = millis();
  int currentModeBtn = digitalRead(PIN_BTN_MODE);

  // MODE BUTTON: Short vs Long Press
  if (currentModeBtn == LOW && lastModeButtonState == HIGH) {
    modePressStartTime = now;
    modeLongPressHandled = false;
  } else if (currentModeBtn == LOW && lastModeButtonState == LOW) {
    if (!modeLongPressHandled && (now - modePressStartTime >= LONG_PRESS_TIME)) {
      isLocked = !isLocked;
      modeLongPressHandled = true;
      unlockTime = now;
      Serial.printf(">>> Key Lock Toggled! State: %s\n", isLocked ? "LOCKED" : "UNLOCKED");
    }
  } else if (currentModeBtn == HIGH && lastModeButtonState == LOW) {
    if (!modeLongPressHandled) {
      if (isLocked) {
        currentScreen = (currentScreen + 1) % 2; // Screen switch
      } else {
        unlockTime = now;
        activePreset = (MedicinePreset)((activePreset + 1) % 4);
        setpointTemp = (activePreset == PRESET_CUSTOM) ? customTemp : PRESET_TEMPS[activePreset];

        prefs.putInt("preset", (int)activePreset);
        prefs.putFloat("setpoint", setpointTemp);
        Serial.printf(">>> Preset Changed: %s (%.1f °C)\n", PRESET_NAMES[activePreset], setpointTemp);
      }
    }
  }
  lastModeButtonState = currentModeBtn;

  if (!isLocked && (now - unlockTime >= AUTO_RELOCK_TIME)) {
    isLocked = true;
  }

  // UP & DOWN BUTTONS
  if (!isLocked && activePreset == PRESET_CUSTOM) {
    if (digitalRead(PIN_BTN_UP) == LOW && (now - lastBtnUpTime > DEBOUNCE_DELAY)) {
      lastBtnUpTime = now;
      unlockTime = now;
      if (customTemp < MAX_SETPOINT_TEMP) {
        customTemp += 0.5f;
        setpointTemp = customTemp;
        prefs.putFloat("customTemp", customTemp);
        prefs.putFloat("setpoint", setpointTemp);
      }
    }
    if (digitalRead(PIN_BTN_DOWN) == LOW && (now - lastBtnDownTime > DEBOUNCE_DELAY)) {
      lastBtnDownTime = now;
      unlockTime = now;
      if (customTemp > MIN_SETPOINT_TEMP) {
        customTemp -= 0.5f;
        setpointTemp = customTemp;
        prefs.putFloat("customTemp", customTemp);
        prefs.putFloat("setpoint", setpointTemp);
      }
    }
  }
}

// ==============================================================================
// 8. OLED DISPLAY UI DASHBOARD (COMPLETE DETAILS)
// ==============================================================================
void drawUI() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // -------------------------------------------------------------
  // TOP STATUS BAR (Present on both screens)
  // -------------------------------------------------------------
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("MediCool");

  // Power Icon: [CHG 100%] or [BAT 75%]
  display.setCursor(54, 0);
  if (isExternalPower) {
    if (isBatteryFull) {
      display.print("CHG:FULL");
    } else {
      display.printf("CHG:%d%%", batteryPercent);
    }
  } else {
    display.printf("BAT:%d%%", batteryPercent);
  }

  // Lock Icon: [L] or [U]
  display.setCursor(110, 0);
  display.print(isLocked ? "[LCK]" : "[UNL]");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // -------------------------------------------------------------
  // SCREEN 0: Primary Medical Dashboard
  // -------------------------------------------------------------
  if (currentScreen == 0) {
    // Current Chamber Average Temperature (Large Font)
    display.setTextSize(2);
    display.setCursor(2, 13);
    display.printf("%.1f C", tempChamber);

    // Active Medicine Preset Name
    display.setTextSize(1);
    display.setCursor(76, 13);
    display.print("MODE:");
    display.setCursor(76, 23);
    display.printf("%s", PRESET_NAMES[activePreset]);

    // Target Setpoint Temperature
    display.setCursor(76, 33);
    display.printf("Set:%.1fC", setpointTemp);

    // Chamber Gradient: Top & Bottom Probes
    display.setCursor(2, 36);
    display.printf("T:%.1fC B:%.1fC", tempTop, tempBottom);

    // Bottom Action / Status Bar
    display.drawLine(0, 47, 127, 47, SSD1306_WHITE);
    display.setCursor(2, 52);

    if (overheatAlarm) {
      display.print("!HOT HEATSINK ERR!");
    } else if (antifreezeActive) {
      display.print("!FROST PROTECTION!");
    } else if (lowBatteryAlarm) {
      display.print("!LOW BATTERY WARN!");
    } else if (sensorError) {
      display.print("!SENSOR WIRE ERR!");
    } else if (!isLocked && activePreset == PRESET_CUSTOM) {
      display.print("ADJUST: Use UP/DOWN");
    } else if (!isLocked) {
      display.print("MODE: Change Preset");
    } else {
      // Normal operating readout: TEC% and Governor State
      display.printf("TEC:%d%% %s", 
                     (peltierDuty * 100) / 255, 
                     peltierDuty > MAX_DUTY_CHARGING ? "BOOST" : (peltierDuty > 0 ? "HOLD" : "IDLE"));
    }
  }
  // -------------------------------------------------------------
  // SCREEN 1: Detailed Engineering Diagnostics
  // -------------------------------------------------------------
  else if (currentScreen == 1) {
    display.setTextSize(1);
    
    // Line 1: Heatsink Temp & Delta T
    display.setCursor(0, 13);
    display.printf("HSink: %.1fC  dT:%.1fC", tempHeatsink, abs(tempTop - tempBottom));

    // Line 2: Fan Speeds
    display.setCursor(0, 24);
    display.printf("HotFan:%d%% ColdFan:%d%%", 
                   (hotFanDuty * 100) / 255, 
                   (coldFanDuty * 100) / 255);

    // Line 3: Exact Voltages
    display.setCursor(0, 35);
    display.printf("Vin:%.1fV  Vbat:%.2fV", inputVoltage, batteryVoltage);

    // Line 4: Power Budget Status
    display.setCursor(0, 46);
    if (isExternalPower) {
      display.printf("Pwr: 20V Ext (%s)", isBatteryFull ? "FULL PWR" : "45% CAP");
    } else {
      display.printf("Pwr: 11.1V Batt (%d%%)", batteryPercent);
    }

    // Line 5: Navigation hint
    display.setCursor(0, 56);
    display.print("MODE: Next (Hold Lock)");
  }

  display.display();
}

// ==============================================================================
// 9. SERIAL TELEMETRY & LOGGING
// ==============================================================================
void logTelemetry() {
  Serial.printf("[TELEMETRY] Chamber: %.2f C | Set: %.1f C | Vbat: %.2fV (%d%%) | Vin: %.1fV | Mode: %s | TEC: %d%% (%dW est) | HotFan: %d%% | ColdFan: %d%% | %s | %s\n",
                tempChamber, setpointTemp,
                batteryVoltage, batteryPercent,
                inputVoltage,
                PRESET_NAMES[activePreset],
                (peltierDuty * 100) / 255,
                (int)((peltierDuty / 255.0f) * 55.0f),
                (hotFanDuty * 100) / 255,
                (coldFanDuty * 100) / 255,
                isExternalPower ? (isBatteryFull ? "20V PD (FULL)" : "20V PD (CHARGING)") : "BATTERY (11.1V)",
                isLocked ? "LOCKED" : "UNLOCKED");
}

// ==============================================================================
// 10. SETUP & MAIN LOOP
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n========================================================");
  Serial.println("   MediCool v2.4 – Dual Voltage Sensing & Inverter");
  Serial.println("========================================================");

  prefs.begin("medicool", false);
  activePreset = (MedicinePreset)prefs.getInt("preset", (int)PRESET_INSULIN);
  customTemp   = prefs.getFloat("customTemp", 4.0f);
  setpointTemp = (activePreset == PRESET_CUSTOM) ? customTemp : PRESET_TEMPS[activePreset];

  pinMode(PIN_BTN_UP,    INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN,  INPUT_PULLUP);
  pinMode(PIN_BTN_MODE,  INPUT_PULLUP);
  pinMode(PIN_VIN_SENSE, INPUT);
  pinMode(PIN_VBAT_SENSE, INPUT);

  initPWM();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("❌ Warning: SSD1306 OLED allocation failed.");
  } else {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(16, 18);
    display.print("MediCool");
    display.setTextSize(1);
    display.setCursor(20, 42);
    display.print("Dual-Sense v2.4");
    display.display();
    delay(1200);
  }

  sensors.begin();
  readSensors();
  Serial.println("System Initialized. Dual Power Sensing Active.\n");
}

void loop() {
  unsigned long currentMillis = millis();

  handleButtons();

  if (currentMillis - lastTempReadTime >= TEMP_READ_INTERVAL) {
    lastTempReadTime = currentMillis;
    readSensors();
    updateThermalControl();
    drawUI();
  }

  if (currentMillis - lastSerialTime >= 2000) {
    lastSerialTime = currentMillis;
    logTelemetry();
  }
}
