# Clef de char 🚗

A DIY remote start and climate preconditioning system for a 2017 Chevrolet Bolt EV,
built around two LILYGO TTGO T3 LoRa32 v1.6 boards communicating over 915MHz LoRa.

## How It Works

A handheld unit (T3 + LiPo battery) communicates via Bluetooth LE to an iPhone web
app (Bluefy browser). Commands are transmitted over LoRa 915MHz to a car unit
installed in the frunk, which controls a spare OEM keyfob via relays to trigger
lock, unlock, and climate preconditioning.

## Features

- 🔒 Remote lock / unlock
- ❄️ Climate preconditioning
- 📍 GPS locate with Google Maps link
- 🔋 Handheld battery percentage
- 🖥️ OLED status display on handheld
- 🔄 Auto-relock after unlock (30 seconds)
- 📡 LoRa range well beyond OEM keyfob
- 📱 iPhone web app via Bluefy browser

## Hardware

| Component | Details |
|---|---|
| Microcontroller | LILYGO TTGO T3 LoRa32 v1.6 × 2 |
| Radio | SX1276 LoRa 915MHz |
| Relay module | 4-channel 5V |
| GPS | NEO-6M with external antenna |
| Keyfob | OEM 2017 Bolt EV (FCC ID: HYQ4AA) |
| Power (car) | 12V aux battery via LM2596 buck converter |
| Power (handheld) | 3.7V LiPo via JST 1.25mm |
| Display | 0.96" SSD1306 OLED (onboard) |

## Repository Structure

├── index.html # Web app (hosted on GitHub Pages)
├── CarUnit/
│ └── CarUnit.ino # Car unit firmware
└── Handheld/
└── Handheld.ino # Handheld unit firmware


## Web App

Hosted at: `https://n21antoine.github.io/car-remote`

Requires the **Bluefy** browser on iPhone for Web Bluetooth support.

## Libraries Required

- LoRa by Sandeep Mistry
- TinyGPSPlus-ESP32 by Mikal Hart
- Adafruit SSD1306
- Adafruit GFX

## Notes

- Keyfob must be programmed to the vehicle before installation
- Car unit installed in frunk next to 12V auxiliary battery
- GPS antenna routed to dashboard for clear sky view
- System uses 915MHz LoRa — North America only
