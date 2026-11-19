/*
 ==============================================================================
  MediCool – DS18B20 1-Wire Sensor Address Discovery Tool
  Project: MediCool (Portable Medicine Cooling System)
  Tinkerers' Lab / Vishwakarma Awards
 ==============================================================================
  HOW TO USE THIS TOOL:
  1. Connect all 3 DS18B20 temperature probes to GPIO 4 (with 4.7k pull-up to 3.3V).
  2. Upload this sketch to your ESP32 and open Serial Monitor at 115200 baud.
  3. This sketch will detect all connected sensors and display their unique 64-bit addresses.
  4. Hold each probe with your fingers one by one. The sensor with rising temperature
     will reveal which physical probe it is (Chamber Top, Chamber Bottom, or Hot Heatsink)!
  5. Copy the 8-byte addresses into the main 'medicool_firmware.ino' sketch.
 ==============================================================================
*/

#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 4 // DS18B20 1-Wire data pin

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int deviceCount = 0;
DeviceAddress tempDeviceAddress;

void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    Serial.print("0x");
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
    if (i < 7) Serial.print(", ");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================================");
  Serial.println("   MediCool – DS18B20 Sensor Address Scanner");
  Serial.println("========================================================");

  sensors.begin();
  deviceCount = sensors.getDeviceCount();

  Serial.print("Found ");
  Serial.print(deviceCount);
  Serial.println(" sensor(s) on the 1-Wire bus.\n");

  if (deviceCount == 0) {
    Serial.println("❌ ERROR: No DS18B20 sensors found!");
    Serial.println("   • Check if the 4.7k pull-up resistor is between GPIO 4 and 3.3V.");
    Serial.println("   • Check VCC (Red) to 3.3V and GND (Black) to Ground.");
    return;
  }

  Serial.println("Sensor Addresses Found:");
  for (int i = 0; i < deviceCount; i++) {
    if (sensors.getAddress(tempDeviceAddress, i)) {
      Serial.print("Sensor [Index ");
      Serial.print(i);
      Serial.print("]: { ");
      printAddress(tempDeviceAddress);
      Serial.println(" }");
    }
  }

  Serial.println("\n--------------------------------------------------------");
  Serial.println("Touch each probe with warm fingers to identify which is which!");
  Serial.println("--------------------------------------------------------\n");
}

void loop() {
  if (deviceCount == 0) {
    delay(2000);
    return;
  }

  sensors.requestTemperatures();
  Serial.println("--- Current Temperature Readings ---");

  for (int i = 0; i < deviceCount; i++) {
    if (sensors.getAddress(tempDeviceAddress, i)) {
      float tempC = sensors.getTempC(tempDeviceAddress);
      Serial.print("Sensor #");
      Serial.print(i);
      Serial.print(" (");
      printAddress(tempDeviceAddress);
      Serial.print("): ");
      Serial.print(tempC, 2);
      Serial.println(" °C");
    }
  }
  Serial.println();
  delay(1500);
}

