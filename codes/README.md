# MediCool – Firmware & Software Guide (v2.2)

This directory contains the production ESP32 firmware for the **MediCool** portable medicine cooling system.

---

## 1. Directory Structure

```
codes/
├── medicool_firmware.ino       # Master ESP32 control firmware (v2.2 with Presets & Key-Lock)
├── sensor_address_finder.ino   # Utility to scan & map DS18B20 sensor addresses
└── README.md                   # This instruction guide
```

---

## 2. Deep Dive: How the 1-Wire Sensor Bus & Pull-Up Resistor Work

### How do multiple sensors share a single data wire?
The **DS18B20** uses Dallas Semiconductor's proprietary **1-Wire protocol**:
* Every single DS18B20 chip is factory-engraved with an immutable, **unique 64-bit digital serial number (ROM address)**.
* When the ESP32 wants to read the chamber bottom temperature, it sends a command over the wire addressed specifically to that sensor's unique ID: *"Sensor `0x28, 0xAA, ...`, send me your temperature"*.
* Only that specific sensor responds, while the other sensors stay completely silent. This allows 3 (or even 10+) sensors to share the **exact same single data pin (GPIO 4)** without collisions!

### What does the 4.7 kΩ Pull-Up Resistor do?
* Both the ESP32 and the DS18B20 use **"Open-Drain"** bus outputs. This means they can only actively pull the wire down to **Ground (0V)**; neither device can push a strong +3.3V onto the wire.
* The **$4.7\text{ k}\Omega$ pull-up resistor** is connected between **+3.3V** and the **Data wire (GPIO 4)**. It gently "pulls" the wire up to HIGH (+3.3V) whenever neither device is transmitting.
* When a device sends a digital `0`, it pulls the wire to Ground. When it sends a digital `1`, it simply releases the wire, allowing the 4.7kΩ resistor to snap the voltage back up to 3.3V.
* **Without the pull-up resistor**, the data wire floats in electrical noise, and the ESP32 will read 0 devices or return `-127.0°C` errors!

---

## 3. The 3-Button Smart User Interface (Medical Grade)

To prevent accidental pocket touches and make operation simple, the 3 buttons have dedicated commercial features:

```text
       [BTN_UP] (GPIO 18)       [BTN_DOWN] (GPIO 19)       [BTN_MODE] (GPIO 23)
```

### A. Key-Lock / Child-Lock (Accidental Touch Prevention)
* The device **defaults to `[LOCKED]` mode** on power-up.
* While **LOCKED**:
  * Pressing UP or DOWN is blocked to prevent accidental temperature adjustments.
  * Short-pressing **MODE** toggles the screen display between:
    * **Screen 0**: Medical Dashboard (Chamber Temp, Mode, Setpoint, Status)
    * **Screen 1**: Engineering Diagnostics (Heatsink Temp, $\Delta T$ gradient, Fan % PWM)
* **To UNLOCK**:
  * **Press and HOLD the MODE button for 2 seconds**.
  * The top status switches to `[UNLOCK]`.
  * The system will **automatically re-lock after 15 seconds** of inactivity to keep your medicine safe!

---

### B. Medicine Presets (Quick-Select Modes)
When the device is **UNLOCKED**, short-pressing the **MODE** button cycles through standard medical presets:

| Preset Name | Target Setpoint | Best Used For |
| :--- | :---: | :--- |
| **INSULIN** | **$4.0^\circ\text{C}$** | Insulin pens, cartridges, and vials ($2^\circ\text{C}\text{–}8^\circ\text{C}$ storage). |
| **VACCINE** | **$5.0^\circ\text{C}$** | Standard cold-chain vaccines and biologics. |
| **EYE DROP** | **$6.0^\circ\text{C}$** | Monoclonal antibodies, glaucoma drops, interferons. |
| **CUSTOM** | **User Set ($2.0\text{–}8.0^\circ\text{C}$)** | Manual adjustment using the UP and DOWN buttons in steps of $0.5^\circ\text{C}$. |

When in **CUSTOM** mode:
* Press **UP (GPIO 18)** to increase target temperature by $+0.5^\circ\text{C}$.
* Press **DOWN (GPIO 19)** to decrease target temperature by $-0.5^\circ\text{C}$.
* Settings are automatically saved to ESP32 non-volatile flash memory (`Preferences`), so your chosen temperature is remembered even if the battery dies or power is unplugged!

---

## 4. Required Arduino IDE Libraries

Install these 4 libraries from the **Arduino IDE Library Manager** (`Ctrl + Shift + I`):
1. **DallasTemperature** (by Miles Burton)
2. **OneWire** (by Paul Stoffregen)
3. **Adafruit SSD1306** (by Adafruit)
4. **Adafruit GFX Library** (by Adafruit)

---

## 5. Pinout Map

| Hardware Subsystem | ESP32 GPIO Pin | Mode / Description |
| :--- | :---: | :--- |
| **Peltier PWM Output** | **GPIO 25** | LEDC PWM @ **30 kHz** (driven via IRLZ44N Q1 through 150Ω) |
| **Hot Blower Fan (7530)** | **GPIO 26** | LEDC PWM @ **25 kHz** (driven via IRLZ44N Q2) |
| **Cold Blower Fan (5010)**| **GPIO 27** | LEDC PWM @ **25 kHz** (driven via IRLZ44N Q3) |
| **DS18B20 1-Wire Bus** | **GPIO 4** | Digital bidirectional (requires 4.7kΩ pull-up to 3.3V) |
| **OLED Display SDA** | **GPIO 21** | I2C Data line |
| **OLED Display SCL** | **GPIO 22** | I2C Clock line |
| **Pushbutton 1 (Temp Up)** | **GPIO 18** | `INPUT_PULLUP` (Active-LOW, connects to GND when pressed) |
| **Pushbutton 2 (Temp Down)**| **GPIO 19** | `INPUT_PULLUP` (Active-LOW, connects to GND when pressed) |
| **Pushbutton 3 (Mode / Lock)**| **GPIO 23** | `INPUT_PULLUP` (Short-press = Mode/Screen; Long-press 2s = Lock/Unlock) |
