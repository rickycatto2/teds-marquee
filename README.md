# Infinity Sign Systems — LED Relay Controller

> **infinitysignsystems.com | 816.252.3337**

An ESP32-based animation controller for large-format marquee signs using groups of 120VAC bulbs switched by I2C relay boards. Hosts its own WiFi access point with a mobile-friendly web UI — no router, no app, no internet required.

---

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32S (ESP-WROOM-32) |
| Relay boards | 3× PCF8574 I2C 8-channel relay module |
| Bulbs | 120VAC E12 candelabra base (4.5W each) |
| Channels | 24 total (8 per board) |
| Load per channel | Up to 3 bulbs (13.5W) |
| Total load | ~324W at full on |

---

## Wiring

### ESP32 → Relay Boards (I2C, daisy-chained)

```
ESP32 GPIO 21 (SDA) ──── Board 1 SDA ──── Board 2 SDA ──── Board 3 SDA
ESP32 GPIO 22 (SCL) ──── Board 1 SCL ──── Board 2 SCL ──── Board 3 SCL
5V (external supply) ─── Board 1 VCC ──── Board 2 VCC ──── Board 3 VCC
Common GND ──────────── Board 1 GND ──── Board 2 GND ──── Board 3 GND
```

> ⚠️ Power the relay boards from an external 5V/2A+ supply. Do not use the ESP32's 3.3V pin — the relay coils will brown it out.

### I2C Addresses (set via DIP switches A0/A1/A2 on each board)

| Board | Channels | A2 | A1 | A0 | Address |
|-------|----------|----|----|----|---------|
| 1 | Ch 1–8   | OFF | OFF | ON  | 0x25 |
| 2 | Ch 9–16  | OFF | ON  | OFF | 0x26 |
| 3 | Ch 17–24 | OFF | ON  | ON  | 0x27 |

### 120VAC Load Wiring

- **White (neutral):** common wire runs through all sockets — all boards can share one neutral run. 12 AWG handles the full load easily.
- **Black (hot):** each relay's COM terminal connects to the hot feed; NO terminal connects to the bulb group for that channel.
- Each channel switches up to 3 bulbs wired in parallel on the hot leg.

> ⚠️ All 120VAC wiring must be done and insulated **before** powering the system. Never work on the load side while energized.

---

## Sketches

### `relay_show.ino` — Production Animation Controller

The main production sketch. Hosts WiFi AP `RelayShow` (password `12345678`). Open `http://192.168.4.1` in any browser.

**Animations:**

| Mode | Description | Controls |
|------|-------------|----------|
| **Chase** | All channels on; N dark notches travel Ch1→Ch24 in a loop | Speed, Notch Width, Notch Count |
| **Pulse** | All channels off; a window of light travels Ch1→Ch24 in a loop | Speed, Pulse Width |
| **Twinkle** | Random channels sparkle on and off in clusters | Speed, Sparkle Size |

- Speed slider runs 1 (slow) → 10 (fast)
- All settings and the last active mode are saved to flash (NVS) — survives power cycles
- On boot, the sign automatically restarts in the last used mode. First boot defaults to Chase.

---

### `channel_test.ino` — Single Channel Test

Tap any of the 24 channel buttons to light that group exclusively. Tap again to turn it off. Used for verifying individual bulb groups during installation.

AP: `ChanTest` | Password: `12345678` | IP: `192.168.4.1`

---

### `relay_test_24ch.ino` — 24-Channel Relay Test

Tests all three boards together. Individual toggle buttons for all 24 channels plus an auto-scan mode that steps through Ch1→Ch24 sequentially at an adjustable speed.

AP: `RelayTest` | Password: `12345678` | IP: `192.168.4.1`

---

### `i2c_scanner.ino` — I2C Address Scanner

Scans all 127 I2C addresses and prints any found devices to Serial Monitor (115200 baud). Identifies PCF8574 and PCF8574A variants. Run this first when troubleshooting board detection.

---

## Setup

### Arduino IDE

1. Install ESP32 board support:
   - File → Preferences → Additional Boards Manager URLs:
   - `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Tools → Board → **ESP32 Dev Module**
3. No additional libraries required — all dependencies are included in the ESP32 Arduino core

### PlatformIO (VS Code)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
```

No extra `lib_deps` needed.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| I2C scanner finds nothing | Wrong VCC (must be 5V), or SDA/SCL swapped | Check wiring; try swapping SDA/SCL |
| Some relays don't click | Wrong I2C address for that board | Check DIP switch settings; run i2c_scanner |
| All relays energized at boot | PCF8574 floating — sketch not reaching it | Verify I2C address in code matches board DIP |
| Relay clicks but bulb doesn't light | Loose socket wire or bad bulb | Use channel_test.ino to isolate; check socket |
| Animations run but some channels always off | Wiring issue on that relay's load side | Use channel_test.ino; check NO terminal and bulb wiring |
| ESP32 browning out | Relay VCC drawing from ESP32 | Move relay VCC to external 5V supply |

---

## Notes

- The PCF8574 is **active LOW** — the sketch inverts the bitmask before writing, so all animation logic works in positive logic (1 = relay ON = bulb lit).
- Pull-up resistors on SDA/SCL are built into the relay boards. With 3 boards daisy-chained the effective pull-up is ~1.5kΩ, which is within I2C spec at these cable lengths.
- Settings are stored using the ESP32's NVS (non-volatile storage) via the `Preferences` library. To reset to defaults, call `prefs.clear()` in setup once, reflash, then remove it.

---

## License

MIT — free to use, modify, and deploy.
