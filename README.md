# 🚽 Smart Bathroom Flusher (ESP8266)

A touchless, Over-The-Air (OTA) updatable automatic toilet flusher using an ultrasonic sensor and a servo motor.

## ✨ Features
- **Touchless Trigger:** Ultrasonic HC-SR04 detection.
- **Smart Logic:** "Sticky" hold-to-flush logic to prevent accidental triggers.
- **OTA Updates:** Flash new firmware wirelessly via PlatformIO.
- **OLED UI:** High-visibility status display (Size 3 fonts).

## 🛠 Hardware
- NodeMCU (ESP8266)
- HC-SR04 Ultrasonic Sensor
- MG90S Metal Gear Servo
- SSD1306 OLED Display (128x64)

## 🔌 Wiring
- **Trigger:** D7 (GPIO 13)
- **Echo:** D6 (GPIO 12)
- **Servo:** D5 (GPIO 14)
- **OLED:** D2 (SDA), D1 (SCL)

- **Home Assistant Integration:** Automatic MQTT Discovery.
- **Reliability:** Last Will & Testament (LWT) for instant offline/online status.
- **Privacy:** Secrets handled via external header to prevent credential leaks.

## 🚀 How to Upload
1. Clone this repo.
2. Create `include/secrets.h` with your WiFi and MQTT credentials.
3. Open in VS Code with PlatformIO.
4. Build and upload via OTA (Update IP in `platformio.ini`).