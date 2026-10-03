#include <ArduinoJson.h> 

// ==========================================
// CẤU HÌNH CHÂN SLAVE
// ==========================================
// Slave RX (16) <--> Master TX (17)
// Slave TX (17) <--> Master RX (18)

// 1. CẢM BIẾN
#define MOISTURE_SENSOR_PIN 15  // Analog Input (ADC1)
#define LIGHT_SENSOR_PIN    35  // Analog Input (ADC1)

// 2. THIẾT BỊ KHÁC (Đèn phòng vẫn giữ ở Slave)
#define PIN_DEN_PHONG       19

// 3. ĐÈN RGB
#define RGB_PIN_R           21
#define RGB_PIN_G           22
#define RGB_PIN_B           23
#define PWM_FREQ            5000
#define PWM_RES             8

// 4. ĐỘNG CƠ MÁI CHE (L298N B)
#define ROOF_IN3            25
#define ROOF_IN4            26
#define ROOF_ENB            27
const int ROOF_SPEED = 255;
const int ROOF_RUN_TIME = 3500; // Thời gian chạy (ms)

// 5. ĐỘNG CƠ CỬA (L298N A) & SIÊU ÂM
#define DOOR_IN1            12
#define DOOR_IN2            14
#define DOOR_ENA            13
#define TRIG_PIN            2  
#define ECHO_PIN            5        
const int DOOR_SPEED = 255;
const int DOOR_RUN_TIME = 1000; // Thời gian chạy (ms)

// --- BIẾN TOÀN CỤC ---
unsigned long lastSend = 0;
int doorState = 0; // 0: Dừng, 1: Mở, 2: Đóng
int roofState = 0; // 0: Dừng, 1: Mở, 2: Đóng
unsigned long doorTimer = 0; 
unsigned long roofTimer = 0;
bool isDoorOpen = false;
bool isRoofOpen = true; // Mặc định khởi động là đang mở

int curSoil = 0;
int curLight = 0;
long curDist = 0;

// --- BIẾN CHO CHẾ ĐỘ RAINBOW (MỚI THÊM) ---
bool isRainbowMode = false;      // Cờ bật/tắt chế độ cầu vồng
unsigned long lastRainbowUpdate = 0;
int rainbowHue = 0;              // Giá trị màu xoay vòng (0-255)

// --- HÀM BỘ LỌC TRUNG VỊ (MEDIAN FILTER) ---
int getMedianAnalog(int pin) {
  const int numReadings = 15; 
  int values[numReadings];
  for (int i = 0; i < numReadings; i++) {
    values[i] = analogRead(pin);
    delay(2); 
  }
  // Sắp xếp mảng
  for (int i = 0; i < numReadings - 1; i++) {
    for (int j = 0; j < numReadings - i - 1; j++) {
      if (values[j] > values[j + 1]) {
        int temp = values[j];
        values[j] = values[j + 1];
        values[j + 1] = temp;
      }
    }
  }
  return values[numReadings / 2];
}

// --- HÀM ĐIỀU KHIỂN ---
void setDoor(int state) {
  if (state == 0) { digitalWrite(DOOR_IN1, LOW); digitalWrite(DOOR_IN2, LOW); analogWrite(DOOR_ENA, 0); }
  else if (state == 1) { digitalWrite(DOOR_IN1, HIGH); digitalWrite(DOOR_IN2, LOW); analogWrite(DOOR_ENA, DOOR_SPEED); }
  else if (state == 2) { digitalWrite(DOOR_IN1, LOW); digitalWrite(DOOR_IN2, HIGH); analogWrite(DOOR_ENA, DOOR_SPEED); }
}

void setRoof(int state) {
  if (state == 0) { digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, LOW); analogWrite(ROOF_ENB, 0); }
  else if (state == 1) { digitalWrite(ROOF_IN3, HIGH); digitalWrite(ROOF_IN4, LOW); analogWrite(ROOF_ENB, ROOF_SPEED); }
  else if (state == 2) { digitalWrite(ROOF_IN3, LOW); digitalWrite(ROOF_IN4, HIGH); analogWrite(ROOF_ENB, ROOF_SPEED); }
}

void setRGB(int r, int g, int b) {
  // Nếu dùng LED Common Anode (Dương chung) thì dùng: 255 - r
  // Nếu dùng LED Common Cathode (Âm chung) thì dùng: r
  ledcWrite(RGB_PIN_R, 255 - r); 
  ledcWrite(RGB_PIN_G, 255 - g);
  ledcWrite(RGB_PIN_B, 255 - b);
}

// --- HÀM CHẠY HIỆU ỨNG RAINBOW (MỚI THÊM) ---
void runRainbowEffect() {
  // Cập nhật màu mỗi 20ms để hiệu ứng mượt mà
  if (millis() - lastRainbowUpdate > 20) { 
    lastRainbowUpdate = millis();
    rainbowHue++;
    if (rainbowHue > 255) rainbowHue = 0;

    // Thuật toán chuyển đổi Hue (0-255) sang RGB
    byte WheelPos = rainbowHue;
    byte r, g, b;
    if(WheelPos < 85) {
      r = WheelPos * 3;
      g = 255 - WheelPos * 3;
      b = 0;
    } else if(WheelPos < 170) {
      WheelPos -= 85;
      r = 255 - WheelPos * 3;
      g = 0;
      b = WheelPos * 3;
    } else {
      WheelPos -= 170;
      r = 0;
      g = WheelPos * 3;
      b = 255 - WheelPos * 3;
    }
    // Gọi lại hàm setRGB sẵn có (nó sẽ tự xử lý Common Anode/Cathode)
    setRGB(r, g, b); 
  }
}

long readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 25000); 
  if (duration == 0) return 999;
  return duration * 0.0343 / 2;
}

void setup() {
  Serial.begin(115200); 
  // Giao tiếp UART với Master
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  pinMode(PIN_DEN_PHONG, OUTPUT);
  pinMode(MOISTURE_SENSOR_PIN, INPUT);
  pinMode(LIGHT_SENSOR_PIN, INPUT);

  pinMode(DOOR_IN1, OUTPUT); pinMode(DOOR_IN2, OUTPUT); pinMode(DOOR_ENA, OUTPUT);
  pinMode(ROOF_IN3, OUTPUT); pinMode(ROOF_IN4, OUTPUT); pinMode(ROOF_ENB, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);

  ledcAttach(RGB_PIN_R, PWM_FREQ, PWM_RES);
  ledcAttach(RGB_PIN_G, PWM_FREQ, PWM_RES);
  ledcAttach(RGB_PIN_B, PWM_FREQ, PWM_RES);

  setDoor(0); setRoof(0);
}

void loop() {
  // 1. NHẬN LỆNH TỪ MASTER
  while (Serial2.available()) {
    String input = Serial2.readStringUntil('\n');
    DynamicJsonDocument doc(512); 
    DeserializationError error = deserializeJson(doc, input);

    if (!error) {
      // Nhận lệnh đèn phòng
      if(doc.containsKey("roomL")) digitalWrite(PIN_DEN_PHONG, doc["roomL"] ? HIGH : LOW);
      
      // --- XỬ LÝ LED RGB & RAINBOW ---
      // Trường hợp 1: Nhận lệnh bật/tắt Rainbow
      if(doc.containsKey("rainbow")) {
         if (doc["rainbow"] == 1) {
            isRainbowMode = true; // Bật chế độ tự động đổi màu
         } else {
            isRainbowMode = false; // Tắt
         }
      }

      // Trường hợp 2: Nhận lệnh chỉnh màu cụ thể (R, G, B)
      // Khi chỉnh màu thủ công -> Phải tắt Rainbow ngay
      if(doc.containsKey("r")) {
         isRainbowMode = false; 
         setRGB(doc["r"], doc["g"], doc["b"]);
      }
      // -------------------------------

      // Nhận lệnh Cửa
      if(doc.containsKey("dCmd")) {
        int cmd = doc["dCmd"]; 
        if(cmd != 0 && doorState != cmd) { 
            doorState = cmd; 
            doorTimer = millis(); 
            setDoor(doorState); 
        }
      }
      
      // Nhận lệnh Mái che
      if(doc.containsKey("rCmd")) {
        int cmd = doc["rCmd"];
        if(cmd != 0 && roofState != cmd) { 
            roofState = cmd; 
            roofTimer = millis(); 
            setRoof(roofState); 
        }
      }
    }
  }

  // 2. SAFETY CHECK (Dừng động cơ khi hết giờ)
  if (doorState != 0 && millis() - doorTimer > DOOR_RUN_TIME) {
    setDoor(0); 
    isDoorOpen = (doorState == 1); 
    doorState = 0;
  }
  
  if (roofState != 0 && millis() - roofTimer > ROOF_RUN_TIME) {
    setRoof(0); 
    isRoofOpen = (roofState == 1); 
    roofState = 0;
  }

  // 3. ĐỌC CẢM BIẾN
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 200) { 
     lastSensorRead = millis();
     curSoil = getMedianAnalog(MOISTURE_SENSOR_PIN);
     delay(10); 
     curLight = getMedianAnalog(LIGHT_SENSOR_PIN);
     curDist = readUltrasonic();
  }

  // 4. GỬI DỮ LIỆU SANG MASTER
  if (millis() - lastSend > 200) {
    lastSend = millis();
    StaticJsonDocument<512> json;
    
    json["sRaw"] = curSoil;
    json["lRaw"] = curLight;
    json["dist"] = curDist;
    json["dOp"] = isDoorOpen;
    json["rOp"] = isRoofOpen;

    String output;
    serializeJson(json, output);
    Serial2.println(output); 
  }

  // 5. CHẠY HIỆU ỨNG RAINBOW (NẾU ĐƯỢC BẬT)
  if (isRainbowMode) {
    runRainbowEffect();
  }
}