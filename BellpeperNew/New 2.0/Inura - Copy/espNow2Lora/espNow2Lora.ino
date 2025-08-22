#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include "DHT.h"

#define DEBUG(x) Serial.println(x)

// --- LoRa Configuration ---
#define SS 5
#define RST 14
#define DIO0 2
#define LocalAddress 0x02
#define Destination_Master 0x01

// --- Fan & Humidifier Pins ---
#define FAN_PIN 32
#define HUMIDITY_FIRE_PIN 33

// Temperature and Humidity Sensor
#define DHTPIN 13
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// --- MAC Addresses ---
uint8_t npkDeviceMac[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01 };    // ✅ Target for NPK command
uint8_t waterDeviceMac[] = { 0xCC, 0xDB, 0xA7, 0x16, 0x18, 0x60 };  // ✅ Target for water control

// --- Shared Variables ---
String incomingJson = "";
bool hasNewData = false;
unsigned long lastLoRaSendTime = 0;
const unsigned long loraSendInterval = 4000;  // ms

// --- Parsed Data Variables ---
float waterPH = 0, targetPH = 0, mixerLevel = 0, supplyLevel = 0, acidLevel = 0, baseLevel = 0,humidity=0,temperature=0;
float rainLevel = 0, soilPH = 0, soilMoisture = 0;
int nitrogen = 0, phosphorus = 0, potassium = 0;

// --- ESP-NOW JSON Receiver ---
void onReceiveESPNow(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  incomingJson = String((char *)incomingData);
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, incomingJson);

  if (!error) {
    if (doc.containsKey("waterPH")) {
      waterPH = doc["waterPH"] ;
      targetPH = doc["targetPH"] ;
      mixerLevel = doc["mixerLevel"] ;
      supplyLevel = doc["supplyLevel"];
      acidLevel = doc["acidLevel"] ;
      baseLevel = doc["baseLevel"] ;
      rainLevel = doc["rainLevel"] ;
      soilPH = doc["SoilPH"] ;
      soilMoisture = doc["SoilMoisture"] ;
      hasNewData = true;
    } else if (doc.containsKey("N")) {
      nitrogen = doc["N"] ;
      phosphorus = doc["P"] ;
      potassium = doc["K"] ;
    }
  } else {
    Serial.print("[ESP-NOW] JSON parse error: ");
    Serial.println(error.c_str());
  }
}

// --- ESP-NOW Task ---
void ESPNowTask(void *pvParameters) {
  Serial.println("ESPNowTask");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed");
    vTaskDelete(NULL);
  }

  esp_now_register_recv_cb(onReceiveESPNow);

  esp_now_peer_info_t npkPeer = {};
  memcpy(npkPeer.peer_addr, npkDeviceMac, 6);
  npkPeer.channel = 0;
  npkPeer.encrypt = false;
  esp_now_add_peer(&npkPeer);

  esp_now_peer_info_t waterPeer = {};
  memcpy(waterPeer.peer_addr, waterDeviceMac, 6);
  waterPeer.channel = 0;
  waterPeer.encrypt = false;
  esp_now_add_peer(&waterPeer);

  while (true) delay(100);
}

// --- JSON Preparation + LoRa + ACK Command Handler ---
void PrepareJSONTask(void *pvParameters) {
  while (true) {
    Serial.println("PrepareJSONTask");
    humidity = dht.readHumidity();
    temperature = dht.readTemperature();
    Serial.println("sensor data");
    unsigned long currentMillis = millis();

    if (hasNewData && (currentMillis - lastLoRaSendTime >= loraSendInterval)) {
      DynamicJsonDocument doc(512);
      JsonObject plant = doc.createNestedObject("GreenHouse1");

      plant["Humidity"] = humidity;
      plant["Temperature"] = temperature;
      plant["waterPH"] = waterPH;
      plant["targetPH"] = targetPH;
      plant["mixerLevel"] = mixerLevel;
      plant["supplyLevel"] = supplyLevel;
      plant["rainLevel"] = rainLevel;
      plant["acidLevel"] = acidLevel;
      plant["baseLevel"] = baseLevel;
      plant["SoilPH"] = soilPH;
      plant["SoilMoisture"] = soilMoisture;
      plant["N"] = nitrogen;
      plant["P"] = phosphorus;
      plant["K"] = potassium;

      String json;
      serializeJson(doc, json);

      DEBUG("[LoRa] Sending JSON:");
      DEBUG(json);

      LoRa.beginPacket();
      LoRa.write(Destination_Master);
      LoRa.write(LocalAddress);
      LoRa.write(json.length());
      LoRa.print(json);
      LoRa.endPacket();
      LoRa.receive();

      bool ackReceived = false;
      unsigned long startTime = millis();
      while (millis() - startTime < 2000) {
        int packetSize = LoRa.parsePacket();
        if (packetSize >= 3) {
          byte to = LoRa.read();
          byte from = LoRa.read();
          byte length = LoRa.read();

          if (to == LocalAddress && from == Destination_Master) {
            String ack = "";
            while (LoRa.available()) ack += (char)LoRa.read();

            Serial.print("✅ ACK Received: ");
            Serial.println(ack);

            String fixedAck = "{" + ack + "}";
            StaticJsonDocument<256> ackDoc;
            DeserializationError ackErr = deserializeJson(ackDoc, fixedAck);

            if (ackErr) {
              Serial.print("❌ Failed to parse ACK JSON: ");
              Serial.println(ackErr.c_str());
              Serial.println("Raw ACK content:");
              Serial.println(ack);
            } else {
              if (!ackDoc.containsKey("command")) {
                Serial.println("⚠️ ACK JSON has no 'command' key");
                serializeJsonPretty(ackDoc, Serial);
                Serial.println();
              } else {
                String commandStr = ackDoc["command"].as<String>();
                Serial.print("📦 Raw command string: ");
                Serial.println(commandStr);

                StaticJsonDocument<512> commandDoc;
                DeserializationError cmdErr = deserializeJson(commandDoc, commandStr);
                if (!cmdErr) {
                  JsonObject command = commandDoc.as<JsonObject>();

                  if (command.containsKey("Fan_Control")) {
                    JsonObject fanControl = command["Fan_Control"];
                    bool fanStatus = fanControl["fan_status"];
                    bool humidStatus = fanControl["humidifier_status"];
                    digitalWrite(FAN_PIN, fanStatus ? HIGH : LOW);
                    digitalWrite(HUMIDITY_FIRE_PIN, humidStatus ? HIGH : LOW);
                    Serial.printf("🔧 Fan turned %s\n", fanStatus ? "ON" : "OFF");
                    Serial.printf("💨 Humidifier turned %s\n", humidStatus ? "ON" : "OFF");
                  }
                  if (command.containsKey("NPK_Command")) {
                    bool npkCmd = command["NPK_Command"];
                    String msg = String("{\"command\":\"NPK_Command\",\"value\":") + (npkCmd ? "true" : "false") + "}";
                    esp_now_send(npkDeviceMac, (uint8_t *)msg.c_str(), msg.length());
                    Serial.print("📡 Sent to NPK device: ");
                    Serial.println(msg);
                  }
                  if (command.containsKey("water_control")) {
                    bool waterCmd = command["water_control"];
                    String msg = String("{\"command\":\"water_Control\",\"value\":") + (waterCmd ? "true" : "false") + "}";
                    esp_now_send(waterDeviceMac, (uint8_t *)msg.c_str(), msg.length());
                    Serial.print("📡 Sent to Water device: ");
                    Serial.println(msg);
                  }
                  ackReceived = true;
                } else {
                  Serial.print("❌ Failed to parse command JSON: ");
                  Serial.println(cmdErr.c_str());
                }
              }
            }
          }
        }
      }

      if (!ackReceived) Serial.println("❌ No ACK received.");
      lastLoRaSendTime = currentMillis;
      hasNewData = false;
    }
    delay(10);
  }
}

void setup() {
  setCpuFrequencyMhz(80); 
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(HUMIDITY_FIRE_PIN, OUTPUT);
  pinMode(DHTPIN, INPUT);

  digitalWrite(FAN_PIN, LOW);
  digitalWrite(HUMIDITY_FIRE_PIN, LOW);

  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("❌ LoRa init failed!");
    while (1)
      ;
  }
  Serial.println("📡 LoRa Initialized");
  LoRa.receive();

  dht.begin();
  Serial.println("dht Initialized");

  xTaskCreatePinnedToCore(ESPNowTask, "ESPNowTask", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(PrepareJSONTask, "PrepareJSONTask", 8192, NULL, 1, NULL, 1);
}

void loop() {
  // not used
}
