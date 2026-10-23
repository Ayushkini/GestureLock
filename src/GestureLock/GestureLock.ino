#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <LiquidCrystal_I2C.h>

// GestureLock: ESP32 + MPU6050 only.
// Uses direct I2C register access; no ESP32_MPU6050 library required.

const char* WIFI_SSID = "TP LINK 2";
const char* WIFI_PASSWORD = "ayush776655";

#define SDA_PIN 21
#define SCL_PIN 22
#define BUZZER_PIN 25       // Active buzzer signal pin
#define LCD_ADDR 0x27       // Common I2C LCD backpack address (try 0x3F if needed)
#define LCD_COLS 16
#define LCD_ROWS 2

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
uint8_t mpuAddress = 0x68;  // Detected automatically: 0x68 or 0x69.

WebServer server(80);

enum Gesture { NONE, LEFT, RIGHT, FORWARD };
const Gesture passwordSequence[] = {LEFT, RIGHT, FORWARD};
const int PASSWORD_LENGTH = 3;

const float TILT_THRESHOLD = 25.0;
const float NEUTRAL_THRESHOLD = 12.0;
const unsigned long SEQUENCE_TIMEOUT_MS = 100000;
const unsigned long UNLOCK_DURATION_MS = 5000;

int progress = 0;
bool readyForGesture = true;
bool unlocked = false;
float pitchAngle = 0.0, rollAngle = 0.0;
unsigned long sequenceStarted = 0, unlockUntil = 0, lastSensorRead = 0;
int successfulAttempts = 0, failedAttempts = 0;
String lastEvent = "Starting system";
String lastGesture = "None";

// ---------- Low-level MPU6050 I2C ----------
bool probeAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(mpuAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t startReg, uint8_t count, uint8_t* data) {
  Wire.beginTransmission(mpuAddress);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) return false;

  size_t received = Wire.requestFrom((uint8_t)mpuAddress, (size_t)count, (bool)true);
  if (received != count) return false;

  for (uint8_t i = 0; i < count; i++) data[i] = Wire.read();
  return true;
}

bool initMPU6050() {
  // The I2C scanner confirms a device responds, but probe both standard addresses.
  if (probeAddress(0x68)) {
    mpuAddress = 0x68;
  } else if (probeAddress(0x69)) {
    mpuAddress = 0x69;
  } else {
    Serial.println("No I2C device found at 0x68 or 0x69.");
    return false;
  }

  Serial.print("I2C device responding at 0x");
  Serial.println(mpuAddress, HEX);

  // Read WHO_AM_I for diagnostics. Some compatible/clone boards report unexpected IDs,
  // so log the value rather than rejecting the device based on this byte alone.
  uint8_t whoAmI = 0;
  if (readRegisters(0x75, 1, &whoAmI)) {
    Serial.print("WHO_AM_I register: 0x");
    Serial.println(whoAmI, HEX);
  } else {
    Serial.println("Could not read WHO_AM_I register.");
    return false;
  }

  // Reset the device, then wake it up using the internal clock.
  if (!writeRegister(0x6B, 0x80)) {
    Serial.println("Could not reset MPU6050.");
    return false;
  }
  delay(100);

  if (!writeRegister(0x6B, 0x01)) {
    Serial.println("Could not wake MPU6050.");
    return false;
  }
  delay(100);

  // DLPF ~20 Hz, sample rate divider.
  if (!writeRegister(0x1A, 0x04)) return false;
  if (!writeRegister(0x19, 0x04)) return false;

  // Accelerometer range ±2 g; gyro range ±250 degrees/s.
  if (!writeRegister(0x1C, 0x00)) return false;
  if (!writeRegister(0x1B, 0x00)) return false;

  // Verify that accelerometer registers can now be read.
  uint8_t test[6];
  if (!readRegisters(0x3B, 6, test)) {
    Serial.println("MPU responded, but accelerometer data read failed.");
    return false;
  }

  return true;
}

int16_t combineBytes(uint8_t highByte, uint8_t lowByte) {
  return (int16_t)((uint16_t)highByte << 8 | lowByte);
}

bool readTilt() {
  uint8_t raw[6];
  // ACCEL_XOUT_H through ACCEL_ZOUT_L
  if (!readRegisters(0x3B, 6, raw)) return false;

  int16_t rawAx = combineBytes(raw[0], raw[1]);
  int16_t rawAy = combineBytes(raw[2], raw[3]);
  int16_t rawAz = combineBytes(raw[4], raw[5]);

  // MPU6050 ±2g sensitivity: 16384 LSB/g.
  float ax = rawAx / 16384.0;
  float ay = rawAy / 16384.0;
  float az = rawAz / 16384.0;

  pitchAngle = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  rollAngle  = atan2(ay, az) * 180.0 / PI;
  return true;
}

Gesture detectGesture() {
  if (rollAngle <= -TILT_THRESHOLD) return LEFT;
  if (rollAngle >= TILT_THRESHOLD) return RIGHT;
  if (pitchAngle >= TILT_THRESHOLD) return FORWARD;
  return NONE;
}

bool isNeutral() {
  return fabs(pitchAngle) < NEUTRAL_THRESHOLD &&
         fabs(rollAngle) < NEUTRAL_THRESHOLD;
}

String gestureName(Gesture g) {
  switch (g) {
    case LEFT: return "LEFT";
    case RIGHT: return "RIGHT";
    case FORWARD: return "FORWARD";
    default: return "NONE";
  }
}

// ---------- LCD and buzzer ----------
void showLockState(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void buzzerOn(unsigned long durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}

void successBeep() {
  // Correct password / unlocked: one long confirmation beep.
  buzzerOn(700);
}

void incorrectBeep() {
  // Incorrect password / expired sequence: three quick warning beeps.
  for (int i = 0; i < 3; i++) {
    buzzerOn(220);
    delay(100);
  }
}

// ---------- Authentication ----------
void denyAccess(const String& reason) {
  failedAttempts++;
  lastEvent = "ACCESS DENIED: " + reason;
  showLockState("ACCESS: LOCKED", "Try password");
  incorrectBeep();
  Serial.println(lastEvent);
  progress = 0;
  sequenceStarted = 0;
}

void grantAccess() {
  successfulAttempts++;
  progress = 0;
  sequenceStarted = 0;
  unlocked = true;
  unlockUntil = millis() + UNLOCK_DURATION_MS;
  lastEvent = "ACCESS GRANTED - unlocked for 5 seconds";
  showLockState("ACCESS: UNLOCKED", "Welcome!");
  successBeep();
  Serial.println(lastEvent);
}

void processGesture(Gesture g) {
  lastGesture = gestureName(g);
  Serial.print("Detected gesture: ");
  Serial.println(lastGesture);

  if (progress == 0) sequenceStarted = millis();

  if (millis() - sequenceStarted > SEQUENCE_TIMEOUT_MS) {
    denyAccess("TIME EXPIRED");
    return;
  }

  if (g == passwordSequence[progress]) {
    progress++;
    if (progress >= PASSWORD_LENGTH) {
      grantAccess();
    } else {
      lastEvent = "Gesture accepted: " + gestureName(g) +
                  ". Next: " + gestureName(passwordSequence[progress]);
      Serial.println(lastEvent);
    }
  } else {
    bool restart = (g == passwordSequence[0]);
    denyAccess("WRONG GESTURE");
    if (restart) {
      progress = 1;
      sequenceStarted = millis();
      lastEvent = "Restarted sequence. Next: RIGHT";
      Serial.println(lastEvent);
    }
  }
}

// ---------- Web dashboard ----------
String dashboardPage() {
  String status = unlocked ? "UNLOCKED" : "LOCKED";
  String statusClass = unlocked ? "unlocked" : "locked";
  String step1 = progress >= 1 ? "done" : (progress == 0 ? "current" : "");
  String step2 = progress >= 2 ? "done" : (progress == 1 ? "current" : "");
  String step3 = progress >= 3 ? "done" : (progress == 2 ? "current" : "");

  String html = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>GestureLock Dashboard</title>
<style>
*{box-sizing:border-box}body{margin:0;padding:28px 16px;font-family:Inter,Segoe UI,Arial,sans-serif;background:radial-gradient(circle at top left,#172554 0,#0b1120 42%);color:#e5e7eb}
.container{max-width:1100px;margin:auto}.hero{padding:24px;border:1px solid #2d4160;border-radius:22px;background:linear-gradient(135deg,#172554,#151f32 60%,#102a2a);box-shadow:0 18px 50px #0004;margin-bottom:18px}.eyebrow{color:#6ee7b7;font-size:12px;letter-spacing:2px;text-transform:uppercase;margin-bottom:8px}header{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:22px}
h1{margin:0;font-size:32px;letter-spacing:-.8px}.sub{color:#94a3b8;margin-top:7px;font-size:14px}.badge{background:#123c32;color:#6ee7b7;border-radius:20px;padding:8px 12px;font-size:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:14px}.card{background:linear-gradient(145deg,#172236,#121b2d);border:1px solid #2b3b55;border-radius:18px;padding:22px;min-width:0;box-shadow:0 10px 30px #0002;transition:transform .2s,border-color .2s}.card:hover{transform:translateY(-2px);border-color:#456184}
.label{color:#94a3b8;font-size:13px;margin-bottom:12px}.value{font-size:26px;font-weight:bold;overflow-wrap:anywhere}.small{font-size:13px;color:#94a3b8;margin-top:9px;overflow-wrap:anywhere}
.locked{color:#fbbf24}.unlocked{color:#6ee7b7}.danger{color:#fca5a5}.section{margin-top:16px}
.steps{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:16px}.step{border:1px solid #35445c;border-radius:12px;padding:14px 5px;text-align:center;color:#94a3b8;font-size:13px}
.done{border-color:#10b981;background:#123c32;color:#6ee7b7}.current{border-color:#60a5fa;background:#172c4b;color:#93c5fd}
.event{font-size:15px;line-height:1.6;overflow-wrap:anywhere}.footer{color:#64748b;text-align:center;font-size:12px;margin-top:22px}
@media(max-width:400px){body{padding:18px 10px}h1{font-size:23px}.value{font-size:22px}.step{font-size:11px}}
</style></head><body><div class="container">
<div class="hero"><header><div><div class="eyebrow">Secure gesture authentication</div><h1>GestureLock</h1><div class="sub">ESP32 + MPU6050 · Offline local access control</div></div><div class="badge">● ESP32 DASHBOARD</div></header></div>
<div class="grid">
<div class="card"><div class="label">AUTHENTICATION STATUS</div><div id="status" class="value %STATUS_CLASS%">%STATUS%</div><div class="small">Automatic relock after 5 seconds</div></div>
<div class="card"><div class="label">LAST DETECTED GESTURE</div><div id="gesture" class="value">%GESTURE%</div><div class="small">Sensor: MPU6050</div></div>
<div class="card"><div class="label">SUCCESSFUL ATTEMPTS</div><div id="success">%SUCCESS%</div><div class="small">Successful authentications</div></div>
<div class="card"><div class="label">FAILED ATTEMPTS</div><div id="failed">%FAILED%</div><div class="small">Rejected attempts</div></div></div>
<div class="card section"><div class="label">GESTURE PASSWORD</div><div class="value" style="font-size:18px">LEFT → RIGHT → FORWARD</div>
<div class="steps"><div class="step %STEP1%">1. LEFT</div><div class="step %STEP2%">2. RIGHT</div><div class="step %STEP3%">3. FORWARD</div></div>
<div id="progress" class="small">Progress: %PROGRESS% of 3 gestures</div></div>
<div class="card section"><div class="label">LATEST SYSTEM EVENT</div><div id="event" class="event">%EVENT%</div></div>
<div class="footer">GestureLock · Offline local dashboard · Connect to the GestureLock Wi-Fi hotspot<br>Software simulation only — no physical lock is connected.</div>
</div>
<script>
async function updateStatus() {
  try {
    const response = await fetch('/status', { cache: 'no-store' });
    if (!response.ok) {
      console.warn("GestureLock status endpoint returned", response.status);
      return;
    }
    const s = await response.json();
    const statusEl = document.getElementById('status');
    statusEl.textContent = s.status;
    statusEl.className = 'value ' + (s.status === 'UNLOCKED' ? 'unlocked' : 'locked');
    document.getElementById('gesture').textContent = s.gesture;
    document.getElementById('success').textContent = s.success;
    document.getElementById('failed').textContent = s.failed;
    document.getElementById('progress').textContent = 'Progress: ' + s.progress + ' of 3 gestures';
    document.getElementById('event').textContent = s.event;
    ['step1','step2','step3'].forEach((key, i) => {
      const el = document.querySelectorAll('.steps .step')[i];
      if (el) el.className = 'step ' + s[key];
    });
  } catch (e) {
    console.warn("GestureLock dashboard update failed:", e);
  }
}
updateStatus();
setInterval(updateStatus, 500);
</script>
</div></body></html>
)rawliteral";

  html.replace("%STATUS%", status);
  html.replace("%STATUS_CLASS%", statusClass);
  html.replace("%GESTURE%", lastGesture);
  html.replace("%SUCCESS%", String(successfulAttempts));
  html.replace("%FAILED%", String(failedAttempts));
  html.replace("%PROGRESS%", String(progress));
  html.replace("%EVENT%", lastEvent);
  html.replace("%STEP1%", step1);
  html.replace("%STEP2%", step2);
  html.replace("%STEP3%", step3);
  return html;
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", dashboardPage());
}

String jsonEscape(const String& input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"') output += "\\\"";
    else if (c == '\\') output += "\\\\";
    else if (c == '\n') output += "\\n";
    else if (c == '\r') output += "\\r";
    else if (c == '\t') output += "\\t";
    else if ((uint8_t)c >= 0x20) output += c;
  }
  return output;
}

void handleStatus() {
  String json = "{";
  json += "\"status\":\"" + String(unlocked ? "UNLOCKED" : "LOCKED") + "\",";
  json += "\"gesture\":\"" + jsonEscape(lastGesture) + "\",";
  json += "\"success\":" + String(successfulAttempts) + ",";
  json += "\"failed\":" + String(failedAttempts) + ",";
  json += "\"progress\":" + String(progress) + ",";
  json += "\"event\":\"" + jsonEscape(lastEvent) + "\",";
  json += "\"step1\":\"" + String(progress >= 1 ? "done" : (progress == 0 ? "current" : "")) + "\",";
  json += "\"step2\":\"" + String(progress >= 2 ? "done" : (progress == 1 ? "current" : "")) + "\",";
  json += "\"step3\":\"" + String(progress >= 3 ? "done" : (progress == 2 ? "current" : "")) + "\"";
  json += "}";
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.send(200, "application/json", json);
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  lcd.init();
  lcd.backlight();
  showLockState("GestureLock", "Starting...");

  Serial.println("\nStarting GestureLock...");
  Serial.println("Checking MPU6050...");

  if (!initMPU6050()) {
    Serial.println("MPU6050 initialization failed. Check the sensor, SDA/SCL wiring, and I2C address.");
    lastEvent = "MPU6050 sensor error";
    while (true) delay(1000);
  }

  Serial.println("MPU6050 initialized successfully.");
  showLockState("ACCESS: LOCKED", "LEFT RIGHT FWD");
  Serial.println("Password: LEFT -> RIGHT -> FORWARD");
  Serial.println("Return the sensor to neutral between gestures.");

  // Start a standalone Wi-Fi hotspot. No router or internet is required.
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  const char* AP_SSID = "GestureLock";
  const char* AP_PASSWORD = "Lock12345";  // At least 8 characters

  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    IPAddress apIP = WiFi.softAPIP();

    Serial.println();
    Serial.println("GestureLock hotspot started.");
    Serial.print("Wi-Fi name: ");
    Serial.println(AP_SSID);
    Serial.print("Wi-Fi password: ");
    Serial.println(AP_PASSWORD);
    Serial.print("Dashboard: http://");
    Serial.println(apIP);

    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.begin();

    lastEvent = "System ready - GestureLock hotspot online";
    Serial.println("Dashboard and /status endpoint started.");
    showLockState("ACCESS: LOCKED", "Connect to WiFi");
  } else {
    Serial.println("Failed to start GestureLock hotspot.");
    lastEvent = "Wi-Fi hotspot failed";
    showLockState("WiFi AP FAILED", "Check restart");
  }
}

// ---------- Main loop ----------
void loop() {
  server.handleClient();

  if (millis() - lastSensorRead >= 30) {
    lastSensorRead = millis();

    if (readTilt()) {
      if (isNeutral()) readyForGesture = true;

      if (readyForGesture && !unlocked) {
        Gesture g = detectGesture();
        if (g != NONE) {
          readyForGesture = false;
          processGesture(g);
        }
      }
    }
  }

  if (unlocked && (long)(millis() - unlockUntil) >= 0) {
    unlocked = false;
    showLockState("ACCESS: LOCKED", "System relocked");
    lastEvent = "System relocked";
    Serial.println(lastEvent);
  }

  if (progress > 0 && millis() - sequenceStarted > SEQUENCE_TIMEOUT_MS) {
    denyAccess("TIME EXPIRED");
  }

  delay(5);
}