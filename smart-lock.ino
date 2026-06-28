#include <WebServer.h>
#include <ESP32Servo.h>
#include <U8g2lib.h>
#include<WiFi.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
WebServer server(80);
Servo myServo;

int attemptCount = 0;
unsigned long lockoutStart = 0;
int previousStationCount = 0;
unsigned long previousTimerUpdate;
unsigned long unlockTime;
bool isUnlocked;

void drawIdleScreen();

void setup() {
  Serial.begin(115200);
  myServo.attach(13);
  myServo.write(0);
  WiFi.softAP("ESP32","12345678");
  server.begin();
  server.on("/",form);
  server.on("/login",login);
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  drawIdleScreen();
  Serial.println(WiFi.softAPIP());
}


void login(){
  int remainingSeconds;
  String page;
  if(attemptCount >= 5){
    if(millis()-lockoutStart >= 600000){
      attemptCount = 0;
    }
    else{
    remainingSeconds = (600000 - ( millis()-lockoutStart ))/1000;
    page = buildPage("", remainingSeconds,false);
    server.send(200,"text/html",page);
    return;
    }
  }
  if(server.arg("password") == "12345678"){
    unlockTime = millis();
    isUnlocked = true;
    page = buildPage("",0,true);
    server.send(200, "text/html",page);
    for(int i=0;i<=80;i++){
      myServo.write(i);
      delay(15);
    };
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(16, 30, "Unlocked!");
    u8g2.drawHLine(0, 38, 124);
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(16, 54, "Welcome back");
    u8g2.sendBuffer();
  }
  else{
    attemptCount++ ;
    if(attemptCount == 5){
    lockoutStart = millis();
    }
    String page;
    int remainingAttempts = 5 - attemptCount;
    String message = "Incorrect Password, " + String(remainingAttempts) + " attempts remaining";
    page = buildPage(message,0,false);
    server.send(200, "text/html", page);
    myServo.write(0);
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


void form(){
  int remainingSeconds;
  String page;
  if(attemptCount >= 5){
    remainingSeconds = (600000 - ( millis()-lockoutStart ))/1000;
    page = buildPage("", remainingSeconds,false);
    server.send(200,"text/html",page);
    return;
  }
  else{
    page = buildPage("", 0,false);
    server.send(200,"text/html",page);
  }
}


String buildPage(String message, int remainingSeconds,bool unlocked) {
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

      <div class="lockout-overlay" id="lockoutOverlay">
        <div style="font-size:28px;">&#9201;</div>
        <p class="lockout-title">Locked out</p>
        <p class="lockout-timer" id="timerText">Try again in --:--</p>
      </div>

    </div>
  </div>

  <script>
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
  if(unlocked){
    page.replace("ICON_PLACEHOLDER","&#128275;");
    page.replace("HEADING_PLACEHOLDER","SUCCESS!");
    page.replace("SUBTITLE_PLACEHOLDER","Door is open now");
  }
  else{
    page.replace("ICON_PLACEHOLDER","&#128274;");
    page.replace("HEADING_PLACEHOLDER","Smart Lock");
    page.replace("SUBTITLE_PLACEHOLDER","Enter password to unlock");
  }
  page.replace("MESSAGE_PLACEHOLDER", message);
  page.replace("REMAINING_SECONDS_PLACEHOLDER", String(remainingSeconds));

  return page;
}


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


void updateIdleOrConnectedScreen(int stationCount) {
  if (stationCount == 0) {
    drawIdleScreen();
  } else {
    drawConnectedScreen();
  }
}


void loop(){
  int currentStationCount = WiFi.softAPgetStationNum();
  if (currentStationCount != previousStationCount) {
    previousStationCount = currentStationCount;
    updateIdleOrConnectedScreen(currentStationCount);
  }
  if(attemptCount >= 5){
    if((millis() - lockoutStart) > 600000 ){
      attemptCount = 0;
      updateIdleOrConnectedScreen(currentStationCount);
    }
    if(millis() - previousTimerUpdate >= 1000){
      previousTimerUpdate = millis();
      int remainingSeconds = (600000 - ( millis()-lockoutStart ))/1000;
      int minutes = (remainingSeconds)/60;
      int seconds = (remainingSeconds)%60;
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
  if(isUnlocked && millis() - unlockTime >= 60000) {
    isUnlocked = false;
    for(int i=80;i>=0;i--){
      myServo.write(i);
      delay(15);
    }
    updateIdleOrConnectedScreen(currentStationCount);
  }
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
  server.handleClient();
}
