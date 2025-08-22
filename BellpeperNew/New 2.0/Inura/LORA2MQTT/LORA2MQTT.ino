#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- WiFi Credentials ---
#define WIFI_SSID "LasithWifi"
#define WIFI_PASSWORD "12345678"

String latestCommand = "";

// --- MQTT Settings ---
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic = "Bellpepergreen/data";
const char* mqtt_client_id = "esp32_lora_receiver";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- LoRa Configuration ---
#define SS 5
#define RST 14
#define DIO0 2

const byte receiverAddress = 0x01;
const byte expectedSender = 0x02;

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("🔌 Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("🔁 Connecting to MQTT...");
    if (mqttClient.connect(mqtt_client_id)) {
      Serial.println("✅ Connected to MQTT broker");
      mqttClient.subscribe("Bellpepergreen/command");
    } else {
      Serial.print("❌ MQTT failed, state=");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("📥 MQTT Command Received: ");
  Serial.println(msg);

  latestCommand = msg;  // ✅ Save the latest command
}


void setup() {
  Serial.begin(9600);

  // Connect WiFi
  connectWiFi();

  // Set up MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);


  // Set up LoRa
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("❌ LoRa init failed!");
    while (1);
  }
  Serial.println("📡 LoRa Receiver + MQTT Ready");
  LoRa.receive();
}

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  int packetSize = LoRa.parsePacket();
  if (packetSize >= 3) {
    byte to = LoRa.read();
    byte from = LoRa.read();
    byte length = LoRa.read(); 

    if (to == receiverAddress && from == expectedSender) {
      String jsonData = "";
      while (LoRa.available()) {
        jsonData += (char)LoRa.read();  // ✅ Now starts from clean JSON
      }
      Serial.print("📥 LoRa JSON Received: ");
      Serial.println(jsonData);

      // --- Parse incoming JSON ---
      StaticJsonDocument<512> dataDoc;
      DeserializationError err = deserializeJson(dataDoc, jsonData);

      if (!err) {
        // --- Forward to MQTT ---
        char mqttPayload[512];
        size_t len = serializeJson(dataDoc, mqttPayload);
        mqttClient.publish(mqtt_topic, mqttPayload, len);
        Serial.println("📤 Forwarded to MQTT:");
        Serial.println(mqttPayload);
      } else {
        Serial.print("❌ JSON parse error: ");
        Serial.println(err.c_str());
      }

      // --- Prepare and send ACK ---
      StaticJsonDocument<512> ackDoc;
      ackDoc["ack"] = true;
      ackDoc["timestamp"] = millis();
      ackDoc["command"] = latestCommand;  // ✅ include stored command

      String jsonAck;
      serializeJson(ackDoc, jsonAck);

      delay(100); // Allow sender to enter receive mode
      LoRa.beginPacket();
      LoRa.write(from);     // To sender
      LoRa.write(to);       // From this node
      LoRa.print(jsonAck);
      LoRa.endPacket();

      Serial.print("📤 Sent ACK: ");
      Serial.println(jsonAck);
      latestCommand="";
    }
  }
}
