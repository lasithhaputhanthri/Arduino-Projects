#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define SS 5
#define RST 14
#define DIO0 2

const char* ssid = "LasithWifi";
const char* password = "12345678";
const char* serverUrl = "http://192.168.1.162:8000/predict";

WiFiClient client;
HTTPClient http;

byte myAddress = 0xAF;
String receivedJson = "";

struct SensorData {
  float HR;
  float SPO2;
  float gyro_x, gyro_y, gyro_z;
  float acceleration_x, acceleration_y, acceleration_z;
};

SensorData dataBuffer[15]; 
int dataIndex = 0;

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi!");

  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa initialization failed!");
    while (1);
  }
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    int recipient = LoRa.read();
    byte sender = LoRa.read();
    byte dataLength = LoRa.read();

    receivedJson = "";
    while (LoRa.available()) {
      receivedJson += (char)LoRa.read();
    }

    if (dataLength != receivedJson.length()) {
      Serial.println("Error: Packet length mismatch!");
      return;
    }

    if (recipient != myAddress) {
      Serial.println("Message not for this device. Ignoring.");
      return;
    }

    Serial.print("Received via LoRa: ");
    Serial.println(receivedJson);

    // Parse the JSON string
    StaticJsonDocument<200> jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, receivedJson);

    if (error) {
      Serial.print("Failed to parse JSON: ");
      Serial.println(error.c_str());
      return;
    }

    collectSensorData(jsonDoc);
    if (dataIndex >= 15) {
      sendSensorData();
      dataIndex = 0;  // Reset buffer after sending
    }
  }
}

void sendSensorData() {  

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping data send.");
    return;
  }

  StaticJsonDocument<1024> jsonDoc;
  jsonDoc["elephant_id"] = "elephant_001";  // Add elephant_id

  JsonArray samplesArray = jsonDoc.createNestedArray("samples");

  for (int i = 0; i < 15; i++) {
    JsonObject sampleObj = samplesArray.createNestedObject();
    sampleObj["HR"] = dataBuffer[i].HR;
    sampleObj["SPO2"] = dataBuffer[i].SPO2;
    sampleObj["gyro_x"] = dataBuffer[i].gyro_x;
    sampleObj["gyro_y"] = dataBuffer[i].gyro_y;
    sampleObj["gyro_z"] = dataBuffer[i].gyro_z;
    sampleObj["acceleration_x"] = dataBuffer[i].acceleration_x;
    sampleObj["acceleration_y"] = dataBuffer[i].acceleration_y;
    sampleObj["acceleration_z"] = dataBuffer[i].acceleration_z;
  }

  String jsonString;
  serializeJson(jsonDoc, jsonString);

  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonString);
  Serial.println("Sending Data...");
  Serial.println(jsonString);

  if (httpResponseCode > 0) {
    Serial.println("Response Code: " + String(httpResponseCode));
    Serial.println(http.getString());
  }

  http.end();
}
void collectSensorData(StaticJsonDocument<200> jsonDoc) {
  SensorData newData;
  newData.HR = jsonDoc["HR"];
  newData.SPO2 = jsonDoc["SPO2"];
  newData.gyro_x = jsonDoc["gyro_x"];
  newData.gyro_y = jsonDoc["gyro_y"];
  newData.gyro_z = jsonDoc["gyro_z"];
  newData.acceleration_x = jsonDoc["acceleration_x"];
  newData.acceleration_y = jsonDoc["acceleration_y"];
  newData.acceleration_z = jsonDoc["acceleration_z"];

  dataBuffer[dataIndex++] = newData;
}