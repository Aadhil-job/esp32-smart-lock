// ============================================================
// ESP32 Smart Lock
// Runs as a standalone WiFi AP + HTTP server.
// Phone connects to the ESP32's network, submits a password
// via a styled webpage, servo physically unlocks a latch.
// Includes lockout/cooldown, auto-relock, and OLED status.
// ============================================================

#include <WebServer.h>
#include <ESP32Servo.h>
#include <U8g2lib.h>
#include <WiFi.h>

// --- Hardware objects ---
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE); // OLED via hardware I2C (SDA=21, SCL=22)
WebServer server(80);  // HTTP server on standard port 80
Servo myServo;         // Servo object — uses ESP32Servo library for hardware PWM

// --- Security state ---
int attemptCount = 0;           // tracks consecutive wrong password attempts
unsigned long lockoutStart = 0; // millis() timestamp of when lockout was triggered

// --- OLED state management ---
int previousStationCount = 0;       // last known number of connected devices, for change detection
unsigned long previousTimerUpdate;  // last time OLED timer was redrawn (once-per-second throttle)

// --- Auto-relock state ---
unsigned long unlockTime; // millis() timestamp of when door was last unlocked
bool isUnlocked;          // true while door is open, false once servo has closed

// Forward declaration — drawIdleScreen() is called in setup() but defined later
void drawIdleScreen();

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Servo — attach to GPIO 13, start at closed position (0°)
  myServo.attach(13);
  myServo.write(0);

  // Start WiFi access point — ESP32 broadcasts its own network
  // WiFi password (for joining the network) and lock password (typed into the webpage) are separate
  WiFi.softAP("ESP32", "12345678");

  // Start HTTP server and register route handlers
  // server.on() stores the function reference — the server calls it automatically when that path is requested
  server.begin();
  server.on("/", form);       // GET / → serve the login page
  server.on("/login", login); // POST /login → handle password submission

  // OLED init
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  drawIdleScreen(); // show WiFi credentials immediately — before anyone connects

  Serial.println(WiFi.softAPIP()); // print actual AP IP for debugging (should be 192.168.4.1)
}

// ============================================================
// LOGIN HANDLER — called automatically when browser POSTs to /login
// ============================================================
void login() {
  int remainingSeconds;
  String page;

  // --- Lockout check (runs before password check) ---
  if (attemptCount >= 5) {
    if (millis() - lockoutStart >= 600000) {
      // 10 minutes have passed — reset lockout, let this attempt through
      attemptCount = 0;
    } else {
      // Still locked out — calculate remaining cooldown and send lockout page
      remainingSeconds = (600000 - (millis() - lockoutStart)) / 1000;
      page = buildPage("", remainingSeconds, false);
      server.send(200, "text/html", page);
      return; // stop here — don't process the password
    }
  }

  // --- Password check ---
  if (server.arg("password") == "12345678") {
    // Correct password — start auto-relock timer and flag door as open
    unlockTime = millis();
    isUnlocked = true;

    // Send success page to browser
    page = buildPage("", 0, true);
    server.send(200, "text/html", page);

    // Sweep servo open gradually — stepped movement for visual effect
    // delay() is acceptable here since this only blocks for ~1.2s on a confirmed successful unlock
    for (int i = 0; i <= 80; i++) {
      myServo.write(i);
      delay(15);
    }

    // OLED: show unlock confirmation
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(16, 30, "Unlocked!");
    u8g2.drawHLine(0, 38, 124);
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(16, 54, "Welcome back");
    u8g2.sendBuffer();

  } else {
    // Wrong password — increment attempt counter
    attemptCount++;

    // If this increment just hit 5, capture the lockout start timestamp
    // This only runs once (when count first reaches 5), not on every locked-out call
    if (attemptCount == 5) {
      lockoutStart = millis();
    }

    // Build error message with remaining attempts
    String page;
    int remainingAttempts = 5 - attemptCount;
    String message = "Incorrect Password, " + String(remainingAttempts) + " attempts remaining";
    page = buildPage(message, 0, false);
    server.send(200, "text/html", page);

    // Keep servo at locked position (0°)
    myServo.write(0);

    // OLED: show wrong passcode + remaining attempts
    int remaining = 5 - attemptCount;
    String attemptsMsg = String(remaining) + " attempts remaining";
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(5, 16, "Wrong passcode");
    u8g2.drawHLine(0, 22, 124);
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(8, 40, attemptsMsg.c_str());
    u8g2.setFont(u8g2_font_open_iconic_check_2x_t);
    u8g2.drawGlyph(5, 18, 0x46);
    u8g2.sendBuffer();
  }
}

// ============================================================
// FORM HANDLER — called automatically when browser GETs /
// ============================================================
void form() {
  int remainingSeconds;
  String page;

  // If currently locked out, calculate remaining seconds and send lockout page
  // This prevents someone from bypassing lockout by refreshing the page
  if (attemptCount >= 5) {
    remainingSeconds = (600000 - (millis() - lockoutStart)) / 1000;
    page = buildPage("", remainingSeconds, false);
    server.send(200, "text/html", page);
    return;
  } else {
    // Normal first visit — send clean login page, no message, no overlay
    page = buildPage("", 0, false);
    server.send(200, "text/html", page);
  }
}

// ============================================================
// BUILD PAGE — returns the complete HTML page with placeholders replaced
// Single template reused for all states (idle, wrong password, lockout, success)
// ============================================================
String buildPage(String message, int remainingSeconds, bool unlocked) {
  String page = R"rawliteral(
  <!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Lock</title>
  <style>
    body {
      margin: 0;
      font-family: sans-serif;
      background-color: #f4f4f2;
    }
    .page {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      height: 100vh;
    }
    .card {
      background-color: #ffffff;
      padding: 32px 24px;
      border-radius: 16px;
      max-width: 300px;
      width: 100%;
      border: 1px solid #e0e0e0;
      box-sizing: border-box;
      text-align: center;
      position: relative;
    }
    .icon-circle {
      width: 48px;
      height: 48px;
      border-radius: 50%;
      background-color: #eef2f7;
      display: flex;
      align-items: center;
      justify-content: center;
      margin: 0 auto 12px auto;
      font-size: 22px;
    }
    h1 {
      font-size: 20px;
      margin: 0 0 4px 0;
      font-weight: 600;
    }
    .subtitle {
      font-size: 13px;
      color: #888;
      margin: 0 0 20px 0;
    }
    label {
      display: block;
      font-size: 13px;
      color: #666;
      text-align: left;
      margin-bottom: 6px;
    }
    input[type="password"] {
      width: 100%;
      padding: 10px;
      border-radius: 8px;
      border: 1px solid #ccc;
      box-sizing: border-box;
      font-size: 14px;
      margin-bottom: 10px;
    }
    .message {
      font-size: 13px;
      color: #c0392b;
      min-height: 16px;
      margin-bottom: 14px;
      text-align: left;
    }
    button {
      width: 100%;
      padding: 10px;
      border-radius: 8px;
      border: none;
      background-color: #eef2f7;
      color: #333;
      font-weight: 600;
      font-size: 14px;
      cursor: pointer;
    }
    .lockout-overlay {
      position: absolute;
      top: 0; left: 0; right: 0; bottom: 0;
      background-color: rgba(255, 255, 255, 0.7);
      backdrop-filter: blur(4px);
      display: none;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      border-radius: 16px;
    }
    .lockout-overlay.active {
      display: flex;
    }
    .lockout-overlay p {
      margin: 4px 0;
    }
    .lockout-title {
      font-weight: 600;
      font-size: 15px;
    }
    .lockout-timer {
      font-size: 13px;
      color: #666;
    }
  </style>
</head>
<body>

  <div class="page">
    <div class="card" id="card">

      <div class="icon-circle">ICON_PLACEHOLDER</div>
      <h1>HEADING_PLACEHOLDER</h1>
      <p class="subtitle">SUBTITLE_PLACEHOLDER</p>

      <form action="/login" method="POST" id="loginForm">
        <label for="pwd">Password</label>
        <input type="password" id="pwd" name="password" placeholder="••••••••">
        <div class="message">MESSAGE_PLACEHOLDER</div>
        <button type="submit">Unlock</button>
      </form>

      <!-- Lockout overlay: blurs the form and shows countdown timer -->
      <div class="lockout-overlay" id="lockoutOverlay">
        <div style="font-size:28px;">&#9201;</div>
        <p class="lockout-title">Locked out</p>
        <p class="lockout-timer" id="timerText">Try again in --:--</p>
      </div>

    </div>
  </div>

  <script>
    // remainingSeconds is injected server-side by buildPage()
    // If > 0, show the lockout overlay and start a client-side countdown
    var remainingSeconds = REMAINING_SECONDS_PLACEHOLDER;

    if (remainingSeconds > 0) {
      document.getElementById('lockoutOverlay').classList.add('active');

      var countdown = setInterval(function () {
        remainingSeconds--;

        var minutes = Math.floor(remainingSeconds / 60);
        var seconds = remainingSeconds % 60;
        var display = minutes + ":" + (seconds < 10 ? "0" : "") + seconds;
        document.getElementById('timerText').textContent = "Try again in " + display;

        if (remainingSeconds <= 0) {
          clearInterval(countdown);
          document.getElementById('lockoutOverlay').classList.remove('active');
        }
      }, 1000);
    }
  </script>

</body>
</html>
  )rawliteral";

  // Swap icon, heading and subtitle based on unlock state
  if (unlocked) {
    page.replace("ICON_PLACEHOLDER", "&#128275;");   // open lock emoji
    page.replace("HEADING_PLACEHOLDER", "SUCCESS!");
    page.replace("SUBTITLE_PLACEHOLDER", "Door is open now");
  } else {
    page.replace("ICON_PLACEHOLDER", "&#128274;");   // closed lock emoji
    page.replace("HEADING_PLACEHOLDER", "Smart Lock");
    page.replace("SUBTITLE_PLACEHOLDER", "Enter password to unlock");
  }

  // Inject error message and lockout countdown value
  page.replace("MESSAGE_PLACEHOLDER", message);
  page.replace("REMAINING_SECONDS_PLACEHOLDER", String(remainingSeconds));

  return page;
}

// ============================================================
// OLED SCREEN FUNCTIONS
// ============================================================

// Idle screen — shown at boot before anyone connects
// Displays AP name and WiFi password so the user knows how to connect
void drawIdleScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(20, 14, "Smart Lock");
  u8g2.drawHLine(0, 20, 124);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 38, "WiFi: ESP32");
  u8g2.drawStr(0, 54, "Pass: 12345678");
  u8g2.sendBuffer();
}

// Connected screen — shown once a device joins the AP
// Prompts the user to open a browser and navigate to the lock's IP
void drawConnectedScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(15, 14, "Connected");
  u8g2.drawHLine(0, 20, 124);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 38, "Go to:");
  u8g2.drawStr(0, 54, "192.168.4.1");
  u8g2.sendBuffer();
}

// Helper — picks idle or connected screen based on current station count
// Used both on connection state change and after lockout/relock resets the display
void updateIdleOrConnectedScreen(int stationCount) {
  if (stationCount == 0) {
    drawIdleScreen();
  } else {
    drawConnectedScreen();
  }
}

// ============================================================
// LOOP
// ============================================================
void loop() {

  // --- Connection state change detection ---
  // Only redraws OLED when the number of connected devices actually changes
  // Avoids redrawing identical content every loop pass
  int currentStationCount = WiFi.softAPgetStationNum();
  if (currentStationCount != previousStationCount) {
    previousStationCount = currentStationCount;
    updateIdleOrConnectedScreen(currentStationCount);
  }

  // --- Lockout state ---
  if (attemptCount >= 5) {

    // Independent cooldown check — resets lockout even without a login attempt
    // Necessary because login() only runs when someone submits a form
    if ((millis() - lockoutStart) > 600000) {
      attemptCount = 0;
      updateIdleOrConnectedScreen(currentStationCount);
    }

    // OLED lockout countdown — redraws once per second to tick down the timer
    if (millis() - previousTimerUpdate >= 1000) {
      previousTimerUpdate = millis();
      int remainingSeconds = (600000 - (millis() - lockoutStart)) / 1000;
      int minutes = remainingSeconds / 60;
      int seconds = remainingSeconds % 60;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB10_tr);
      u8g2.drawStr(25, 14, "Locked Out");
      u8g2.drawHLine(0, 20, 124);
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.drawStr(25, 38, "Try again in:");
      String countdown = String(minutes) + ":" + (seconds < 10 ? "0" : "") + String(seconds);
      u8g2.setFont(u8g2_font_ncenB14_tr);
      u8g2.drawStr(35, 58, countdown.c_str());
      u8g2.sendBuffer();
    }
  }

  // --- Auto-relock ---
  // Once 60 seconds have passed since unlock, sweep servo back to closed position
  // isUnlocked flag prevents this from re-triggering after the first close
  if (isUnlocked && millis() - unlockTime >= 60000) {
    isUnlocked = false;
    for (int i = 80; i >= 0; i--) {
      myServo.write(i);
      delay(15);
    }
    updateIdleOrConnectedScreen(currentStationCount);
  }

  // --- Unlock countdown on OLED ---
  // Shows "Closing in: Xs" while the door is open, updated once per second
  // Stops drawing once secondsLeft hits 0 — auto-relock block above handles the actual close
  if (isUnlocked) {
    if (millis() - previousTimerUpdate >= 1000) {
      previousTimerUpdate = millis();
      int secondsLeft = (60000 - (millis() - unlockTime)) / 1000;

      if (secondsLeft > 0) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB10_tr);
        u8g2.drawStr(20, 14, "Unlocked!");
        u8g2.drawHLine(0, 20, 124);
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(24, 38, "Closing in:");
        u8g2.drawStr(44, 54, (String(secondsLeft) + "s").c_str());
        u8g2.sendBuffer();
      }
    }
  }

  // --- Process incoming HTTP requests ---
  // Must be called every loop pass — non-blocking check for new requests
  // Triggers form() or login() automatically based on the requested path
  server.handleClient();
}
