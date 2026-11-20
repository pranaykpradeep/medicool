# MediCool Mobile Companion App (Flutter v2.0)

This is the standalone native Flutter mobile app for **MediCool**. It implements all 5 tabs shown in our Figma design system:
1. **Home:** Real-time Chamber gauge, WHO compliance badge, battery %, power source.
2. **Fans:** Advanced manual Hot Blower Fan slider (0%–100%) and Cold Blower Destratification modes.
3. **Turbo:** Super Advanced 100% Peltier Overdrive (~55W pull-down) with automatic thermal guardrails.
4. **Modes:** Precision setpoint steppers, pharmaceutical presets, and remote key-lock.
5. **Audit:** 24-hour rolling temperature line chart and PDF compliance report export.

---

## How to Run & Build the APK

### Prerequisites:
* Flutter SDK (3.x or newer) installed.
* Android Studio (with Android SDK) or VS Code with Flutter extension.

### Step 1: Connect Your Phone
Enable **Developer Options** and **USB Debugging** on your Android smartphone, and plug it into your computer via USB.

### Step 2: Run in Debug Mode
Open terminal inside `codes/version2.0/mobile_app/`:
```bash
flutter pub get
flutter run
```
The app will build and install directly onto your connected phone!

### Step 3: Build a Release APK (For Sharing or Presentation)
To generate an independent `.apk` file that you can transfer to any phone:
```bash
flutter build apk --release
```
The compiled APK will be created at:
`build/app/outputs/flutter-apk/app-release.apk`

---

## How the App Connects to the Hardware
1. Turn on the MediCool cooler (running `codes/version2.0/medicool_v2_firmware.ino`).
2. Connect your phone's Wi-Fi to **`MediCool-Smart`** (Password: `medicool123`).
3. Open the **MediCool Companion App** on your phone.
4. The app automatically communicates with the ESP32 at `http://192.168.4.1` via JSON REST APIs.

