#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal.h>
#include <ESP32Servo.h>

// ===== WiFi =====
const char* ssid     = "Your WIFI/Hotspot";
const char* password = "Your WIFI/Hotspot PIN";

#define FREE_THRESHOLD      8     // slot free if distance > 8 cm
#define ENTRY_THRESHOLD_CM  5     // gate opens if entry distance < 5 cm

// ===== LCD (16x2 parallel) =====
#define LCD_RS 33
#define LCD_EN 32
#define LCD_D4 25
#define LCD_D5 26
#define LCD_D6 27
#define LCD_D7 14
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// ===== Ultrasonic sensors for slots =====
#define TRIG1 5
#define ECHO1 18
#define TRIG2 23
#define ECHO2 22
#define TRIG3 4
#define ECHO3 19
#define TRIG4 21
#define ECHO4 13

// ===== Entry sensor =====
#define TRIG_ENTRY 12
#define ECHO_ENTRY 35   

// ===== Servo (gate) =====
#define SERVO_PIN 2
Servo gateServo;

// ===== Web server & Global State =====
WebServer server(80);
int totalSlots = 4;
String gateStatus = "Closed";
unsigned long lastGateCloseTime = 0;
bool gateOpenRequested = false;
bool manualOverride = false; // FALLBACK FLAG

// ----- Distance Helper -----
int readDistance(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duration = pulseIn(echo, HIGH, 30000);
  int distance = duration * 0.034 / 2;
  if (distance <= 0 || distance > 400) return 400;
  return distance;
}

void readSlots(int &d1, int &d2, int &d3, int &d4, bool &s1, bool &s2, bool &s3, bool &s4) {
  d1 = readDistance(TRIG1, ECHO1);
  d2 = readDistance(TRIG2, ECHO2);
  d3 = readDistance(TRIG3, ECHO3);
  d4 = readDistance(TRIG4, ECHO4);
  s1 = d1 > FREE_THRESHOLD;
  s2 = d2 > FREE_THRESHOLD;
  s3 = d3 > FREE_THRESHOLD;
  s4 = d4 > FREE_THRESHOLD;
}

// ----- Gate control -----
void requestGateOpen() {
  if (!gateOpenRequested) {
    gateStatus = "Open";
    gateServo.write(0);  
    gateOpenRequested = true;
    lastGateCloseTime = millis();
    Serial.println("Gate OPENED");
  }
}

// ----- API Handlers -----
void handleToggle() {
  manualOverride = true; // Set the fallback
  requestGateOpen();
  server.send(200, "text/plain", "OK");
}

void handleData() {
  int d1, d2, d3, d4; bool s1, s2, s3, s4;
  readSlots(d1, d2, d3, d4, s1, s2, s3, s4);
  int availableSlots = (s1 + s2 + s3 + s4);

  String json = "{";
  json += "\"available\":" + String(availableSlots) + ",";
  json += "\"occupied\":" + String(totalSlots - availableSlots) + ",";
  json += "\"gate\":\"" + gateStatus + "\",";
  json += "\"occupied_slots\":[";
  if (!s1) json += "1,";
  if (!s2) json += "2,";
  if (!s3) json += "3,";
  if (!s4) json += "4,";
  if (json.endsWith(",")) json.remove(json.length() - 1);
  json += "]}";
  server.send(200, "application/json", json);
}

// ----- UI HTML -----
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>SmartPark Pro</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <style>
    body { background-color: #0d1117; color: white; font-family: 'Inter', sans-serif; }
    .glow-cyan { text-shadow: 0 0 10px #22d3ee; }
    .glow-rose { text-shadow: 0 0 10px #fb7185; }
    .card-bg { background-color: #161b22; border: 1px solid #374151; }
    .slot-card { transition: all 0.3s ease; }
  </style>
</head>
<body class="min-h-screen flex flex-col items-center p-6">
  <div class="text-center mb-10">
    <h1 class="text-4xl font-black text-cyan-400">SMART<span class="text-white">PARK</span></h1>
    <p class="text-gray-500 text-xs uppercase tracking-widest font-bold">IoT Manual Fallback System</p>
  </div>

  <div class="grid grid-cols-1 md:grid-cols-3 gap-6 w-full max-w-5xl mb-10">
    <div class="card-bg p-8 rounded-3xl text-center">
      <h3 class="text-gray-500 text-xs font-bold uppercase mb-2">Available</h3>
      <p id="available" class="text-6xl font-black text-cyan-400 glow-cyan">0</p>
    </div>
    <div class="card-bg p-8 rounded-3xl text-center">
      <h3 class="text-gray-500 text-xs font-bold uppercase mb-2">Occupied</h3>
      <p id="occupied" class="text-6xl font-black text-rose-500 glow-rose">0</p>
    </div>
    <div class="card-bg p-8 rounded-3xl text-center flex flex-col justify-between">
      <div>
        <h3 class="text-gray-500 text-xs font-bold uppercase mb-2">Gate</h3>
        <p id="gate" class="text-3xl font-black uppercase">---</p>
      </div>
      <button onclick="fetch('/toggle')" class="mt-4 bg-cyan-600 hover:bg-cyan-500 text-white font-bold py-2 px-4 rounded-xl text-xs uppercase transition-all active:scale-95">
        Manual Open
      </button>
    </div>
  </div>

  <div id="slots" class="grid grid-cols-2 md:grid-cols-4 gap-6 w-full max-w-5xl"></div>

  <script>
    async function update() {
      try {
        const res = await fetch('/data');
        const data = await res.json();
        document.getElementById("available").innerText = data.available;
        document.getElementById("occupied").innerText = data.occupied;
        const g = document.getElementById("gate");
        g.innerText = data.gate;
        g.className = "text-3xl font-black uppercase " + (data.gate.includes("Open") ? "text-cyan-400 glow-cyan" : "text-rose-500 glow-rose");

        let html = "";
        for (let i = 1; i <= 4; i++) {
          const isOcc = data.occupied_slots.includes(i);
          html += `
            <div class="slot-card p-6 rounded-2xl border-2 flex flex-col items-center justify-center ${isOcc ? 'border-rose-500/30 bg-rose-900/10' : 'border-cyan-500/30 bg-cyan-900/10'}">
              <span class="text-[10px] font-bold text-gray-500 mb-2">SLOT 0${i}</span>
              <span class="text-xl font-bold ${isOcc ? 'text-rose-400' : 'text-cyan-400'}">${isOcc ? 'BUSY' : 'FREE'}</span>
            </div>`;
        }
        document.getElementById("slots").innerHTML = html;
      } catch(e) {}
    }
    setInterval(update, 1000);
  </script>
</body>
</html>
)rawliteral";

// ----- Setup & Loop -----
void setup() {
  Serial.begin(115200);
  
  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(TRIG3, OUTPUT); pinMode(ECHO3, INPUT);
  pinMode(TRIG4, OUTPUT); pinMode(ECHO4, INPUT);
  pinMode(TRIG_ENTRY, OUTPUT); pinMode(ECHO_ENTRY, INPUT);

  lcd.begin(16, 2);
  lcd.print("WiFi Connecting");

  gateServo.attach(SERVO_PIN);
  gateServo.write(90); // Start Closed

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

  server.on("/", []() { server.send(200, "text/html", index_html); });
  server.on("/data", handleData);
  server.on("/toggle", handleToggle);
  server.begin();

  lcd.clear();
  lcd.print("IP: ");
  lcd.setCursor(0,1);
  lcd.print(WiFi.localIP().toString());
}

void loop() {
  server.handleClient();

  int d1, d2, d3, d4; bool s1, s2, s3, s4;
  readSlots(d1, d2, d3, d4, s1, s2, s3, s4);
  int availableSlots = (s1 + s2 + s3 + s4);
  int entryDist = readDistance(TRIG_ENTRY, ECHO_ENTRY);
  bool carAtEntry = (entryDist > 0 && entryDist < ENTRY_THRESHOLD_CM);

  // LOGIC: Open if (available AND car) OR (manual fallback pressed)
  if ((availableSlots > 0 && carAtEntry && !gateOpenRequested) || (manualOverride && !gateOpenRequested)) {
    requestGateOpen();
  }

  // LOGIC: Force gate closed if full AND no manual override
  if (availableSlots == 0 && !manualOverride) {
    gateServo.write(90);
    gateStatus = "Closed";
    gateOpenRequested = false;
  }

  // Auto-close timer (4 seconds)
  if (gateOpenRequested && (millis() - lastGateCloseTime > 4000)) {
    gateServo.write(90);
    gateStatus = "Closed";
    gateOpenRequested = false;
    manualOverride = false; // Reset the fallback for next car
  }

  // LCD Update (Simplified)
  lcd.setCursor(14,0); lcd.print(availableSlots);
  lcd.setCursor(14,1); lcd.print(gateStatus[0]);
  
  delay(100);
}
