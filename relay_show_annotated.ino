/*
 * =============================================================================
 * RELAY SHOW — Production Sketch
 * Infinity Sign Systems
 * infinitysignsystems.com | 816.252.3337
 * =============================================================================
 *
 * WHAT THIS DOES:
 * Controls 24 groups of 120VAC light bulbs on a marquee sign using three
 * I2C relay boards. Hosts its own WiFi access point so you can control
 * animations from any phone or laptop without needing a router.
 *
 * HARDWARE SUMMARY:
 *   - ESP32S dev board (ESP-WROOM-32)
 *   - 3x PCF8574 I2C 8-channel relay boards
 *   - 24 channels total, each switching a group of 120VAC bulbs
 *   - Bulbs: E12 candelabra base, 4.5W each, up to 3 per relay channel
 *
 * FILES REQUIRED (both must be in the same folder named "relay_show"):
 *   - relay_show.ino   <- this file (logic + web server)
 *   - page.h           <- the HTML/CSS/JS web UI as a C string
 *
 * HOW TO OPEN IN ARDUINO IDE:
 *   1. Create folder: Documents/Arduino/relay_show/
 *   2. Put both relay_show.ino and page.h inside it
 *   3. File -> Open -> relay_show.ino
 *   Both files will appear as tabs in the IDE.
 *
 * WIFI:
 *   SSID:     Charlieparker
 *   Password: #########
 *   URL:      http://192.168.4.1
 *   The ESP32 creates its own access point — no router needed.
 *
 * ANIMATIONS:
 *   CHASE   - All 24 channels on. One or more dark "notches" travel
 *             from Ch1 to Ch24 in a loop. Adjustable notch width and count.
 *   PULSE   - All channels off. A single lit window travels Ch1->Ch24.
 *             Adjustable pulse width.
 *   TWINKLE - Channels randomly flicker on and off in clusters.
 *             Adjustable sparkle cluster size.
 *
 * SETTINGS PERSISTENCE:
 *   All settings (mode, speed, widths, counts) are saved to the ESP32's
 *   built-in flash memory (NVS) every time you change them. On power-up
 *   the sign automatically restores and starts the last active mode.
 *   First boot ever defaults to Chase at speed 5.
 *
 * WIRING SUMMARY:
 *   ESP32 GPIO 21 (SDA) -> all relay boards SDA (daisy chained)
 *   ESP32 GPIO 22 (SCL) -> all relay boards SCL (daisy chained)
 *   External 5V supply  -> all relay boards VCC
 *   Common GND          -> all relay boards GND + ESP32 GND
 *
 *   I2C addresses (set by DIP switches A0/A1/A2 on each board):
 *     Board 1: A0=ON  A1=OFF A2=OFF -> 0x25 -> Ch 1-8
 *     Board 2: A0=OFF A1=ON  A2=OFF -> 0x26 -> Ch 9-16
 *     Board 3: A0=ON  A1=ON  A2=OFF -> 0x27 -> Ch 17-24
 *
 * 120VAC LOAD WIRING:
 *   - White (neutral): one common wire can run through all 24 sockets.
 *     12 AWG handles the full load (~324W) with huge headroom.
 *   - Black (hot): each relay COM connects to hot supply.
 *     Each relay NO connects to the bulb group for that channel.
 *   - NEVER work on the load side while the sign is powered.
 *
 * PCF8574 NOTE:
 *   The relay boards are ACTIVE LOW — writing a 0 bit turns a relay ON,
 *   writing a 1 bit turns it OFF. The code tracks relay state in positive
 *   logic (1 = ON) and inverts with ~ before sending to the chip.
 *   See writeAll() below.
 *
 * =============================================================================
 */

// ── Library Includes ──────────────────────────────────────────────────────────
#include <WiFi.h>        // ESP32 WiFi — used to create the access point
#include <WebServer.h>   // ESP32 HTTP server — handles web UI requests
#include <Wire.h>        // I2C communication — talks to the PCF8574 relay boards
#include <Preferences.h> // NVS flash storage — remembers settings across reboots
#include "page.h"        // The web UI HTML/CSS/JS — stored as a C string in flash

// ── Hardware Configuration ────────────────────────────────────────────────────
// I2C pin assignments (these are the ESP32's default I2C pins)
#define SDA_PIN 21
#define SCL_PIN 22

// I2C addresses of the three relay boards.
// These are determined by the A0/A1/A2 DIP switches on each board.
// If a board is not responding, run i2c_scanner.ino to find its actual address.
const uint8_t BOARD_ADDR[3] = { 0x25, 0x26, 0x27 };

// ── WiFi Access Point Configuration ──────────────────────────────────────────
// The ESP32 creates its own WiFi network — no external router needed.
// Connect to this network, then open http://192.168.4.1 in any browser.
const char* AP_SSID = "charlieparker";  // WiFi network name
const char* AP_PASS = "########";   // WiFi password (min 8 chars for WPA2)

// ── Global Objects ────────────────────────────────────────────────────────────
WebServer   server(80);  // HTTP server on port 80 (standard web port)
Preferences prefs;       // NVS flash storage for saving settings

// ── Relay State ───────────────────────────────────────────────────────────────
// One byte per board. Each bit represents one relay channel.
// Bit 0 = relay 1 on that board, bit 7 = relay 8.
// We track state in POSITIVE logic: 1 = relay ON, 0 = relay OFF.
// The PCF8574 chip is active-low so we invert before sending (see writeAll).
uint8_t relayState[3] = {0, 0, 0};

/*
 * writeAll()
 * Sends the current relayState to all three PCF8574 boards over I2C.
 * The ~ operator inverts all bits to convert from our positive logic
 * to the active-low logic the PCF8574 expects.
 * Call this after any change to relayState[] to make it take effect.
 */
void writeAll() {
  for (int b = 0; b < 3; b++) {
    Wire.beginTransmission(BOARD_ADDR[b]);
    Wire.write(~relayState[b]);  // invert: our 1=ON becomes PCF's 0=ON
    Wire.endTransmission();
  }
}

/*
 * setChannel(ch, on)
 * Sets a single channel (0-23) on or off in relayState[].
 * Does NOT send to hardware — call writeAll() after to apply.
 * Channel numbering: 0=Ch1 (Board1 relay1) ... 23=Ch24 (Board3 relay8)
 */
void setChannel(int ch, bool on) {
  if (ch < 0 || ch > 23) return;          // bounds check
  int b   = ch / 8;                        // which board (0, 1, or 2)
  int bit = ch % 8;                        // which relay on that board (0-7)
  if (on) relayState[b] |=  (1 << bit);   // set bit
  else    relayState[b] &= ~(1 << bit);   // clear bit
}

/*
 * getChannel(ch)
 * Returns true if channel ch is currently ON in relayState[].
 * Used to build the JSON status response sent to the web UI.
 */
bool getChannel(int ch) {
  if (ch < 0 || ch > 23) return false;
  return (relayState[ch / 8] >> (ch % 8)) & 1;
}

// Convenience functions to turn all 24 channels on or off at once
void allOn()  { for (int b = 0; b < 3; b++) relayState[b] = 0xFF; writeAll(); }
void allOff() { for (int b = 0; b < 3; b++) relayState[b] = 0x00; writeAll(); }

// ── Animation Mode Enum ───────────────────────────────────────────────────────
// Defines the possible animation states the controller can be in.
// STOPPED means all off and no animation running.
enum Mode { STOPPED, CHASE, PULSE, TWINKLE };
Mode currentMode = STOPPED;

// Forward declaration — tells the compiler that startMode() exists and what
// it takes as an argument, before the full definition appears later in the file.
// Required because some functions above reference startMode before it's defined.
void startMode(Mode m);

// ── Settings ──────────────────────────────────────────────────────────────────
// These are the user-adjustable parameters shown in the web UI.
// All are saved to NVS flash so they survive power cycles.
int speedSetting = 5;   // 1=slowest, 10=fastest (controls step interval ms)
int notchCount   = 3;   // CHASE: how many dark notches travel the strip
int notchWidth   = 2;   // CHASE: how many channels wide each dark notch is
int pulseWidth   = 3;   // PULSE: how many channels wide the lit pulse is
int sparkleSize  = 2;   // TWINKLE: max cluster size for each sparkle event

/*
 * saveSettings()
 * Writes all current settings to NVS (ESP32 flash memory).
 * Called automatically whenever the user changes mode or adjusts a slider.
 * NVS survives power loss — settings are restored on next boot by loadSettings().
 */
void saveSettings() {
  prefs.begin("relayshow", false);  // open NVS namespace "relayshow", read-write
  prefs.putInt("mode",        (int)currentMode);
  prefs.putInt("speed",       speedSetting);
  prefs.putInt("notchCount",  notchCount);
  prefs.putInt("notchWidth",  notchWidth);
  prefs.putInt("pulseWidth",  pulseWidth);
  prefs.putInt("sparkleSize", sparkleSize);
  prefs.end();  // always close when done
}

/*
 * loadSettings()
 * Reads saved settings from NVS flash on boot.
 * The second argument to getInt() is the default value used if the key
 * has never been written (i.e. first boot ever after flashing).
 * Defaults: Chase mode, speed 5, notchCount 3, notchWidth 2, etc.
 */
void loadSettings() {
  prefs.begin("relayshow", true);  // open read-only
  currentMode  = (Mode)prefs.getInt("mode",        (int)CHASE);
  speedSetting =       prefs.getInt("speed",        5);
  notchCount   =       prefs.getInt("notchCount",   3);
  notchWidth   =       prefs.getInt("notchWidth",   2);
  pulseWidth   =       prefs.getInt("pulseWidth",   3);
  sparkleSize  =       prefs.getInt("sparkleSize",  2);
  prefs.end();
}

/*
 * speedToMs(spd)
 * Converts the user-facing speed setting (1-10) to a millisecond interval.
 * Higher speed = smaller interval = faster animation.
 * The lookup table gives a roughly logarithmic feel so the slider
 * doesn't feel too slow at one end and too twitchy at the other.
 *   Speed 1  -> 500ms per step (~2 steps/sec)
 *   Speed 5  -> 130ms per step (~8 steps/sec)
 *   Speed 10 ->  30ms per step (~33 steps/sec)
 */
uint32_t speedToMs(int spd) {
  static const uint32_t table[10] = {500, 350, 250, 180, 130, 95, 70, 55, 42, 30};
  return table[constrain(spd - 1, 0, 9)];
}

// ── CHASE Animation ───────────────────────────────────────────────────────────
// All 24 channels are ON. One or more dark "notches" travel from Ch1 to Ch24
// continuously. The notches are evenly spaced around the 24-channel ring.
// notchCount controls how many notches, notchWidth controls how many
// channels wide each notch is.
int      chasePos    = 0;    // leading edge of the first notch (0-23)
uint32_t chaseLastMs = 0;    // timestamp of last step, for non-blocking timing

/*
 * stepChase()
 * Advances the chase animation by one step.
 * Builds a boolean frame (all true = all on) then punches out
 * the dark notches at evenly-spaced offsets, then renders to hardware.
 */
void stepChase() {
  bool frame[24];
  for (int i = 0; i < 24; i++) frame[i] = true;  // start with all on

  for (int n = 0; n < notchCount; n++) {
    // Space notches evenly around the 24-channel ring
    int offset = (n * 24) / notchCount;
    for (int w = 0; w < notchWidth; w++) {
      // % 24 wraps around so notches loop seamlessly
      frame[(chasePos + offset + w) % 24] = false;
    }
  }

  for (int i = 0; i < 24; i++) setChannel(i, frame[i]);
  writeAll();
  chasePos = (chasePos + 1) % 24;  // advance notch position, wrap at 24
}

// ── PULSE Animation ───────────────────────────────────────────────────────────
// All 24 channels are OFF. A single lit window of pulseWidth channels
// travels from Ch1 (top) to Ch24 (bottom) and then restarts.
// When the pulse travels off the bottom it goes dark briefly before
// reappearing at the top — the off period length = pulseWidth steps.
int      pulsePos    = 0;    // position of the leading edge of the pulse
uint32_t pulseLastMs = 0;    // timestamp of last step

/*
 * stepPulse()
 * Advances the pulse by one channel position.
 * A channel is lit if it falls within [pulsePos, pulsePos+pulseWidth).
 * When pulsePos exceeds 24+pulseWidth the pulse has fully scrolled off
 * the bottom and we reset to 0 (top).
 */
void stepPulse() {
  for (int i = 0; i < 24; i++) {
    int dist = i - pulsePos;
    // Channel is on if it's within the pulse window
    setChannel(i, dist >= 0 && dist < pulseWidth);
  }
  writeAll();
  pulsePos++;
  // Reset once the trailing edge has passed channel 23
  if (pulsePos >= 24 + pulseWidth) pulsePos = 0;
}

// ── TWINKLE Animation ─────────────────────────────────────────────────────────
// Each channel independently flickers on and off at random intervals.
// When a channel's timer fires it flips state and also flips up to
// (sparkleSize) adjacent channels as a cluster, giving a "sparkle" effect.
// Each channel then gets a new random timer so flips stay unsynchronized.
uint32_t twinkleNext[24];  // millis() timestamp when each channel next flips
bool     twinkleLit[24];   // current on/off state for each channel

/*
 * twinkleInit()
 * Randomizes all channels and schedules their first flip times.
 * Called once when switching to TWINKLE mode.
 */
void twinkleInit() {
  uint32_t now = millis();
  for (int i = 0; i < 24; i++) {
    twinkleLit[i]  = random(2);  // random initial state (0 or 1)
    // Stagger initial fire times to avoid all channels flipping at once
    twinkleNext[i] = now + random(speedToMs(speedSetting) * 3);
    setChannel(i, twinkleLit[i]);
  }
  writeAll();
}

/*
 * stepTwinkle()
 * Called every loop iteration (no fixed interval — uses per-channel timers).
 * Checks each channel against its scheduled next-flip time.
 * When a channel fires, it grabs a random cluster of 1-sparkleSize
 * adjacent channels and flips them all to the same new state,
 * then schedules the next flip with a random delay.
 */
void stepTwinkle() {
  uint32_t now = millis();
  uint32_t ms  = speedToMs(speedSetting);
  bool changed = false;

  for (int i = 0; i < 24; i++) {
    if (now >= twinkleNext[i]) {
      bool newState = !twinkleLit[i];              // flip the state
      int  sz = random(1, sparkleSize + 1);        // random cluster size
      for (int j = 0; j < sz && (i + j) < 24; j++) {
        twinkleLit[i + j]  = newState;
        // Schedule next flip: half a step to 3.5 steps from now (random)
        twinkleNext[i + j] = now + ms / 2 + random(ms * 3);
        setChannel(i + j, newState);
      }
      changed = true;
    }
  }
  if (changed) writeAll();  // only write to I2C if something actually changed
}

// ── Mode Switcher ─────────────────────────────────────────────────────────────
/*
 * startMode(m)
 * Switches to a new animation mode. Resets all relevant state,
 * sets up the initial relay output for the new mode, and saves
 * the new mode to NVS so it's remembered after a power cycle.
 *
 * Always call this to change modes — don't set currentMode directly.
 */
void startMode(Mode m) {
  currentMode = m;
  allOff();  // always start from a clean slate

  switch (m) {
    case CHASE:
      allOn();                    // chase starts with everything lit
      chasePos    = 0;            // notch at top
      chaseLastMs = millis();
      break;

    case PULSE:
      pulsePos    = 0;            // pulse starts at top (Ch1)
      pulseLastMs = millis();
      break;

    case TWINKLE:
      twinkleInit();              // randomize all channels
      break;

    case STOPPED:
    default:
      allOff();                   // everything off, no animation
      break;
  }

  saveSettings();  // persist the new mode to flash
}

// ── JSON State Builder ────────────────────────────────────────────────────────
/*
 * stateJson()
 * Builds a JSON string describing the current controller state.
 * Sent to the browser in response to /state GET requests.
 * The web UI polls this every 200ms to update the live LED display
 * and keep sliders in sync.
 *
 * Example response:
 * {"mode":"chase","speed":5,"notchCount":3,"notchWidth":2,
 *  "pulseWidth":3,"sparkleSize":2,
 *  "relays":[1,1,0,1,1,0,1,1,0,1,1,0,1,1,0,1,1,0,1,1,0,1,1,0]}
 */
String stateJson() {
  const char* mstr = "stopped";
  if      (currentMode == CHASE)   mstr = "chase";
  else if (currentMode == PULSE)   mstr = "pulse";
  else if (currentMode == TWINKLE) mstr = "twinkle";

  String j = "{";
  j += "\"mode\":\""      + String(mstr)        + "\",";
  j += "\"speed\":"       + String(speedSetting) + ",";
  j += "\"notchCount\":"  + String(notchCount)   + ",";
  j += "\"notchWidth\":"  + String(notchWidth)   + ",";
  j += "\"pulseWidth\":"  + String(pulseWidth)   + ",";
  j += "\"sparkleSize\":" + String(sparkleSize)  + ",";
  j += "\"relays\":[";
  for (int i = 0; i < 24; i++) {
    j += getChannel(i) ? "1" : "0";
    if (i < 23) j += ",";
  }
  j += "]}";
  return j;
}

// ── HTTP Route Handlers ───────────────────────────────────────────────────────
// These functions are registered with server.on() in setup() and called
// automatically by server.handleClient() in loop() when a matching
// HTTP request comes in from the browser.

/*
 * handleRoot()
 * GET /
 * Serves the web UI page. PAGE_HTML is defined in page.h and stored
 * in flash (PROGMEM) to avoid using up RAM. send_P() reads from flash directly.
 */
void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

/*
 * handleState()
 * GET /state
 * Returns current controller state as JSON.
 * The browser polls this every 200ms to update the live display.
 */
void handleState() {
  server.send(200, "application/json", stateJson());
}

/*
 * handleMode()
 * POST /mode?m=chase|pulse|twinkle|stopped
 * Switches to the requested animation mode.
 * Called by the mode buttons in the web UI.
 */
void handleMode() {
  if (!server.hasArg("m")) { server.send(400, "text/plain", "missing m"); return; }
  String m = server.arg("m");
  if      (m == "chase")   startMode(CHASE);
  else if (m == "pulse")   startMode(PULSE);
  else if (m == "twinkle") startMode(TWINKLE);
  else                     startMode(STOPPED);
  Serial.printf("[Mode] %s\n", m.c_str());
  server.send(200, "application/json", stateJson());
}

/*
 * handleSettings()
 * POST /settings?speed=N&notchWidth=N&notchCount=N&pulseWidth=N&sparkleSize=N
 * Updates one or more settings. All parameters are optional — only the ones
 * present in the request are updated. Called continuously as sliders are dragged.
 * constrain() clamps values to valid ranges so bad input can't break anything.
 */
void handleSettings() {
  if (server.hasArg("speed"))
    speedSetting = constrain(server.arg("speed").toInt(), 1, 10);
  if (server.hasArg("notchWidth"))
    notchWidth   = constrain(server.arg("notchWidth").toInt(), 1, 8);
  if (server.hasArg("notchCount"))
    notchCount   = constrain(server.arg("notchCount").toInt(), 1, 8);
  if (server.hasArg("pulseWidth"))
    pulseWidth   = constrain(server.arg("pulseWidth").toInt(), 1, 12);
  if (server.hasArg("sparkleSize"))
    sparkleSize  = constrain(server.arg("sparkleSize").toInt(), 1, 4);
  saveSettings();  // persist to flash immediately
  server.send(200, "application/json", stateJson());
}

// ── Setup ─────────────────────────────────────────────────────────────────────
/*
 * setup()
 * Runs once on boot. Initializes all hardware and services in order:
 *   1. Serial (for debug output — open Serial Monitor at 115200 baud)
 *   2. I2C + relay boards (verify all three boards are reachable)
 *   3. Load saved settings from NVS
 *   4. Start the last active animation mode
 *   5. Start the WiFi access point
 *   6. Register HTTP routes and start the web server
 */
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Infinity Sign Systems - Relay Show ===");

  // Initialize I2C and verify all three relay boards respond
  Wire.begin(SDA_PIN, SCL_PIN);
  for (int b = 0; b < 3; b++) {
    Wire.beginTransmission(BOARD_ADDR[b]);
    byte err = Wire.endTransmission();
    // err == 0 means the board ACKed — it's there and talking
    Serial.printf("Board %d (0x%02X): %s\n", b+1, BOARD_ADDR[b],
                  err == 0 ? "OK" : "NOT FOUND — check wiring and DIP switches");
  }

  // Seed the random number generator from an unconnected analog pin
  // (floating analog input gives noisy random values — good enough for seeding)
  randomSeed(analogRead(0));

  // Restore saved settings from NVS flash, then start the last used mode
  loadSettings();
  Serial.printf("Restored from flash: mode=%d speed=%d notchCount=%d notchWidth=%d\n",
                (int)currentMode, speedSetting, notchCount, notchWidth);
  startMode(currentMode);

  // Start WiFi access point
  // The ESP32 creates its own network — no external router needed
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("WiFi AP started: SSID=%s  IP=%s\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());

  // Register URL routes with the web server
  // HTTP_GET  = browser requesting a page or data
  // HTTP_POST = browser sending a command (mode change, settings update)
  server.on("/",         HTTP_GET,  handleRoot);      // serve the web UI
  server.on("/state",    HTTP_GET,  handleState);     // return JSON status
  server.on("/mode",     HTTP_POST, handleMode);      // switch animation mode
  server.on("/settings", HTTP_POST, handleSettings);  // update settings

  server.begin();
  Serial.println("Web server started.");
  Serial.println("Connect to WiFi: " + String(AP_SSID) + " / " + String(AP_PASS));
  Serial.println("Then open: http://192.168.4.1");
}

// ── Main Loop ──────────────────────────────────────────────────────────────────
/*
 * loop()
 * Runs continuously after setup(). Does two things every iteration:
 *
 *   1. server.handleClient() — checks for incoming HTTP requests from
 *      the browser and dispatches them to the appropriate handler function.
 *      Must be called frequently or the web UI becomes sluggish.
 *
 *   2. Animation stepping — advances the current animation by one frame
 *      if enough time has elapsed since the last step. Uses non-blocking
 *      timing (millis() comparison) so the web server never gets starved.
 *      TWINKLE manages its own per-channel timing so it runs every loop.
 *
 * Non-blocking timing pattern used for CHASE and PULSE:
 *   if (now - lastStepMs >= stepIntervalMs) { lastStepMs = now; doStep(); }
 * This is equivalent to a delay but doesn't block the CPU.
 */
void loop() {
  server.handleClient();  // process any pending web requests

  uint32_t now = millis();
  uint32_t ms  = speedToMs(speedSetting);  // current step interval

  switch (currentMode) {

    case CHASE:
      // Advance chase one step if interval has elapsed
      if (now - chaseLastMs >= ms) {
        chaseLastMs = now;
        stepChase();
      }
      break;

    case PULSE:
      // Advance pulse one step if interval has elapsed
      if (now - pulseLastMs >= ms) {
        pulseLastMs = now;
        stepPulse();
      }
      break;

    case TWINKLE:
      // Twinkle checks its own per-channel timers every loop iteration
      stepTwinkle();
      break;

    case STOPPED:
    default:
      // Nothing to do — all relays already off from startMode(STOPPED)
      break;
  }
}
