#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal.h>
#include <ESP32Servo.h>

// ===== WiFi =====
const char* ssid     = "Mohammed's A55";
const char* password = "123456789#";

#define FREE_THRESHOLD      8     // slot free if distance > 8 cm (adjust if needed)
#define ENTRY_THRESHOLD_CM  5    // gate opens if entry distance < 2 cm

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

// ===== Extra ultrasonic at entry (5th sensor) =====
#define TRIG_ENTRY 12
#define ECHO_ENTRY 35   // input-only pin, OK for echo

// ===== Servo (gate) =====
#define SERVO_PIN 2
Servo gateServo;

// ===== Web server =====
WebServer server(80);
int totalSlots = 4;
String gateStatus = "Closed";
unsigned long lastGateCloseTime = 0;
bool gateOpenRequested = false;

// ----- Distance -----
int readDistance(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duration = pulseIn(echo, HIGH, 30000);
  int distance = duration * 0.034 / 2;
  if (distance < 0) distance = 0;
  if (distance > 400) distance = 400;
  return distance;
}

// Get all slot distances and booleans
void readSlots(int &d1,int &d2,int &d3,int &d4, bool &s1,bool &s2,bool &s3,bool &s4) {
  d1 = readDistance(TRIG1, ECHO1);
  d2 = readDistance(TRIG2, ECHO2);
  d3 = readDistance(TRIG3, ECHO3);
  d4 = readDistance(TRIG4, ECHO4);

  s1 = d1 > FREE_THRESHOLD;
  s2 = d2 > FREE_THRESHOLD;
  s3 = d3 > FREE_THRESHOLD;
  s4 = d4 > FREE_THRESHOLD;
}

int calculateAvailableSlots() {
  int d1,d2,d3,d4;
  bool s1,s2,s3,s4;
  readSlots(d1,d2,d3,d4,s1,s2,s3,s4);

  int freeCount = (s1 + s2 + s3 + s4);
  Serial.printf("Slots(cm): %d %d %d %d | Free: %d\n", d1, d2, d3, d4, freeCount);
  return freeCount;
}

// ----- LCD -----
void updateLCD(bool s1,bool s2,bool s3,bool s4, bool carAtEntry) {
  lcd.clear();
  lcd.setCursor(0,0);
  // S1,S2,S3,S4 = Free/Occ (short form to fit)
  lcd.print("1:"); lcd.print(s1 ? "Fr " : "Oc ");
  lcd.print("2:"); lcd.print(s2 ? "Fr " : "Oc");

  lcd.setCursor(0,1);
  lcd.print("3:"); lcd.print(s3 ? "Fr " : "Oc ");
  lcd.print("4:"); lcd.print(s4 ? "Fr " : "Oc");

  // Entry + Gate info
  lcd.setCursor(11,0);
  lcd.print(carAtEntry ? "E:C" : "E:N"); // Entry: Car / None

  lcd.setCursor(11,1);
  lcd.print(gateStatus[0]); // 'O' or 'C'
  lcd.print("  ");          // clear rest
}

// ----- Gate control -----
void checkGateAutoClose() {
  // 4000 ms = 4 seconds open time
  if (gateOpenRequested && (millis() - lastGateCloseTime > 4000)) {
    gateStatus = "Closing";
    gateServo.write(90);      // closed angle
    delay(300);
    gateStatus = "Closed";
    gateOpenRequested = false;
    Serial.println("Gate AUTO-CLOSED");
  }
}

void requestGateOpen() {
  if (!gateOpenRequested) {
    gateStatus = "Opening";
    gateServo.write(0);       // open angle
    delay(300);
    gateStatus = "Open";
    gateOpenRequested = true;
    lastGateCloseTime = millis();
    Serial.println("Gate OPENED (auto-close in 5s)");
  }
}

// ----- JSON API -----
void handleData() {
  int d1,d2,d3,d4;
  bool s1,s2,s3,s4;
  readSlots(d1,d2,d3,d4,s1,s2,s3,s4);
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

// ----- HTML UI -----
String getHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>IoT Smart Parking System</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;700&display=swap');
    body { font-family: 'Inter', sans-serif; }
    .glow-green { text-shadow: 0 0 5px #4ade80, 0 0 10px #4ade80; }
    .glow-red { text-shadow: 0 0 5px #f87171, 0 0 10px #f87171; }
    .glow-lime { text-shadow: 0 0 5px #a3e635, 0 0 10px #a3e635; }
    .border-glow-green { box-shadow: 0 0 10px #a3e635, 0 0 20px rgba(163,230,53,0.5); border-color: #a3e635; }
    .border-glow-red { box-shadow: 0 0 10px #f87171, 0 0 20px rgba(248,113,113,0.5); border-color: #f87171; }
    .card-bg { background-color: #161b22; }
    .body-bg { background-color: #0d1117; }
  </style>
</head>
<body class="body-bg text-white min-h-screen flex flex-col items-center justify-center p-4">
  <div class="text-center mb-8">
    <h1 class="text-4xl md:text-5xl font-extrabold text-teal-400 mb-2">IoT Smart Parking System</h1>
    <p class="text-gray-400 text-lg">4 Ultrasonic Sensors + Auto Gate</p>
  </div>
  <div class="w-full max-w-5xl mb-8">
    <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
      <div class="card-bg border border-gray-700 rounded-3xl p-6 md:p-8 shadow-2xl flex flex-col items-start">
        <h2 class="text-xl font-semibold text-gray-200 mb-2">Available Slots</h2>
        <p id="available" class="text-5xl md:text-6xl font-bold text-green-400 glow-green">4</p>
        <p class="text-gray-400 text-sm mt-1">out of 4 total slots</p>
      </div>
      <div class="card-bg border border-gray-700 rounded-3xl p-6 md:p-8 shadow-2xl flex flex-col items-start">
        <h2 class="text-xl font-semibold text-gray-200 mb-2">Occupied Slots</h2>
        <p id="occupied" class="text-5xl md:text-6xl font-bold text-red-400 glow-red">0</p>
        <p class="text-gray-400 text-sm mt-1">currently parked</p>
      </div>
      <div class="card-bg border border-gray-700 rounded-3xl p-6 md:p-8 shadow-2xl flex flex-col items-start">
        <h2 class="text-xl font-semibold text-gray-200 mb-2">Gate Status</h2>
        <p id="gate" class="text-4xl md:text-5xl font-bold text-red-400 glow-red transition-colors duration-300">Closed</p>
        <p class="text-gray-400 text-sm mt-1">Auto-close 5s</p>
      </div>
    </div>
  </div>
  <div class="w-full max-w-5xl card-bg border border-gray-700 rounded-3xl p-6 md:p-8 shadow-2xl">
    <h2 class="text-2xl md:text-3xl font-bold text-gray-200 mb-6">Parking Slots Status</h2>
    <div id="slots" class="grid grid-cols-2 md:grid-cols-4 gap-6 w-full"></div>
  </div>
  <script>
    async function updateData() {
      try {
        const res = await fetch('/data');
        const data = await res.json();
        document.getElementById("available").innerText = data.available;
        document.getElementById("occupied").innerText = data.occupied;

        const gateElement = document.getElementById("gate");
        gateElement.innerText = data.gate;
        if (data.gate.includes("Open")) {
          gateElement.classList.remove("glow-red","text-red-400");
          gateElement.classList.add("glow-lime","text-lime-400");
        } else {
          gateElement.classList.remove("glow-lime","text-lime-400");
          gateElement.classList.add("glow-red","text-red-400");
        }

        const slotsContainer = document.getElementById("slots");
        let layoutHtml = "";
        for (let i = 1; i <= 4; i++) {
          const isOccupied = data.occupied_slots.includes(i);
          const slotStatus = isOccupied ? "Occupied" : "Available";
          const slotClass  = isOccupied ? "bg-red-900 border-glow-red" : "bg-green-900 border-glow-green";
          const statusClass = isOccupied ? "text-red-200" : "text-green-200";
          layoutHtml += `
            <div class="p-6 rounded-xl border-4 ${slotClass} flex flex-col items-center justify-center text-center h-32">
              <div class="text-2xl font-bold text-gray-100 mb-2">Slot ${i}</div>
              <div class="text-lg font-semibold ${statusClass}">${slotStatus}</div>
            </div>`;
        }
        slotsContainer.innerHTML = layoutHtml;
      } catch(e) {
        console.error(e);
      }
    }
    setInterval(updateData, 500);
    updateData();
  </script>
</body>
</html>
)rawliteral";
}

void handleRoot() {
  server.send(200, "text/html", getHTML());
}

// ===== SETUP & LOOP =====
void setup() {
  Serial.begin(115200);

  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(TRIG3, OUTPUT); pinMode(ECHO3, INPUT);
  pinMode(TRIG4, OUTPUT); pinMode(ECHO4, INPUT);

  pinMode(TRIG_ENTRY, OUTPUT);
  pinMode(ECHO_ENTRY, INPUT);

  lcd.begin(16, 2);
  lcd.print("Smart Parking");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  gateServo.attach(SERVO_PIN);
  gateServo.write(90);  // start closed

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.print("Connected:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  int d1,d2,d3,d4;
  bool s1,s2,s3,s4;
  readSlots(d1,d2,d3,d4,s1,s2,s3,s4);
  int availableSlots = (s1 + s2 + s3 + s4);

  int entryDist = readDistance(TRIG_ENTRY, ECHO_ENTRY);
  bool carAtEntry = (entryDist > 0 && entryDist < ENTRY_THRESHOLD_CM);
  Serial.printf("EntryDist: %d cm | CarAtEntry: %d | Free: %d\n", entryDist, carAtEntry, availableSlots);

  updateLCD(s1, s2, s3, s4, carAtEntry);

  // Gate opens ONLY if car is near entry AND at least one slot is free
  if (availableSlots > 0 &&
      carAtEntry &&
      !gateOpenRequested) {
    requestGateOpen();
  }

  // If no slots free, force gate closed
  if (availableSlots == 0) {
    gateStatus = "Closed";
    gateServo.write(90);
    gateOpenRequested = false;
  }

  checkGateAutoClose();
  delay(200);
}
