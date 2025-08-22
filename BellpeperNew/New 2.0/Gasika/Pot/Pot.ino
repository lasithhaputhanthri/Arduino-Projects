#include <ModbusMaster.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// RS485 UART and control pins
#define RS485_RX 3  // RO (to ESP32)
#define RS485_TX 27  // DI (from ESP32)
#define RS485_RE 33  // Receiver Enable
#define RS485_DE 22  // Driver Enable

// Soil moisture analog pin
#define SOIL_PIN 34

HardwareSerial RS485Serial(2);  // UART2
ModbusMaster node;

// Receiver MAC address (update this)
uint8_t receiverMAC[] = {0xCC, 0xDB, 0xA7, 0x16, 0x18, 0x60};

// JSON buffer
char jsonBuffer[128];

void preTransmission() {
  digitalWrite(RS485_RE, HIGH);  // Disable receiver
  digitalWrite(RS485_DE, HIGH);  // Enable driver
}

void postTransmission() {
  digitalWrite(RS485_DE, LOW);   // Disable driver
  digitalWrite(RS485_RE, LOW);   // Enable receiver
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
  }
}

void setup() {
  Serial.begin(115200);

  // RS485 control pins
  pinMode(RS485_RE, OUTPUT);
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_RE, LOW);  // Enable receiver by default
  digitalWrite(RS485_DE, LOW);  // Disable driver by default

  // UART init
  RS485Serial.begin(4800, SERIAL_8N1, RS485_RX, RS485_TX);
  node.begin(1, RS485Serial);  // Slave ID 1
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  // Soil moisture
  pinMode(SOIL_PIN, INPUT);

  // ESP-NOW setup
  setupEspNow();
  delay(200);  // allow UART hardware to stabilize

}

void loop() {
  float ph_value = -1;

  // Read pH sensor via Modbus
  if (node.readInputRegisters(0x0000, 1) == node.ku8MBSuccess) {
    uint16_t raw = node.getResponseBuffer(0);
    ph_value = raw / 10.0;
    Serial.print("pH: ");
    Serial.println(ph_value);
  } else {
    Serial.println("Modbus pH read failed");
  }

  // Read soil moisture
  int moisture_raw = analogRead(SOIL_PIN);
  float moisture_percent = map(moisture_raw, 3000, 800, 0, 100);  // Adjust as needed
  moisture_percent = constrain(moisture_percent, 0, 100);
  Serial.print("Soil Moisture: ");
  Serial.print(moisture_percent);
  Serial.println("%");

  // Create and send JSON
  StaticJsonDocument<128> doc;
  doc["ph"] = ph_value;
  doc["moisture"] = moisture_percent;
  serializeJson(doc, jsonBuffer);

  Serial.print("Sending JSON: ");
  Serial.println(jsonBuffer);

  esp_now_send(receiverMAC, (uint8_t *)jsonBuffer, strlen(jsonBuffer) + 1);

  delay(500);
}