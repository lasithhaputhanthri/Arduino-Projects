#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <driver/i2s.h>
#include "DHT.h"
#include "HX711.h"

// ----------- WiFi & MQTT -----------
const char* ssid = "LasithWifi";
const char* password = "12345678";
const char* mqtt_server = "broker.emqx.io";
const char* mqtt_topic_sub = "bee/command";
const char* mqtt_topic_pub = "bee/sensor";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebSocketsClient webSocket;

// ----------- Sound Config -----------
#define I2S_WS 15
#define I2S_SD 12
#define I2S_SCK 13
#define SAMPLE_RATE 16000
bool sendSound = false;

// ----------- DHT Sensors -----------
#define DHTTYPE DHT22
DHT dht1(27, DHTTYPE);
DHT dht2(25, DHTTYPE);
DHT dht3(23, DHTTYPE);
DHT dht4(19, DHTTYPE);

// ----------- HX711 Load Cells -----------
HX711 scale1;
HX711 scale2;
#define LOADCELL_DOUT_PIN1 5
#define LOADCELL_SCK_PIN1 17
#define LOADCELL_DOUT_PIN2 14
#define LOADCELL_SCK_PIN2 26

float scale1_factor = 228.0;  // default calibration
float scale2_factor = 228.0;

unsigned long lastSensorPublish = 0;

// ----------- I2S Config -----------
void initI2SMic() {
  Serial.println("[DEBUG] Initializing I2S mic...");
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_start(I2S_NUM_0);
  Serial.println("[DEBUG] I2S mic initialized.");
}

// ----------- WiFi & MQTT -----------
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[DEBUG] Attempting MQTT connection...");
    if (mqttClient.connect("ESP32_BEE")) {
      Serial.println("connected.");
      mqttClient.subscribe(mqtt_topic_sub);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 2 seconds...");
      delay(2000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[DEBUG] MQTT Message received: ");
  Serial.println((char*)payload);
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (!err && doc.containsKey("sendSound")) {
    sendSound = doc["sendSound"];
    Serial.print("[DEBUG] Updated sendSound to: ");
    Serial.println(sendSound);
  }
  if (doc.containsKey("scale1")) {
    scale1_factor = doc["scale1"];
    scale1.set_scale(scale1_factor);
    Serial.print("[DEBUG] Updated scale1_factor to: ");
    Serial.println(scale1_factor);
  }

  if (doc.containsKey("scale2")) {
    scale2_factor = doc["scale2"];
    scale2.set_scale(scale2_factor);
    Serial.print("[DEBUG] Updated scale2_factor to: ");
    Serial.println(scale2_factor);
  }
}

void setupMQTT() {
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);
}

// ----------- Sensor Read + MQTT Publish -----------
void publishSensorData() {
  Serial.println("[DEBUG] Reading sensor data...");
  StaticJsonDocument<256> doc;
  doc["t1"] = dht1.readTemperature();
  doc["h1"] = dht1.readHumidity();
  doc["t2"] = dht2.readTemperature();
  doc["h2"] = dht2.readHumidity();
  doc["t3"] = dht3.readTemperature();
  doc["h3"] = dht3.readHumidity();
  doc["t4"] = dht4.readTemperature();
  doc["h4"] = dht4.readHumidity();

  float weight1 = scale1.get_units(5);
  float weight2 = scale2.get_units(5);
  Serial.println(weight1);
  Serial.println(weight2);
  doc["weight"] = (weight1 + weight2) / 2.0;

  char buffer[256];
  serializeJson(doc, buffer);
  Serial.print("[DEBUG] Publishing to MQTT: ");
  Serial.println(buffer);
  mqttClient.publish(mqtt_topic_pub, buffer);
}

// ----------- WebSocket -----------
void setupWebSocket() {
  Serial.println("[DEBUG] Connecting to WebSocket...");
  webSocket.beginSSL("abc123.ngrok.io", 443, "/ws");
  webSocket.onEvent(webSocketEvent);
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("[DEBUG] WebSocket connected.");
  } else if (type == WStype_DISCONNECTED) {
    Serial.println("[DEBUG] WebSocket disconnected.");
  } else if (type == WStype_TEXT) {
    Serial.print("[DEBUG] WebSocket text: ");
    Serial.println((char*)payload);
  }
}

// ----------- Setup -----------
void setup() {
  Serial.begin(115200);
  Serial.println("[DEBUG] Starting setup...");
  WiFi.begin(ssid, password);
  Serial.print("[DEBUG] Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[DEBUG] WiFi connected");

  setupMQTT();
  setupWebSocket();
  initI2SMic();

  dht1.begin();
  dht2.begin();
  dht3.begin();
  dht4.begin();

  scale1.begin(LOADCELL_DOUT_PIN1, LOADCELL_SCK_PIN1);
  scale2.begin(LOADCELL_DOUT_PIN2, LOADCELL_SCK_PIN2);
  scale1.set_scale();
  scale1.tare();
  scale2.set_scale();
  scale2.tare();

  scale1.set_scale(scale1_factor);
  scale1.tare();

  scale2.set_scale(scale2_factor);
  scale2.tare();


  Serial.println("[DEBUG] Setup complete.");
}

// ----------- Loop -----------
void loop() {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();
  webSocket.loop();

  // Publish sensors every 4 seconds
  if (millis() - lastSensorPublish > 4000) {
    publishSensorData();
    lastSensorPublish = millis();
  }

  // Stream audio if flag is true
  if (sendSound) {
    uint8_t i2s_data[256];
    size_t bytes_read;
    i2s_read(I2S_NUM_0, &i2s_data, sizeof(i2s_data), &bytes_read, portMAX_DELAY);
    if (bytes_read > 0) {
      Serial.print("[DEBUG] Sending audio bytes: ");
      Serial.println(bytes_read);
      webSocket.sendBIN(i2s_data, bytes_read);
    }
  }
}
