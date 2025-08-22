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
#define HUMIDITY_FIRE_PIN 15

// Temperature and Humidity Sensor
#define DHTPIN 13
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// --- MAC Addresses ---
uint8_t npkDeviceMac[] = { 0x5C, 0x01, 0x3B, 0x6D, 0x56, 0x70 };
uint8_t waterDeviceMac[] = { 0xCC, 0xDB, 0xA7, 0x16, 0x18, 0x60 };

// --- Shared Variables ---
String incomingJson = "";
bool hasNewData = false;
unsigned long lastLoRaSendTime = 0;
const unsigned long loraSendInterval = 4000;  // ms

// --- Parsed Data Variables ---
float waterPH = 0, targetPH = 0, mixerLevel = 0, supplyLevel = 0, acidLevel = 0, baseLevel = 0, humidity = 0, temperature = 0;
float rainLevel = 0, soilPH = 0, soilMoisture = 0;
int nitrogen = 0, phosphorus = 0, potassium = 0;

// --- Safe DHT Read ---
void safeReadDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    humidity = h;
    temperature = t;
    Serial.printf("🌡️ Temp: %.2f °C, 💧 Humidity: %.2f %%\n", temperature, humidity);
  } else {
    Serial.println("⚠️ Failed to read from DHT sensor");
  }
}

// --- ESP-NOW JSON Receiver ---
void onReceiveESPNow(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  incomingJson = String((char *)incomingData);
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, incomingJson);

  if (!error) {
    if (doc.containsKey("waterPH")) {
      waterPH = doc["waterPH"];
      targetPH = doc["targetPH"];
      mixerLevel = doc["mixerLevel"];
      supplyLevel = doc["supplyLevel"];
      acidLevel = doc["acidLevel"];
      baseLevel = doc["baseLevel"];
      rainLevel = doc["rainLevel"];
      soilPH = doc["SoilPH"];
      soilMoisture = doc["SoilMoisture"];
      hasNewData = true;
    } else if (doc.containsKey("N")) {
      nitrogen = doc["N"];
      phosphorus = doc["P"];
      potassium = doc["K"];
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

// --- LoRa + JSON + ACK Command Task ---
void PrepareJSONTask(void *pvParameters) {
  unsigned long lastDHTRead = 0;

  while (true) {
    unsigned long now = millis();
    if (now - lastDHTRead > 2500) {
      safeReadDHT();
      lastDHTRead = now;
    }

    if (hasNewData && (now - lastLoRaSendTime >= loraSendInterval)) {
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
                    digitalWrite(FAN_PIN, fanControl["fan_status"] ? HIGH : LOW);
                    digitalWrite(HUMIDITY_FIRE_PIN, fanControl["humidifier_status"] ? LOW : HIGH);
                    Serial.print("Fanpin: ");
                    Serial.print(fanControl["fan_status"] ? HIGH : LOW);
                    Serial.print(" HUMIDITY_FIRE_PIN: ");
                    Serial.println(fanControl["humidifier_status"] ? LOW : HIGH);
                  }

                  if (command.containsKey("NPK_Command")) {

                    String msg = String("{\"command\":\"NPK_Command\",\"value\":") + (command["NPK_Command"] ? "true" : "false") + "}";
                    esp_now_send(npkDeviceMac, (uint8_t *)msg.c_str(), msg.length());
                  }

                  if (command.containsKey("mixer_control") && command.containsKey("suppy_control")) {
                    DynamicJsonDocument doc(128);
                    doc["mixer_control"] = command["mixer_control"];
                    doc["suppy_control"] = command["suppy_control"];

                    String msg;
                    serializeJson(doc, msg);

                    esp_now_send(waterDeviceMac, (uint8_t *)msg.c_str(), msg.length());
                    Serial.println("📤 Sent to water device: " + msg);
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
      lastLoRaSendTime = now;
      hasNewData = false;
    }

    delay(10);
  }
}

void setup() {
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
  delay(2000);  // Allow sensor to stabilize

  xTaskCreatePinnedToCore(ESPNowTask, "ESPNowTask", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(PrepareJSONTask, "PrepareJSONTask", 12288, NULL, 1, NULL, 1);
}

void loop() {
  // Nothing here
}
