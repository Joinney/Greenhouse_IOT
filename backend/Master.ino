#include <WiFi.h>
#include <WebSocketMCP.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <ESPmDNS.h> 
#include <esp_wifi.h>
#include <time.h> 
#include <DHT.h>

// >>>>> 1. THƯ VIỆN FIREBASE <<<<<
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ==========================================
// 1. CẤU HÌNH PHẦN CỨNG ESP32-S3
// ==========================================
#define TFT_SCLK    39 
#define TFT_MOSI    40 
#define TFT_CS      42  
#define TFT_RST     38  
#define TFT_DC      41  

// --- CẢM BIẾN TRÊN MASTER ---
#define DHT_PIN_S3      4   
#define RAIN_PIN_S3     7  
// LƯU Ý: Nếu cảm biến màu XANH là DHT11, màu TRẮNG là DHT22
#define DHTTYPE         DHT11

// >>> CẤU HÌNH HIỆU CHỈNH NHIỆT ĐỘ (CALIBRATION) <<<
const float TEMP_OFFSET = -7.0; 
const float HUMI_OFFSET = 0.0;  

// >>> RELAY TRỰC TIẾP TRÊN MASTER <<<
#define PIN_FAN_S3      12  // Nối chân IN của Relay Quạt vào GPIO 12
#define PIN_PUMP_S3     13  // Nối chân IN của Relay Bơm vào GPIO 13

// --- MÀU SẮC GIAO DIỆN ---
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_GREY    0x39E7 
#define C_DARK    0x10A2 
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_CYAN    0x07FF
#define C_ORANGE  0xFD20
#define C_YELLOW  0xFFE0
#define C_PURPLE  0xF81F
#define C_MAGENTA 0xF81F 

#define BUTTON_RESET_PIN 0 
#define RX_FROM_SLAVE 18 
#define TX_TO_SLAVE   17 

// ==========================================
// 2. NGƯỠNG & CẤU HÌNH
// ==========================================
#define FIREBASE_API_KEY      "AIzaSyDTFe35IUamuBKUZPt5UclmBYDdV50aI4c"
#define FIREBASE_DATABASE_URL "https://greenhouseg3td-default-rtdb.asia-southeast1.firebasedatabase.app"

const int MOISTURE_DRY = 4095; 
const int MOISTURE_WET = 1500;
const unsigned long HISTORY_INTERVAL = 900000; 

const long   gmtOffset_sec = 25200;
const int    daylightOffset_sec = 0;
const char* ntpServer = "pool.ntp.org";

const char* apSsidDefault = "GreenHouse_Ultimate";
const char* apPassDefault = "88888888";
const char* hostName      = "greenhouse"; 
const char* mcpEndpoint   = "wss://api.xiaozhi.me/mcp/?token=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOjU4MzcxNSwiYWdlbnRJZCI6MTMxMjYyNywiZW5kcG9pbnRJZCI6ImFnZW50XzEzMTI2MjciLCJwdXJwb3NlIjoibWNwLWVuZHBvaW50IiwiaWF0IjoxNzY4Mzg2NDYzLCJleHAiOjE3OTk5NDQwNjN9.IpOECrrJLEN6zd_BhN-A2A1Q3vQrIG9GfamC-FBw-sNR3urknyTn5iIS_eIaIk_VWnetfS1iFbZUm6NqW8dokg";

// ==========================================
// 3. KHỞI TẠO ĐỐI TƯỢNG
// ==========================================
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
WebSocketMCP mcpClient;
DHT dht(DHT_PIN_S3, DHTTYPE); 

FirebaseData fbdo;
FirebaseData streamData; // >>> THÊM BIẾN NÀY ĐỂ STREAM <<<
FirebaseAuth auth;
FirebaseConfig configFirebase; 
bool firebaseReady = false;

struct SystemState {
  float temp = 0.0; float humi = 0.0;
  int soilRaw = 0; int soilPct = 0; int lightRaw = 0;
  bool isRaining = false; int doorDistance = 0;

  bool isFanOn = false; int fanMode = 0; 
  bool isPumpOn = false; int pumpMode = 0; 
  bool isRoomLightOn = false; int roomLightMode = 0; 
  bool isGrowLightOn = false; int growLightMode = 0;       
  int r = 255, g = 0, b = 255; long rgbHue = 0;

  bool isDoorOpen = false; int doorMode = 0;        
  bool isRoofOpen = true;  int roofMode = 0;       
  
  int doorCmd = 0; int roofCmd = 0;

  bool pumpTimerEnabled = false;
  int pumpStartH = 7; int pumpStartM = 0; int pumpDuration = 5;   
  bool isPumpRunningByTimer = false;
  unsigned long pumpTimerStartMillis = 0;
};
SystemState sys;

unsigned long lastScreenUpdate = 0;
unsigned long lastFirebaseSend = 0;
unsigned long lastHistorySend = 0;
unsigned long lastRgbUpdate = 0;
unsigned long lastLogicCheck = 0;
unsigned long lastLocalSensorRead = 0; 

// ==========================================
// 4. HTML INTERFACE
// ==========================================
const char htmlConfig[] PROGMEM = R"html(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SETUP WIFI</title><style>body{font-family:sans-serif;text-align:center;background:#222;color:white;padding:20px}input,select{width:100%;max-width:300px;height:40px;margin:10px 0;border-radius:5px;text-align:center;border:none}button{padding:12px 30px;margin:10px;border-radius:5px;cursor:pointer;background:#007bff;color:white;border:none;font-weight:bold}.btn-ctrl{background:#28a745;margin-top:20px}</style></head><body><h2>⚙️ CẤU HÌNH WIFI</h2><div id="main"><p id="info">Đang quét...</p><select id="ssid"></select><br><input id="pass" type="text" placeholder="Pass WiFi"><br><button onclick="save()">LƯU & KẾT NỐI</button><hr style="border-color:#444;margin:20px 0"><button class="btn-ctrl" onclick="window.location.href='/control'">MỞ BẢNG ĐIỀU KHIỂN</button></div><script>var x=new XMLHttpRequest;window.onload=function(){scan()};function scan(){x.onreadystatechange=function(){if(4==x.readyState&&200==x.status){var e=JSON.parse(x.responseText),n=document.getElementById("ssid");if(n.innerHTML="",0===e.length)return void(document.getElementById("info").innerText="Không tìm thấy mạng.");e.forEach(e=>{var t=e.ssid+" ("+e.rssi+" dBm)",o=new Option(t,e.ssid);n.options[n.options.length]=o}),document.getElementById("info").innerText="Tìm thấy "+e.length+" mạng!"}},x.open("GET","/scanWifi",!0),x.send()}function save(){var e=document.getElementById("ssid").value,n=document.getElementById("pass").value;if(!e)return alert("Chưa chọn Wifi!");document.getElementById("info").innerHTML="<span style='color:yellow'>Đang kết nối...</span>";var t=new XMLHttpRequest;t.open("GET","/saveWifi?ssid="+encodeURIComponent(e)+"&pass="+encodeURIComponent(n),!0),t.onreadystatechange=function(){if(4==t.readyState&&200==t.status)try{var o=JSON.parse(t.responseText);if("ok"===o.status&&o.ip){var s="http://"+o.ip+"/control",i="<h3 style='color:#00e676'>ĐÃ KẾT NỐI!</h3><p>IP: <b style='color:#00e676'>"+o.ip+"</b></p><button class='btn-ctrl' onclick=\"window.location.href='"+s+"'\">➡️ VÀO BẢNG ĐIỀU KHIỂN</button>";document.getElementById("main").innerHTML=i}else alert("Lỗi kết nối!"),document.getElementById("info").innerText="Thử lại!"}catch(e){alert("Lỗi phản hồi!")}},t.send()}</script></body></html>)html";

const char htmlControl[] PROGMEM = R"html(<!DOCTYPE html><html lang="vi"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>GreenHouse Ultimate</title><link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css"><style>:root{--bg:#121212;--card:#1e1e1e;--text:#e0e0e0;--pri:#00e676;--sec:#2979ff;--dan:#ff1744}body{font-family:Roboto,sans-serif;background:var(--bg);color:var(--text);margin:0;padding:15px;text-align:center}.container{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:15px;max-width:1200px;margin:0 auto}.card{background:var(--card);border-radius:15px;padding:20px;box-shadow:0 4px 15px rgba(0,0,0,.5);border:1px solid #333}.card-header{font-size:1.1rem;color:#aaa;margin-bottom:15px;display:flex;align-items:center;justify-content:center;gap:10px}.val{font-size:2rem;font-weight:700;margin:5px 0;color:#fff}.unit{font-size:.9rem;color:#888}.ctrl-grp{display:flex;justify-content:space-around;margin-top:15px;padding-top:15px;border-top:1px solid #333}.btn{border:none;padding:10px 20px;border-radius:20px;font-weight:700;cursor:pointer;width:80px;transition:.2s}.btn-on{background:rgba(0,230,118,.1);color:var(--pri);border:1px solid var(--pri)}.btn-on.active{background:var(--pri);color:#000}.btn-off{background:rgba(255,23,68,.1);color:var(--dan);border:1px solid var(--dan)}.btn-off.active{background:var(--dan);color:#fff}.mode-sw{display:inline-flex;background:#252525;padding:4px;border-radius:20px;margin-bottom:10px}.mode-opt{padding:6px 15px;border-radius:15px;cursor:pointer;color:#666;font-size:.9rem}.mode-opt.active{background:var(--sec);color:#fff}.icon-door{color:#e040fb}a{color:#888;text-decoration:none;margin-top:20px;display:block}</style></head><body><h2><i class="fas fa-leaf" style="color:#00e676"></i> SMART GREENHOUSE V7</h2><div class="container"><div class="card" style="border-color:#e040fb"><div class="card-header"><i class="fas fa-dungeon icon-door"></i> CỬA TỰ ĐỘNG</div><div style="font-size:.9rem;color:#aaa">Khoảng cách: <span id="val-door-dist" style="color:#fff;font-weight:700">--</span> cm</div><div id="st-door" style="font-size:1.2rem;font-weight:700;margin:10px 0;color:#e040fb">--</div><div class="mode-sw"><div id="door-mode-auto" class="mode-opt" onclick="ctrl('door','auto')">Auto</div><div id="door-mode-manual" class="mode-opt" onclick="ctrl('door','close')">Manual</div></div><div class="ctrl-grp"><button id="btn-door-on" class="btn btn-on" onclick="ctrl('door','open')">MỞ</button> <button id="btn-door-off" class="btn btn-off" onclick="ctrl('door','close')">ĐÓNG</button></div></div><div class="card"><div class="card-header"><i class="fas fa-seedling" style="color:#f0f"></i> ĐÈN TĂNG TRƯỞNG</div><div class="mode-sw"><div id="grow-mode-manual" class="mode-opt" onclick="ctrlGrow('mode','manual')">Màu</div><div id="grow-mode-fade" class="mode-opt" onclick="ctrlGrow('mode','fade')">Rainbow</div></div><input type="color" id="rgb-picker" value="#ff00ff" onchange="ctrlGrow('color',this.value)" style="margin-top:10px;width:60px;height:60px;border:none;cursor:pointer"><div class="ctrl-grp"><button id="btn-grow-on" class="btn btn-on" onclick="ctrlGrow('power','on')">BẬT</button> <button id="btn-grow-off" class="btn btn-off" onclick="ctrlGrow('power','off')">TẮT</button></div></div><div class="card"><div class="card-header"><i class="fas fa-lightbulb" style="color:#fbc02d"></i> ĐÈN PHÒNG</div><div style="font-size:.9rem;color:#aaa">Cảm biến: <span id="val-light" style="color:#fff;font-weight:700">--</span></div><div class="mode-sw" style="margin-top:10px"><div id="room-mode-auto" class="mode-opt" onclick="ctrl('room','auto')">Auto</div><div id="room-mode-manual" class="mode-opt" onclick="ctrl('room','off')">Manual</div></div><div class="ctrl-grp"><button id="btn-room-on" class="btn btn-on" onclick="ctrl('room','on')">BẬT</button> <button id="btn-room-off" class="btn btn-off" onclick="ctrl('room','off')">TẮT</button></div></div><div class="card"><div class="card-header"><i class="fas fa-cloud-showers-heavy"></i> MÁI CHE</div><div id="st-rain" style="font-weight:700;margin-bottom:10px">--</div><div class="mode-sw"><div id="roof-mode-auto" class="mode-opt" onclick="ctrl('roof','auto')">Auto</div><div id="roof-mode-manual" class="mode-opt" onclick="ctrl('roof','close')">Manual</div></div><div class="ctrl-grp"><button id="btn-roof-on" class="btn btn-on" onclick="ctrl('roof','open')">MỞ</button> <button id="btn-roof-off" class="btn btn-off" onclick="ctrl('roof','close')">ĐÓNG</button></div></div><div class="card"><div class="card-header"><i class="fas fa-water" style="color:#2979ff"></i> BƠM TƯỚI</div><div class="val" id="val-soil">--</div><div class="unit">Độ ẩm đất (%)</div><div class="mode-sw" style="margin-top:10px"><div id="pump-mode-auto" class="mode-opt" onclick="ctrl('pump','auto')">Auto</div><div id="pump-mode-manual" class="mode-opt" onclick="ctrl('pump','off')">Manual</div></div><div class="ctrl-grp"><button id="btn-pump-on" class="btn btn-on" onclick="ctrl('pump','on')">BẬT</button> <button id="btn-pump-off" class="btn btn-off" onclick="ctrl('pump','off')">TẮT</button></div></div><div class="card"><div class="card-header"><i class="fas fa-thermometer-half" style="color:#ffea00"></i> MÔI TRƯỜNG</div><div style="display:flex;justify-content:space-around"><div><div class="val" id="val-temp">--</div><div class="unit">Nhiệt độ (°C)</div></div><div><div class="val" id="val-humi">--</div><div class="unit">Độ ẩm (%)</div></div></div><div class="ctrl-grp" style="flex-direction:column;border:none"><div style="margin-bottom:5px;color:#aaa">Quạt gió</div><div class="mode-sw"><div id="fan-mode-auto" class="mode-opt" onclick="ctrl('fan','auto')">Auto</div><div id="fan-mode-manual" class="mode-opt" onclick="ctrl('fan','off')">Manual</div></div><div style="display:flex;justify-content:center;gap:10px"><button id="btn-fan-on" class="btn btn-on" onclick="ctrl('fan','on')">BẬT</button> <button id="btn-fan-off" class="btn btn-off" onclick="ctrl('fan','off')">TẮT</button></div></div></div></div><a href="/">⚙️ Cấu hình WiFi</a><script>function ctrl(e,t){fetch("/api?dev="+e+"&act="+t).then(update)}function ctrlGrow(e,t){"color"===e&&(t=t.substring(1)),fetch("/api_grow?type="+e+"&val="+t).then(update)}function update(){fetch("/status").then(e=>e.json()).then(e=>{document.getElementById("val-temp").innerText=e.temp.toFixed(1),document.getElementById("val-humi").innerText=e.humi.toFixed(0),document.getElementById("val-soil").innerText=e.moisture.toFixed(0),document.getElementById("val-door-dist").innerText=e.doorDist,document.getElementById("st-door").innerText=e.doorOpen?"ĐANG MỞ":"ĐANG ĐÓNG",document.getElementById("st-rain").innerHTML=e.isRaining?"<span style='color:#2979ff'>CÓ MƯA (Đóng)</span>":"<span style='color:#ffea00'>NẮNG (Mở)</span>",document.getElementById("val-light").innerText=e.lightRaw,upBtn("door",e.doorOpen),upMode("door",e.doorMode),upBtn("pump",e.pump),upMode("pump",e.pumpMode),upBtn("fan",e.fan),upMode("fan",e.fanMode),upBtn("roof",e.roofOpen),upMode("roof",e.roofMode),upBtn("room",e.roomLight),upMode("room",e.roomLightMode),upBtn("grow",e.growLight);let t=1===e.growMode;document.getElementById("grow-mode-manual").className=t?"mode-opt":"mode-opt active",document.getElementById("grow-mode-fade").className=t?"mode-opt active":"mode-opt"})}function upBtn(e,t){let n=document.getElementById("btn-"+e+"-on"),o=document.getElementById("btn-"+e+"-off");n&&o&&(n.className=t?"btn btn-on active":"btn btn-on",o.className=t?"btn btn-off":"btn btn-off active")}function upMode(e,t){let n=document.getElementById(e+"-mode-auto"),o=document.getElementById(e+"-mode-manual");n&&o&&(n.className=0===t?"mode-opt active":"mode-opt",o.className=0!==t?"mode-opt active":"mode-opt")}setInterval(update,2e3),window.onload=update</script></body></html>)html";

// ==========================================
// 5. HÀM VẼ GIAO DIỆN
// ==========================================

void drawProgressBar(int x, int y, int w, int h, int val, int maxVal, uint16_t color) {
  tft.drawRect(x, y, w, h, C_GREY); 
  int fillW = map(val, 0, maxVal, 0, w - 2);
  fillW = constrain(fillW, 0, w - 2);
  tft.fillRect(x + 1, y + 1, fillW, h - 2, color);
  tft.fillRect(x + 1 + fillW, y + 1, w - 2 - fillW, h - 2, C_BLACK);
}

void drawStatusBox(int x, int y, int w, int h, const char* label, bool active, uint16_t activeColor) {
  uint16_t bgColor = active ? activeColor : C_GREY;
  uint16_t txtColor = active ? C_BLACK : C_WHITE;
  tft.fillRoundRect(x, y, w, h, 3, bgColor);
  tft.setTextColor(txtColor, bgColor); 
  tft.setTextSize(1);
  int textWidth = strlen(label) * 6;
  int textX = x + (w - textWidth) / 2;
  int textY = y + (h - 8) / 2; 
  tft.setCursor(textX, textY); 
  tft.print(label);
}

void initDashboard() {
  tft.fillScreen(C_BLACK); 
  tft.fillRect(0, 0, 160, 15, C_DARK);
  tft.drawFastHLine(0, 15, 160, C_GREY);
  tft.setTextColor(C_WHITE, C_DARK);
  tft.setTextSize(1);
  tft.setCursor(4, 4); tft.print("SMART GARDEN");
  tft.setTextColor(C_GREY, C_BLACK);
  tft.setCursor(5, 53); tft.print("SOIL:");
}

void drawTFT() {
  if (millis() - lastScreenUpdate < 500) return; 
  lastScreenUpdate = millis();

  tft.setTextColor(C_CYAN, C_DARK);
  tft.setTextSize(1);
  tft.fillRect(110, 0, 50, 15, C_DARK);
  
  if (WiFi.status() == WL_CONNECTED) {
    struct tm ti;
    tft.setCursor(126, 4); 
    if(getLocalTime(&ti)) tft.printf("%02d:%02d", ti.tm_hour, ti.tm_min);
    else tft.print("WIFI");
  } else {
    tft.setTextColor(C_ORANGE, C_DARK); 
    tft.setCursor(110, 4); tft.print("OFFLINE");
  }

  tft.setCursor(5, 25);
  tft.setTextSize(3);
  tft.setTextColor(sys.temp > 30 ? C_ORANGE : C_WHITE, C_BLACK); 
  tft.printf("%.1f", sys.temp);
  
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BLACK);
  tft.setCursor(75, 25); tft.print("o");
  tft.setCursor(82, 25); tft.setTextSize(2); tft.print("C");

  tft.setCursor(110, 30);
  tft.setTextSize(2); 
  tft.setTextColor(C_CYAN, C_BLACK); 
  tft.printf("%.0f%%", sys.humi);

  uint16_t soilColor = (sys.soilPct < 40) ? C_RED : C_GREEN;
  drawProgressBar(40, 52, 70, 10, sys.soilPct, 100, soilColor);
  
  tft.setCursor(115, 53);
  tft.setTextSize(1);
  tft.setTextColor(soilColor, C_BLACK);
  tft.print(sys.soilPct); tft.print("% "); 

  tft.setCursor(5, 68);
  tft.setTextColor(C_GREY, C_BLACK); tft.print("RAIN: ");
  if (sys.isRaining) {
      tft.setTextColor(C_BLUE, C_BLACK); tft.print("Y  "); 
  } else {
      tft.setTextColor(C_ORANGE, C_BLACK); tft.print("N  "); 
  }

  tft.setCursor(85, 68);
  tft.setTextColor(C_GREY, C_BLACK); tft.print("LUX: ");
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.print(sys.lightRaw); tft.print("  "); 

  drawStatusBox(5, 85, 73, 18, "FAN", sys.isFanOn, C_RED);
  drawStatusBox(82, 85, 73, 18, "PUMP", sys.isPumpOn, C_BLUE);
  drawStatusBox(5, 108, 36, 18, "LMP", sys.isRoomLightOn, C_YELLOW);
  drawStatusBox(43, 108, 36, 18, "GRW", sys.isGrowLightOn, C_PURPLE);
  
  uint16_t roofColor = sys.isRoofOpen ? C_GREEN : C_ORANGE;
  const char* rfLabel = sys.isRoofOpen ? "RF:O" : "RF:C";
  drawStatusBox(81, 108, 36, 18, rfLabel, true, roofColor);
  
  uint16_t doorColor = sys.isDoorOpen ? C_MAGENTA : C_GREY;
  const char* drLabel = sys.isDoorOpen ? "DR:O" : "DR:C";
  drawStatusBox(119, 108, 36, 18, drLabel, true, doorColor);
  
  tft.fillRect(0, 127, 160, 1, C_GREY);
}

// ==========================================
// 6. LOGIC & DATA & FIREBASE
// ==========================================

void checkResetButton() {
  if (digitalRead(BUTTON_RESET_PIN) == LOW) {
    delay(50); 
    if (digitalRead(BUTTON_RESET_PIN) == LOW) {
      unsigned long startHold = millis();
      bool msgShown = false;
      while (digitalRead(BUTTON_RESET_PIN) == LOW) {
        if (millis() - startHold > 3000) {
          if (!msgShown) {
             tft.fillScreen(C_RED); 
             tft.setCursor(10, 40); tft.setTextColor(C_WHITE); tft.setTextSize(2); 
             tft.print("RESETTING...");
             tft.setCursor(10, 70); tft.setTextSize(1); tft.print("Wiping WiFi...");
             msgShown = true;
          }
          preferences.begin("wifi-creds", false); preferences.clear(); preferences.end();
          delay(1000); ESP.restart(); 
        }
        delay(10);
      }
    }
  }
}

// >>> ĐIỀU KHIỂN RELAY TRỰC TIẾP TRÊN MASTER <<<
void controlDirectActuators() {
    // Relay thường là kích mức THẤP (LOW) là BẬT, CAO (HIGH) là TẮT
    // Nếu bạn dùng Relay kích CAO, hãy đổi ngược lại (HIGH/LOW)
    digitalWrite(PIN_FAN_S3, sys.isFanOn ? LOW : HIGH);
    digitalWrite(PIN_PUMP_S3, sys.isPumpOn ? LOW : HIGH);
}

// Dán đè hàm này vào file MASTER
void readLocalSensors() {
  // Chỉ đọc mỗi 2 giây (DHT11 rất chậm, đọc nhanh quá sẽ bị lỗi hoặc treo giá trị cũ)
  if (millis() - lastLocalSensorRead > 2000) {
    lastLocalSensorRead = millis();
    
    // Đọc trực tiếp từ thư viện
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    // Kiểm tra lỗi
    if (isnan(t) || isnan(h)) {
        Serial.println("Lỗi đọc DHT11!");
    } else {
        // Gán giá trị thực tế
        // Nếu muốn chỉnh sai số thì sửa ở dòng TEMP_OFFSET đầu file
        sys.temp = t + TEMP_OFFSET; 
        sys.humi = h + HUMI_OFFSET;
    }
    
    // Đọc cảm biến mưa
    sys.isRaining = (digitalRead(RAIN_PIN_S3) == LOW);
  }
}

void readDataFromSlave() {
  String lastLine = "";
  while (Serial1.available()) {
    String t = Serial1.readStringUntil('\n');
    if (t.length() > 0) {
        lastLine = t;
    }
  }

  if (lastLine != "") {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, lastLine);
    if (!error) {
      if(doc.containsKey("sRaw")) sys.soilRaw = doc["sRaw"];
      if(doc.containsKey("lRaw")) sys.lightRaw = doc["lRaw"];
      if(doc.containsKey("dist")) sys.doorDistance = doc["dist"];
      
      sys.isDoorOpen = doc["dOp"]; 
      sys.isRoofOpen = doc["rOp"];
      
      sys.soilPct = map(sys.soilRaw, MOISTURE_DRY, MOISTURE_WET, 0, 100);
      sys.soilPct = constrain(sys.soilPct, 0, 100);
    }
  }
}

// >>> FIX: GỬI LỆNH RAINBOW ĐÚNG CÁCH <<<
void sendCommandToSlave() {
  StaticJsonDocument<512> doc;
  
  doc["roomL"] = sys.isRoomLightOn;
  
  if(sys.isGrowLightOn) { 
      if (sys.growLightMode == 1) {
         // Chế độ Rainbow: Gửi cờ bật, không cần gửi RGB
         doc["rainbow"] = 1;
      } else {
         // Chế độ Màu tĩnh: Tắt rainbow, gửi mã màu
         doc["rainbow"] = 0;
         doc["r"] = sys.r; 
         doc["g"] = sys.g; 
         doc["b"] = sys.b; 
      }
  } else { 
      // Tắt đèn: Gửi màu đen + tắt rainbow
      doc["rainbow"] = 0;
      doc["r"] = 0; 
      doc["g"] = 0; 
      doc["b"] = 0; 
  }
  
  if(sys.doorCmd != 0) { doc["dCmd"] = sys.doorCmd; sys.doorCmd = 0; }
  if(sys.roofCmd != 0) { doc["rCmd"] = sys.roofCmd; sys.roofCmd = 0; }
  
  String output; serializeJson(doc, output); Serial1.println(output);
}

void processLogic() {
  // Logic cũ giữ nguyên
  static unsigned long rainStoppedTime = 0;
  if (sys.isRaining) {
      rainStoppedTime = millis(); 
  }

  if (millis() - lastLogicCheck < 1000) return;
  lastLogicCheck = millis();

  readLocalSensors(); // Gọi hàm đọc cảm biến đã sửa

  struct tm ti;
  if(sys.pumpTimerEnabled && getLocalTime(&ti)) {
     if(!sys.isPumpRunningByTimer && ti.tm_hour == sys.pumpStartH && ti.tm_min == sys.pumpStartM && ti.tm_sec < 5) {
       sys.isPumpRunningByTimer = true; sys.pumpTimerStartMillis = millis();
       if(sys.pumpMode == 0) sys.isPumpOn = true;
     }
  }
  if(sys.isPumpRunningByTimer && (millis() - sys.pumpTimerStartMillis > sys.pumpDuration * 60000)) {
      sys.isPumpRunningByTimer = false;
      if(sys.pumpMode == 0) sys.isPumpOn = false;
  }

  if(sys.fanMode == 0) sys.isFanOn = (sys.temp >= 26);
  
  if(sys.pumpMode == 0) {
    if(sys.isPumpRunningByTimer) sys.isPumpOn = true;
    else {
      if(sys.soilPct < 35) sys.isPumpOn = true;
      else if(sys.soilPct > 35) sys.isPumpOn = false;
    }
  }
  
  if(sys.roomLightMode == 0) {
    if(sys.lightRaw > 2600) sys.isRoomLightOn = true;
    else if(sys.lightRaw < 2400) sys.isRoomLightOn = false;
  }

  if(sys.roofMode == 0) { // Auto
    if(sys.isRaining) {
        if(sys.isRoofOpen) sys.roofCmd = 2; 
    } 
    else {
        if (millis() - rainStoppedTime > 10000) {
             if(!sys.isRoofOpen) sys.roofCmd = 1; 
        }
    }
  } else if (sys.roofMode == 1) { 
    if (!sys.isRoofOpen) sys.roofCmd = 1;
  } else if (sys.roofMode == 2) { 
    if (sys.isRoofOpen) sys.roofCmd = 2;
  }
  
  if(sys.doorMode == 0) { 
    bool obj = (sys.doorDistance > 0 && sys.doorDistance <= 5);
    if(obj && !sys.isDoorOpen) sys.doorCmd = 1; 
    else if(!obj && sys.isDoorOpen) sys.doorCmd = 2;
  } else if (sys.doorMode == 1) { 
    if (!sys.isDoorOpen) sys.doorCmd = 1;
  } else if (sys.doorMode == 2) { 
    if (sys.isDoorOpen) sys.doorCmd = 2;
  }
}

// >>>>>> HÀM XỬ LÝ KHI CÓ LỆNH TỪ FIREBASE (MỚI THÊM) <<<<<<
void streamCallback(FirebaseStream data) {
  String path = data.dataPath();
  String val = data.stringData();
  
  // Debug
  Serial.print("Stream path: "); Serial.print(path);
  Serial.print(" - Value: "); Serial.println(val);

  // --- XỬ LÝ QUẠT (FAN) ---
  if (path == "/fan") {
    if (val == "auto") sys.fanMode = 0;
    else {
      sys.fanMode = 1; // Chuyển sang Manual
      sys.isFanOn = (val == "on");
    }
  }
  
  // --- XỬ LÝ BƠM (PUMP) ---
  else if (path == "/pump") {
    if (val == "auto") sys.pumpMode = 0;
    else {
      sys.pumpMode = 1;
      sys.isPumpOn = (val == "on");
    }
  }
  
  // --- XỬ LÝ ĐÈN PHÒNG (ROOM LAMP) ---
  else if (path == "/room") {
    if (val == "auto") sys.roomLightMode = 0;
    else {
      sys.roomLightMode = 1;
      sys.isRoomLightOn = (val == "on"); 
    }
  }

  // --- XỬ LÝ ĐÈN GROW ---
  else if (path == "/grow") {
      sys.isGrowLightOn = (val == "on");
  }
  else if (path == "/grow_mode") { // Thêm xử lý grow_mode
      sys.growLightMode = (val == "fade") ? 1 : 0;
  }
  else if (path == "/grow_color") { // Thêm xử lý grow_color
      long n = strtol(val.c_str(), NULL, 16);
      sys.r = n >> 16;
      sys.g = n >> 8 & 0xFF;
      sys.b = n & 0xFF;
      sys.growLightMode = 0; // Chuyển sang Manual khi có màu mới
      sys.isGrowLightOn = true; // Bật đèn luôn
  }
  
  // --- XỬ LÝ CỬA & MÁI (DOOR & ROOF) ---
  else if (path == "/door") {
      if (val == "auto") sys.doorMode = 0;
      else {
        sys.doorMode = (val == "open" ? 1 : 2); // 1: Manual Open, 2: Manual Close
        sys.doorCmd  = (val == "open" ? 1 : 2); 
      }
  }
  else if (path == "/roof") {
      if (val == "auto") sys.roofMode = 0;
      else {
        sys.roofMode = (val == "open" ? 1 : 2); // 1: Manual Open, 2: Manual Close
        sys.roofCmd  = (val == "open" ? 1 : 2);
      }
  }
  // --- XỬ LÝ CÁC PATH _MODE RIÊNG BIỆT (NẾU WEB GỬI) ---
  else if (path == "/pump_mode") {
      sys.pumpMode = (val == "manual" ? 1 : 0);
  }
  else if (path == "/fan_mode") {
      sys.fanMode = (val == "manual" ? 1 : 0);
  }
  else if (path == "/door_mode") {
      sys.doorMode = (val == "manual" ? 1 : 0); // Logic đơn giản: 1 là Manual (bất kể open/close), 0 là Auto
      // Lưu ý: Logic Manual Open/Close chi tiết hơn được xử lý ở trên (path == "/door")
  }
   else if (path == "/roof_mode") {
      sys.roofMode = (val == "manual" ? 1 : 0);
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("Stream timeout, resuming...");
}

void setupFirebase() {
  configFirebase.api_key = FIREBASE_API_KEY;
  configFirebase.database_url = FIREBASE_DATABASE_URL;
  
  // Tăng buffer cho stream
  streamData.setBSSLBufferSize(1024, 1024);

  if (Firebase.signUp(&configFirebase, &auth, "", "")) firebaseReady = true;
  Firebase.begin(&configFirebase, &auth);
  Firebase.reconnectWiFi(true);

  // >>> KÍCH HOẠT LẮNG NGHE TẠI ĐƯỜNG DẪN CONTROLS <<<
  if (!Firebase.RTDB.beginStream(&streamData, "/greenhouse/controls")) {
    Serial.println("Lỗi Stream: " + streamData.errorReason());
  }
  Firebase.RTDB.setStreamCallback(&streamData, streamCallback, streamTimeoutCallback);
}

void sendDataToFirebase() {
  if (!firebaseReady) return;
  unsigned long currentMillis = millis();

  if (currentMillis - lastFirebaseSend > 3000) {
    lastFirebaseSend = currentMillis;
    FirebaseJson json;
    
    json.set("sensors/temperature", sys.temp);
    json.set("sensors/humidity", sys.humi);
    json.set("sensors/soil_moisture", sys.soilPct);
    json.set("sensors/soil_raw", sys.soilRaw);
    json.set("sensors/light_lux", sys.lightRaw);
    json.set("sensors/rain_detected", sys.isRaining);
    json.set("sensors/door_distance", sys.doorDistance);

    json.set("status/fan", sys.isFanOn ? "ON" : "OFF");
    json.set("status/pump", sys.isPumpOn ? "ON" : "OFF");
    json.set("status/room_lamp", sys.isRoomLightOn ? "ON" : "OFF");
    json.set("status/grow_light", sys.isGrowLightOn ? "ON" : "OFF");
    json.set("status/roof", sys.isRoofOpen ? "OPEN" : "CLOSED");
    json.set("status/door", sys.isDoorOpen ? "OPEN" : "CLOSED");

    json.set("modes/fan_mode", sys.fanMode == 0 ? "AUTO" : "MANUAL");
    json.set("modes/pump_mode", sys.pumpMode == 0 ? "AUTO" : "MANUAL");
    json.set("modes/lamp_mode", sys.roomLightMode == 0 ? "AUTO" : "MANUAL");
    json.set("modes/roof_mode", sys.roofMode == 0 ? "AUTO" : "MANUAL");
    json.set("modes/door_mode", sys.doorMode == 0 ? "AUTO" : "MANUAL");
    
    FirebaseJson rgb; rgb.set("r", sys.r); rgb.set("g", sys.g); rgb.set("b", sys.b);
    json.set("settings/rgb_color", rgb);

    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
      char buf[50]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      json.set("system/timestamp", String(buf));
    }
    json.set("system/ip", WiFi.localIP().toString());
    json.set("system/wifi_rssi", WiFi.RSSI());

    Firebase.RTDB.setJSON(&fbdo, "/greenhouse/live_data", &json);
  }

  if (currentMillis - lastHistorySend > HISTORY_INTERVAL) {
    lastHistorySend = currentMillis;
    FirebaseJson historyJson;
    historyJson.set("temp", sys.temp);
    historyJson.set("humi", sys.humi);
    historyJson.set("soil", sys.soilPct);
    historyJson.set("lux", sys.lightRaw);
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
       char buf[50]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
       historyJson.set("timestamp", String(buf));
       time_t now; time(&now);
       historyJson.set("ts", (int)now);
    }
    Firebase.RTDB.pushJSON(&fbdo, "/greenhouse/history", &historyJson);
  }
}

void registerMcpTools() {
  mcpClient.registerTool("room_light_control", "Điều khiển Đèn Phòng", "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"on\",\"off\",\"auto\"]}},\"required\":[\"action\"]}", 
    [](const String& args) { DynamicJsonDocument doc(256); deserializeJson(doc, args); String act = doc["action"].as<String>(); if (act == "auto") sys.roomLightMode = 0; else { sys.roomLightMode = 1; sys.isRoomLightOn = (act == "on"); } return WebSocketMCP::ToolResponse("{\"status\":\"OK\"}"); });
  mcpClient.registerTool("grow_light_control", "Điều khiển Đèn Tăng Trưởng", "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"on\",\"off\",\"set_color\",\"set_mode\"]},\"value\":{\"type\":\"string\"}},\"required\":[\"action\"]}", 
    [](const String& args) { DynamicJsonDocument doc(512); deserializeJson(doc, args); String action = doc["action"].as<String>(); String val = doc["value"].as<String>(); if (action == "on") sys.isGrowLightOn = true; else if (action == "off") sys.isGrowLightOn = false; else if (action == "set_mode") { sys.growLightMode = (val == "fade") ? 1 : 0; if (sys.growLightMode) sys.isGrowLightOn = true; } else if (action == "set_color") { sys.growLightMode = 0; sys.isGrowLightOn = true; long n = strtol(val.c_str(), NULL, 16); sys.r=n>>16; sys.g=n>>8&0xFF; sys.b=n&0xFF; } return WebSocketMCP::ToolResponse("{\"status\":\"OK\"}"); });
  mcpClient.registerTool("pump_control", "Điều khiển Bơm Tưới", "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"on\",\"off\",\"auto\"]}},\"required\":[\"action\"]}", 
    [](const String& args) { DynamicJsonDocument doc(256); deserializeJson(doc, args); String a = doc["action"].as<String>(); if(a=="auto") sys.pumpMode=0; else { sys.pumpMode=1; sys.isPumpOn=(a=="on"); } return WebSocketMCP::ToolResponse("{\"status\":\"OK\"}"); });
  mcpClient.registerTool("fan_control", "Điều khiển Quạt Gió", "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"on\",\"off\",\"auto\"]}},\"required\":[\"action\"]}", 
    [](const String& args) { DynamicJsonDocument doc(256); deserializeJson(doc, args); String a = doc["action"].as<String>(); if(a=="auto") sys.fanMode=0; else { sys.fanMode=1; sys.isFanOn=(a=="on"); } return WebSocketMCP::ToolResponse("{\"status\":\"OK\"}"); });
  
  mcpClient.registerTool("roof_control", "Điều khiển Mái Che", "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"open\",\"close\",\"auto\"]}},\"required\":[\"action\"]}", 
    [](const String& args) { 
        DynamicJsonDocument doc(256); deserializeJson(doc, args); String a = doc["action"].as<String>(); 
        if(a=="auto") sys.roofMode=0; 
        else { 
            sys.roofMode = (a=="open"?1:2); 
            sys.roofCmd = (a=="open"?1:2); 
        } 
        return WebSocketMCP::ToolResponse("{\"status\":\"OK\"}"); 
    });
    
  mcpClient.registerTool("door_control", "Điều khiển Cửa Tự Động", "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"open\",\"close\",\"auto\"]}},\"required\":[\"action\"]}", 
    [](const String& args) { 
        DynamicJsonDocument doc(256); deserializeJson(doc, args); String a = doc["action"].as<String>(); 
        if(a=="auto") sys.doorMode=0; 
        else { 
            sys.doorMode = (a=="open"?1:2); 
            sys.doorCmd = (a=="open"?1:2); 
        } 
        return WebSocketMCP::ToolResponse("{\"status\":\"OK\"}"); 
    });
    
  mcpClient.registerTool("read_data", "Đọc cảm biến", "{}", [](const String& args) { String json = "{\"temp\":" + String(sys.temp) + ",\"humi\":" + String(sys.humi) + ",\"soil\":" + String(sys.soilPct) + ",\"light\":" + String(sys.lightRaw) + ",\"raining\":" + String(sys.isRaining) + ",\"doorDist\":"+String(sys.doorDistance)+"}"; return WebSocketMCP::ToolResponse(json); });
}

// ==========================================
// CẬP NHẬT: API TRẢ VỀ DỮ LIỆU ĐẦY ĐỦ
// ==========================================

// 1. Hàm tạo JSON đầy đủ (đã thêm soilRaw và RGB)
void handleStatus() {
  String json = "{";
  json += "\"temp\":" + String(sys.temp, 1) + ",";
  json += "\"humi\":" + String(sys.humi, 0) + ",";
  json += "\"moisture\":" + String(sys.soilPct) + ",";
  json += "\"soilRaw\":" + String(sys.soilRaw) + ","; // Debug đất
  json += "\"lightRaw\":" + String(sys.lightRaw) + ",";
  
  json += "\"fan\":" + String(sys.isFanOn) + ",";
  json += "\"fanMode\":" + String(sys.fanMode) + ","; 
  
  json += "\"pump\":" + String(sys.isPumpOn) + ",";
  json += "\"pumpMode\":" + String(sys.pumpMode) + ","; 
  
  json += "\"roomLight\":" + String(sys.isRoomLightOn) + ",";
  json += "\"roomLightMode\":" + String(sys.roomLightMode) + ","; 
  
  json += "\"growLight\":" + String(sys.isGrowLightOn) + ",";
  json += "\"growMode\":" + String(sys.growLightMode) + ",";
  
  // Trả về mã màu HEX để giao diện đồng bộ
  char hexCol[8];
  sprintf(hexCol, "#%02X%02X%02X", sys.r, sys.g, sys.b);
  json += "\"rgb\":\"" + String(hexCol) + "\",";

  json += "\"doorOpen\":" + String(sys.isDoorOpen) + ",";
  json += "\"doorMode\":" + String(sys.doorMode) + ",";
  json += "\"doorDist\":" + String(sys.doorDistance) + ",";
  
  json += "\"roofOpen\":" + String(sys.isRoofOpen) + ",";
  json += "\"roofMode\":" + String(sys.roofMode) + ",";
  
  json += "\"isRaining\":" + String(sys.isRaining);
  json += "}";
  
  server.send(200, "application/json", json);
}

// 2. Sửa API điều khiển để gọi handleStatus() cuối cùng
void handleApi() {
  String dev = server.arg("dev"); 
  String act = server.arg("act");
  
  if(dev == "fan") { 
    if(act=="auto") sys.fanMode=0; 
    else {sys.fanMode=1; sys.isFanOn=(act=="on");} 
  }
  else if(dev == "pump") { 
    if(act=="auto") sys.pumpMode=0; 
    else {sys.pumpMode=1; sys.isPumpOn=(act=="on");} 
  }
  else if(dev == "room") { 
    if(act=="auto") sys.roomLightMode=0; 
    else {sys.roomLightMode=1; sys.isRoomLightOn=(act=="on");} 
  }
  else if(dev == "door") { 
      if(act=="auto") sys.doorMode=0; 
      else { 
          sys.doorMode = (act=="open" ? 1 : 2); 
          sys.doorCmd = (act=="open" ? 1 : 2); 
      } 
  }
  else if(dev == "roof") { 
      if(act=="auto") sys.roofMode=0; 
      else { 
          sys.roofMode = (act=="open" ? 1 : 2); 
          sys.roofCmd = (act=="open" ? 1 : 2); 
      } 
  }
  
  handleStatus(); // Trả về JSON luôn
}

// 3. Sửa API đèn trồng cây để gọi handleStatus() cuối cùng
void handleApiGrow() {
  String type = server.arg("type"); 
  String val = server.arg("val");
  
  if (type == "power") {
    sys.isGrowLightOn = (val == "on");
  }
  else if (type == "mode") { 
    sys.growLightMode = (val == "fade") ? 1 : 0; 
    if (sys.growLightMode) sys.isGrowLightOn = true; 
  }
  else if (type == "color") { 
    sys.growLightMode = 0; 
    sys.isGrowLightOn = true; 
    long n = strtol(val.c_str(), NULL, 16); 
    sys.r = n >> 16; 
    sys.g = n >> 8 & 0xFF; 
    sys.b = n & 0xFF; 
  }
  
  handleStatus(); // Trả về JSON luôn
}

void handleScanWifi() { int n = WiFi.scanNetworks(); String j = "["; for(int i = 0; i < n; ++i){ if(i) j += ","; j += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}"; } j += "]"; server.send(200, "application/json", j); }

void handleSaveWifi() { 
  String s = server.arg("ssid"); String p = server.arg("pass"); 
  if (s.length() > 0) {
    preferences.begin("wifi-creds", false); preferences.putString("ssid", s); preferences.putString("password", p); preferences.end(); 
    WiFi.mode(WIFI_AP_STA); WiFi.begin(s.c_str(), p.c_str());
    int retries = 0; while (WiFi.status() != WL_CONNECTED && retries < 20) { delay(500); retries++; }
    if (WiFi.status() == WL_CONNECTED) {
        configTime(25200, 0, "pool.ntp.org");
        setupFirebase();
        mcpClient.begin(mcpEndpoint, [](bool c){if(c) registerMcpTools();}); 
        server.send(200, "application/json", "{\"status\":\"ok\", \"ip\":\"" + WiFi.localIP().toString() + "\"}");
    } else { server.send(200, "application/json", "{\"status\":\"fail\"}"); }
  } else { server.send(400, "text/plain", "Missing SSID"); }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_RESET_PIN, INPUT_PULLUP);
  Serial1.begin(115200, SERIAL_8N1, RX_FROM_SLAVE, TX_TO_SLAVE);
  
  // KHỞI TẠO CHÂN ĐIỀU KHIỂN BƠM/QUẠT TRÊN MASTER
  pinMode(PIN_FAN_S3, OUTPUT);
  pinMode(PIN_PUMP_S3, OUTPUT);
  // Mặc định tắt (giả sử Relay Active LOW -> HIGH là tắt)
  digitalWrite(PIN_FAN_S3, HIGH);
  digitalWrite(PIN_PUMP_S3, HIGH);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(1); 
  tft.fillScreen(C_BLACK);
  
  pinMode(RAIN_PIN_S3, INPUT_PULLUP);
  dht.begin();
  
  checkResetButton();

  preferences.begin("wifi-creds", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("password", "");
  preferences.end();
  
  if (ssid.length() > 0) {
    tft.fillScreen(C_BLACK); 
    tft.setCursor(10, 40); tft.setTextColor(C_WHITE); tft.setTextSize(1);
    tft.print("Connecting WiFi...");
    tft.setCursor(10, 55); tft.print(ssid);

    WiFi.begin(ssid.c_str(), pass.c_str());
    int t=20; 
    while(WiFi.status() != WL_CONNECTED && t-- >0) { 
        delay(500); 
        checkResetButton(); 
    }
  }
  
  initDashboard(); 
  
  if (WiFi.status() == WL_CONNECTED) {
    configTime(25200, 0, "pool.ntp.org");
    setupFirebase();
    mcpClient.begin(mcpEndpoint, [](bool c){if(c) registerMcpTools();});
    server.on("/", [](){server.send(200,"text/html",htmlControl);});
  } else {
    WiFi.softAP(apSsidDefault, apPassDefault);
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.on("/", [](){server.send(200,"text/html",htmlConfig);});
  }

  server.on("/control", [](){server.send(200,"text/html",htmlControl);});
  server.on("/api", handleApi); server.on("/api_grow", handleApiGrow); server.on("/status", handleStatus);
  server.on("/scanWifi", handleScanWifi); server.on("/saveWifi", handleSaveWifi);
  server.onNotFound([]() { server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true); server.send(302, "text/plain", ""); });
  
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  if(WiFi.status() == WL_CONNECTED) {
    mcpClient.loop();
    sendDataToFirebase();
  }
  
  readDataFromSlave();
  processLogic();
  
  // >>> THỰC THI ĐIỀU KHIỂN BƠM/QUẠT TRỰC TIẾP <<<
  controlDirectActuators();
  
  static unsigned long lastCmd = 0;
  if(millis() - lastCmd > 200) {
    lastCmd = millis();
    sendCommandToSlave();
  }
  
  drawTFT();
  checkResetButton();
  delay(2);
}