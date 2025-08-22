#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <esp_sleep.h>

// Debug macro
#define DEBUG(x) Serial.println(x)

// LoRa and DHT configuration
#define SS      5
#define RST     14
#define DIO0    2
#define LocalAddress 0x02
#define Destination_Master 0x01

#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ESP-NOW data structure
typedef struct struct_message {
  float waterPH;
  float targetPH;
  float mixerLevel;
  float supplyLevel;
  float rainLevel;
  float SoilPH;
  float SoilMoisture;
} struct_message;

volatile struct_message receivedData;
volatile bool hasNewData = false;

// Replace with actual peer MAC address
uint8_t tankDeviceMac[] = {0x24, 0x6F, 0x28, 0xA1, 0xB2, 0xC3};

// ESP-NOW receive callback
void onReceiveESPNow(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(receivedData)) {
    memcpy((void*)&receivedData, incomingData, sizeof(receivedData));
    hasNewData = true;
    DEBUG("\u2705 ESP-NOW Data Received");
  }
}

// ESP-NOW listening task on Core 0
void ESPNowTask(void *pvParameters) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) {
    DEBUG("\u274C ESP-NOW init failed");
    vTaskDelete(NULL);
  }
  esp_now_register_recv_cb(onReceiveESPNow);

  // Add specific peer (not broadcast)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, tankDeviceMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  while (true) {
    delay(100);
  }
}

// LoRa send function
bool sendLoRaMessage(String message, byte destination) {
  LoRa.beginPacket();
  LoRa.write(destination);
  LoRa.write(LocalAddress);
  LoRa.write(message.length());
  LoRa.print(message);
  LoRa.endPacket();
  LoRa.receive();
  return true;
}

// LoRa handling and sleep task on Core 1
void LoRaTask(void *pvParameters) {
  while (true) {
    if (hasNewData) {
      float H = dht.readHumidity();
      float T = dht.readTemperature();

      DynamicJsonDocument doc(512);
      doc["Humidity"] = H;
      doc["Temperature"] = T;

      JsonObject plant = doc.createNestedObject("GreenHouse1");
      plant["waterPH"] = receivedData.waterPH;
      plant["targetPH"] = receivedData.targetPH;
      plant["mixerLevel"] = receivedData.mixerLevel;
      plant["supplyLevel"] = receivedData.supplyLevel;
      plant["rainLevel"] = receivedData.rainLevel;
      plant["SoilPH"] = receivedData.SoilPH;
      plant["SoilMoisture"] = receivedData.SoilMoisture;

      String json;
      serializeJson(doc, json);

      DEBUG("\ud83d\udce1 Sending via LoRa:");
      DEBUG(json);

      sendLoRaMessage(json, Destination_Master);
      hasNewData = false;
    }

    // Sleep setup
    esp_sleep_enable_timer_wakeup(4000000); // 4 seconds
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 1); // LoRa DIO0
    DEBUG("\ud83d\udca4 Light sleep...");
    delay(50);
    esp_light_sleep_start();

    // Check for incoming LoRa
    if (LoRa.parsePacket()) {
      byte sender = LoRa.read();
      byte recipient = LoRa.read();
      byte length = LoRa.read();

      if (recipient == LocalAddress) {
        String msg = "";
        while (LoRa.available()) msg += (char)LoRa.read();
        DEBUG("\ud83d\udce8 LoRa RX: " + msg);

        // Forward message over ESP-NOW to specific device
        if (esp_now_send(tankDeviceMac, (uint8_t*)msg.c_str(), msg.length()) == ESP_OK) {
          DEBUG("\u27a1\ufe0f Forwarded LoRa data to tank device via ESP-NOW");
        } else {
          DEBUG("\u274c Failed to forward LoRa data over ESP-NOW");
        }

        if (msg == "PING") {
          sendLoRaMessage("ACK", sender);
        }
      }
    }
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  dht.begin();

  // Init LoRa
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(433E6)) {
    DEBUG("\u274C LoRa init failed");
    while (true);
  }
  LoRa.receive();

  // Launch tasks
  xTaskCreatePinnedToCore(ESPNowTask, "ESPNowTask", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(LoRaTask, "LoRaTask", 8192, NULL, 1, NULL, 1);
}

void loop() {
  // Not used
}
