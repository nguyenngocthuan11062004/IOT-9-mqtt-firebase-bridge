#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

// --- 1. CẤU HÌNH WIFI & MQTT ---
const char* ssid = "your_wifi";      
const char* password = "your_pass"; 

const char* mqtt_server = "MQTT_HOST";
const int mqtt_port = 8883;
const char* mqtt_username = "MQTT_USERNAME"; 
const char* mqtt_password = "MQTT_PASSWORD"; 

// --- 2. CẤU HÌNH PHẦN CỨNG ---
// Relay
const int relayPin = 26; // GPIO 26 điều khiển Relay

// DHT22
#define DHTPIN 4        
#define DHTTYPE DHT22     
DHT dht(DHTPIN, DHTTYPE);

// Rain Sensor
const int rainAnalogPin = 35;  
const int rainDigitalPin = 23; 

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// --- 3. KHỞI TẠO ĐỐI TƯỢNG NETWORK ---
WiFiClientSecure espClient;
PubSubClient client(espClient);

unsigned long lastMsg = 0; 
const long interval = 2000; // Chu kỳ đọc và gửi dữ liệu (5 giây)

// --- HÀM KẾT NỐI WIFI ---
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connecting.");
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK!");
  delay(1000);
}

// --- HÀM CALLBACK (XỬ LÝ TIN NHẮN) ---
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // Kiểm tra nếu tin nhắn đến từ topic điều khiển Relay
  if (String(topic) == "esp32/relay") {
    // --- ĐÃ SỬA: LOGIC ĐẢO NGƯỢC ---
    if (message == "ON") {
      digitalWrite(relayPin, LOW); // Mức LOW để BẬT Relay (Kích âm)
      Serial.println("RELAY ON (Active Low)");
    } 
    else if (message == "OFF") {
      digitalWrite(relayPin, HIGH);  // Mức HIGH để TẮT Relay
      Serial.println("RELAY OFF (Active Low)");
    }
  }
}

// --- HÀM KẾT NỐI LẠI MQTT ---
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientID = "ESP32Client-";
    clientID += String(random(0xffff), HEX);
    
    if (client.connect(clientID.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected");
      lcd.setCursor(0, 1);
      lcd.print("MQTT Connected");
      
      client.subscribe("esp32/relay"); 
      Serial.println("Subscribed to: esp32/relay");

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // 1. Cấu hình Relay
  pinMode(relayPin, OUTPUT);
  // --- ĐÃ SỬA: Mặc định HIGH để Relay TẮT khi khởi động ---
  digitalWrite(relayPin, HIGH); 

  // 2. Khởi tạo LCD
  lcd.init();      
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("System Start...");

  // 3. Khởi tạo Cảm biến
  dht.begin();
  pinMode(rainAnalogPin, INPUT);
  pinMode(rainDigitalPin, INPUT);

  // 4. Khởi tạo Network
  setup_wifi();
  
  espClient.setInsecure(); 
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback); 
}
String lastRelayState = "OFF";  // Mặc định là OFF khi khởi động

void loop() {
  // Kiểm tra kết nối MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > interval) {
    lastMsg = now;

    // A. Đọc cảm biến (độ ẩm, nhiệt độ, trạng thái mưa)
    float h = dht.readHumidity();   // Đọc độ ẩm
    float t = dht.readTemperature(); // Đọc nhiệt độ
    int analogRain = analogRead(rainAnalogPin);  // Đọc cảm biến mưa analog
    int digitalRain = digitalRead(rainDigitalPin); // Đọc cảm biến mưa kỹ thuật số

    // Kiểm tra lỗi nếu giá trị cảm biến không hợp lệ
    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // B. Hiển thị LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("T:"); lcd.print((int)t); lcd.print("C H:"); lcd.print((int)h); lcd.print("%");
    
    lcd.setCursor(0, 1);
    if (digitalRead(relayPin) == LOW) lcd.print("RL:ON ");
    else lcd.print("RL:OFF ");
    
    if (digitalRain == LOW) lcd.print("MUA!");
    else lcd.print("Tanh");

    // C. Gửi dữ liệu lên MQTT (nếu có thay đổi)
    client.publish("esp32/temperature", String(t, 1).c_str());
    client.publish("esp32/humidity", String(h, 1).c_str());

    String rainStatus;
    if (digitalRain == LOW) {
      if (analogRain < 1500) rainStatus = "Heavy Rain";
      else rainStatus = "Raining";
    } else {
      rainStatus = "No Rain";
    }
    client.publish("esp32/rain_status", rainStatus.c_str());

    // Kiểm tra và chỉ gửi trạng thái relay khi có thay đổi
    String currentRelayState = (digitalRead(relayPin) == LOW) ? "ON" : "OFF";

    // Nếu trạng thái relay thay đổi, gửi lên MQTT
    if (currentRelayState != lastRelayState) {
      client.publish("esp32/relay_state", currentRelayState.c_str());
      lastRelayState = currentRelayState; // Cập nhật giá trị trạng thái relay đã gửi
    }
  }
}