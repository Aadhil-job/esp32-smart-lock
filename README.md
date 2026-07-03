# ESP32 Smart Lock

A WiFi-controlled physical lock built entirely on the ESP32 — no router, no internet, no external server. The ESP32 runs its own access point and web server simultaneously. Connect your phone to the ESP32's WiFi network, submit a password through a styled webpage served by the device itself, and a servo physically unlocks a cardboard latch mechanism. Includes attempt-limited lockout with timed cooldown, live OLED status display, and auto-relock after 60 seconds.

Built as project 3 of my self-driven ESP32 learning series, entering second year of ECE.

---

## Demo

[![Watch the Demo on LinkedIn](https://img.shields.io/badge/LinkedIn-Watch%20Demo%20Video-0077B5?style=for-the-badge&logo=linkedin)](https://www.linkedin.com/posts/aadhiljob_ece-esp32-embeddedsystems-ugcPost-7478802511001329665-AMNc/?utm_source=share&utm_medium=member_desktop&rcm=ACoAAER9AjcB82l--O1bjB3Wrr4lOcxL_smk22A)

---

## Features

- ESP32 runs as its own standalone WiFi access point — no home router or internet required
- Serves a styled, responsive login page directly from the ESP32 itself
- Servo physically moves a cardboard latch mechanism on correct password
- Attempts limited to 5 — lockout triggers on fifth wrong attempt
- 10-minute timed cooldown after lockout, with live countdown on both OLED and webpage
- Lockout persists even if someone refreshes the page mid-cooldown (correctly re-calculates remaining time)
- Auto-relocks after 60 seconds, with live closing countdown on OLED
- OLED shows: WiFi credentials on idle, IP address once a device connects, attempt feedback, lockout countdown, unlock countdown
- Slow-sweep servo motion on open for visual effect (stepped movement, not instant snap)
- Dynamic webpage — same template used for all states (idle, wrong password, lockout), with placeholders swapped server-side before sending

---

## Components

| Component | Quantity |
|---|---|
| ESP32 38-pin dev board | 1 |
| SG90 9g micro servo | 1 |
| 1.3" SH1106 OLED display (I2C, 4-pin) | 1 |
| Cardboard + icecream sticks (latch mechanism) | — |
| Breadboard | 1 |
| Jumper wires | Several |

---

## Wiring

### OLED Display → ESP32

| OLED Pin | ESP32 Pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

GPIO 21 and 22 are the ESP32's default hardware I2C pins.

### Servo → ESP32

| Servo Wire | ESP32 Pin |
|---|---|
| Signal (orange/yellow) | GPIO 13 |
| VCC (red) | 5V |
| GND (brown/black) | GND |

> **Note:** Servo is powered from the ESP32's 5V pass-through pin (USB source). This works reliably for a lightweight cardboard latch with minimal friction. For heavier loads, use a separate 5V supply with shared ground to avoid brownout resets.

---

## Libraries Required

Install from Arduino IDE via **Sketch → Include Library → Manage Libraries**:

| Library | Author | Purpose |
|---|---|---|
| U8g2 | oliver | OLED display driver |
| ESP32Servo | Kevin Harrington | Servo control via hardware PWM |
| WebServer | (ESP32 built-in) | HTTP server, route registration, request handling |

WiFi.h and WebServer.h come bundled with the ESP32 board package — no separate install needed.

---

## Setup

### 1. Install ESP32 board package
In Arduino IDE go to **File → Preferences** and add this URL to Additional Board Manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then go to **Tools → Board → Board Manager**, search for ESP32 and install.

### 2. Configure the sketch
Open `Smart_Lock.ino` and update these lines near the top:

```cpp
WiFi.softAP("ESP32", "12345678");       // change AP name and WiFi password
if(server.arg("password") == "12345678") // change lock password
```

The WiFi password (for joining the network) and the lock password (typed into the webpage) are intentionally separate — you can and should make them different values.

### 3. Select board and port
- Go to **Tools → Board** and select **ESP32 Dev Module**
- Go to **Tools → Port** and select your COM port

### 4. Upload and connect
- Upload the sketch
- On your phone, go to WiFi settings and connect to the ESP32's network
- Open a browser and navigate to `192.168.4.1`
- The login page will load — enter the lock password to unlock

---

## How It Works

**Dual role: AP + Web Server**

The ESP32 runs two separate systems simultaneously. `WiFi.softAP()` creates a standalone WiFi access point — your phone connects to this directly, no router involved. Separately, a `WebServer` object listens for HTTP requests on port 80. These are independent layers: the AP handles the network connection, the WebServer handles the actual content. `server.handleClient()` is called every loop pass to check for and process incoming requests non-blocking.

**Route registration and handlers**

Two routes are registered in `setup()`:
- `/` → `form()` — serves the login page
- `/login` → `login()` — handles password submission

When a browser requests a path, `WebServer` automatically calls the matching handler. `form()` and `login()` never call each other — the server dispatches them based on the URL, the same way an event triggers an interrupt handler.

**Dynamic page template**

A single HTML template (`buildPage()`) is reused for all page states. Placeholder strings (`MESSAGE_PLACEHOLDER`, `REMAINING_SECONDS_PLACEHOLDER`, `ICON_PLACEHOLDER`, `HEADING_PLACEHOLDER`, `SUBTITLE_PLACEHOLDER`) are replaced server-side using `String.replace()` before sending, allowing the same styled layout to serve fresh pages, wrong-password responses, and lockout screens without duplicating HTML.

**Password submission over HTTP POST**

The login form submits via POST — the password travels in the HTTP request body rather than the URL, avoiding exposure in browser history or server logs. The ESP32 extracts it using `server.arg("password")`. Note: this is plain HTTP, not HTTPS — the body is still readable by anyone sniffing the WiFi traffic (see Security Model below).

**Lockout and cooldown**

A global `attemptCount` tracks consecutive wrong attempts. On the fifth wrong attempt, `lockoutStart = millis()` captures the lockout timestamp. At the top of every `login()` call and independently in `loop()`, `millis() - lockoutStart >= 600000` checks if 10 minutes have passed — if yes, `attemptCount` resets to `0` automatically, without requiring anyone to submit a password. Refreshing the page mid-lockout correctly re-calculates remaining cooldown time from `lockoutStart`, not from when the page was loaded.

**Auto-relock**

On successful unlock, `unlockTime = millis()` is set and `isUnlocked` is flagged `true`. `loop()` continuously checks `millis() - unlockTime >= 60000` while `isUnlocked` is true — once 60 seconds pass, the servo sweeps back and `isUnlocked` resets to `false`, preventing the check from firing again until the next unlock.

**OLED state machine**

The OLED tracks five distinct states:
1. **Idle** — shows AP name and WiFi password for setup
2. **Connected** — switches to IP address once a device joins the AP
3. **Attempt feedback** — shows "Unlocked!" or "Wrong passcode" on login attempts
4. **Lockout countdown** — ticks down remaining lockout time, once per second via `millis()`
5. **Auto-relock countdown** — shows seconds until door closes after unlock

Station count changes are detected by comparing against a stored `previousStationCount` — the OLED only redraws on actual state changes, not every loop pass.

---

## Security Model

This project intentionally makes security tradeoffs appropriate for a learning build. Worth understanding what those are:

**What it does well:**
- Attempt limiting with timed lockout prevents simple brute-force attacks
- Lockout state is correctly re-enforced even if the user refreshes the page mid-cooldown
- WiFi password and lock password are separated — two independent authentication layers

**Known limitations (by design):**
- **Plaintext HTTP** — the password travels unencrypted between phone and ESP32. Anyone running a packet sniffer on the same WiFi network can read it. A production version would use HTTPS with TLS certificates.
- **WiFi range as attack surface** — unlike a physical keypad (requires standing at the door), this lock's attack surface is WiFi range (~10-30m). An attacker could attempt the password from outside the door without being visible.
- **No persistent lockout across reboots** — `attemptCount` lives in RAM and resets on power loss. Unplugging and replugging the ESP32 resets the failed-attempt counter. Mitigation: keep the ESP32 physically inaccessible (inside the door/enclosure).
- **Single hardcoded password** — no multi-user support, no password change mechanism without re-uploading firmware.

These limitations are documented intentionally, not overlooked — understanding *why* a system is insecure is a prerequisite to knowing how to secure it.

---

## Code Structure

See [`Smart_Lock.ino`](./Smart_Lock.ino) for the full source code.

| Function | Purpose |
|---|---|
| `setup()` | Initializes AP, WebServer, servo, OLED, registers routes |
| `loop()` | Handles client requests, OLED state updates, auto-relock, lockout reset |
| `login()` | Handles POST to `/login` — lockout check, password validation, servo control |
| `form()` | Handles GET to `/` — detects lockout state, serves correct page |
| `buildPage()` | Builds and returns the full HTML page with placeholders replaced |
| `drawIdleScreen()` | OLED: AP name + WiFi password |
| `drawConnectedScreen()` | OLED: IP address after device connects |
| `updateIdleOrConnectedScreen()` | Helper: picks correct idle/connected screen based on station count |

---

## What I Learned

- How the ESP32 can act as its own WiFi access point, router, and HTTP server simultaneously — three separate roles on one chip, two libraries working in parallel
- The difference between being an HTTP *client* (weather station project) and an HTTP *server* (this project) — and what changes structurally when you flip the direction
- How WebServer route registration works: handlers are registered once in `setup()` and dispatched automatically by path match, not called directly — the same mental model as interrupt handlers
- How HTML forms submit data over HTTP POST vs GET, why POST avoids URL exposure, and why neither is inherently encrypted without HTTPS
- How to extract POST form data on the server side using `server.arg()`
- How to build a dynamic HTML response from a template by replacing placeholder strings server-side with `String.replace()`, instead of duplicating HTML for each possible state
- How to implement timed lockout with cooldown using `millis()` — and specifically why the cooldown check needs to run both inside the handler *and* independently in `loop()`, to reset state even without user interaction
- Why a boolean flag (`isUnlocked`) is needed alongside a timestamp (`unlockTime`) for auto-relock — time comparisons stay true forever once they trip, so a separate flag is required to prevent re-triggering
- Servo PWM control via `ESP32Servo` — how angle maps to pulse width, how the servo's internal closed-loop feedback (potentiometer + motor) holds position, and why micro servos can brownout a shared power rail under load
- How to structure OLED state management as a set of distinct named states with change-detection to avoid redrawing identical content every loop pass
- The difference between "fails safe" and "fails open" as a debugging/security instinct — a bug that locks out a legitimate user is better than one that lets anyone in
- That security tradeoffs are explicit design decisions, not things to hide — documenting known limitations shows deeper understanding than pretending they don't exist

---

## Part of a Series

This is project 3 in a self-driven ESP32 learning series:

| # | Project | Key Concepts |
|---|---|---|
| 1 | [Reversing Aid](https://github.com/Aadhil-job/esp32-reversing-aid) | HC-SR04, pulseIn(), tone(), voltage divider |
| 2 | [Weather Station](https://github.com/Aadhil-job/esp32-weather-station) | WiFi, HTTP GET, JSON, I2C, OLED, millis() |
| 3 | Smart Lock (this) | softAP, WebServer, HTTP POST, servo, lockout logic |

---

## Author

**Aadhil M J** — ECE student, second year  
Building through the ESP32 starter kit one project at a time.

> *More projects coming — follow along.*
