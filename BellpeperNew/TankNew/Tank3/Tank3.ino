#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <Wire.h>

// Debugging
#define DEBUG 1
#if DEBUG
  #define DEBUG_PRINT(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
#endif

// Sensor and relay pin definitions
#define PH_SENSOR_PIN 39
#define ACID_PUMP_PIN 25
#define BASE_PUMP_PIN 17

#define fan_pin 21

#define RELAY_MIXER 22
#define RELAY_SUPPLY 33
#define RELAY_RAIN 32

#define SENSOR_MIXER 12
#define SENSOR_SUPPLY 27
#define SENSOR_RAIN 16

#define ss 5
#define rst 14
#define dio0 2
#define LocalAddress 0x02
#define Destination_Master 0x01

#define TANK_HEIGHT 25
#define MAX_DISTANCE 200

#define MIXER_LOW_THRESHOLD 70
#define MIXER_HIGH_THRESHOLD 90
#define RAIN_LOW_THRESHOLD 10

// ✅ pH Sensor Calibration
#define PH4_VOLTAGE 1.95
#define PH7_VOLTAGE 1.725

// ✅ DHT11 Sensor
#define DHTPIN 3
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

float humidity = 0, temperature = 0;
unsigned long lastPumpAction = 0;

// ✅ PID parameters
double Kp = 100, Ki = 0.1, Kd = 0.5;
double pH_Value, PID_Output, Target_pH = 5.0;
double pH_Error = 0, prev_pH_Error = 0, integral = 0;
bool espNowDataReceived = false;

bool HF;
bool fanState;

// ✅ Motor ON Time Limits for pH Pumps
#define MIN_ON_TIME 1000   // 1 sec minimum
#define MAX_ON_TIME 5000   // 5 sec maximum

// ✅ ESP-NOW Data Structure
typedef struct struct_message {
    float soilMoisture;
    float externalTemp;
} struct_message;

struct_message receivedData;

// ✅ ESP-NOW Callback Function (Fixed)
void onReceiveESPNow(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {

    memcpy(&receivedData, incomingData, sizeof(receivedData));

    DEBUG_PRINT("✅ ESP-NOW Data Received:");
    DEBUG_PRINT("🌱 Soil Moisture: " + String(76));
    DEBUG_PRINT("🌡️ External Temp: " + String(28));

    espNowDataReceived = true;
}

// ✅ Function to Read pH Sensor
float readPH() {
    int buffer_arr[10];
    unsigned long avgval = 0;

    for (int i = 0; i < 10; i++) {
        buffer_arr[i] = analogRead(PH_SENSOR_PIN);
        delay(30);
    }

    for (int i = 0; i < 9; i++) {
        for (int j = i + 1; j < 10; j++) {
            if (buffer_arr[i] > buffer_arr[j]) {
                int temp = buffer_arr[i];
                buffer_arr[i] = buffer_arr[j];
                buffer_arr[j] = temp;
            }
        }
    }

    for (int i = 2; i < 8; i++) {
        avgval += buffer_arr[i];
    }
    avgval /= 6;

    float voltage = avgval * (3.3 / 4095.0);

    // Convert voltage to pH using two-point calibration
    float ph_value = 7.0 + ((voltage - PH7_VOLTAGE) / (PH4_VOLTAGE - PH7_VOLTAGE)) * (4.0 - 7.0);

    Serial.print("📏 pH Voltage: ");
    Serial.print(voltage, 3);
    Serial.print(" V | pH Value: ");
    Serial.println(ph_value, 2);

    return ph_value;
}

// ✅ LoRa Receiver - Listens for Commands
void onReceiveLORA(int packetSize) {
    if (packetSize == 0) return;

    Serial.println("📡 Incoming LoRa Packet...");

    byte sender = LoRa.read();
    byte recipient = LoRa.read();
    byte incomingLength = LoRa.read();
    
    if (recipient != LocalAddress) return; // Ignore if not for us

    String incoming = "";
    while (LoRa.available()) incoming += (char)LoRa.read();
    
    if (incoming.length() != incomingLength) return; // Ignore corrupted packets

    Serial.print("📩 Received Control Data: ");
    Serial.println(incoming);

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, incoming);

    if (error) {
        Serial.println("❌ JSON Parsing Failed!");
        return;
    }

    HF = doc["HumidityFire"];
    fanState = doc["Fan"];

    Serial.print("🚰 Pump State: "); Serial.println(HF ? "ON" : "OFF");
    Serial.print("🌬 Fan State: "); Serial.println(fanState ? "ON" : "OFF");

    // ✅ Control the fan
    digitalWrite(fan_pin, fanState ? HIGH : LOW);
}

// ✅ LoRa Transmitter - Sends Sensor Data
void MainTask() {
    float mixerLevel = getWaterLevel(getDistance(SENSOR_MIXER));
    float supplyLevel = getWaterLevel(getDistance(SENSOR_SUPPLY));
    float rainLevel = getWaterLevel(getDistance(SENSOR_RAIN));

    DynamicJsonDocument doc(1024);
    doc["MixerTankLevel"] = mixerLevel;
    doc["SupplyTankLevel"] = supplyLevel;
    doc["RainTankLevel"] = rainLevel;
    doc["Humidity"] = humidity;
    doc["Temperature"] = temperature;
    doc["pH"] = pH_Value;

    String jsonString;
    serializeJson(doc, jsonString);
    LoRa.beginPacket();
    LoRa.write(Destination_Master);
    LoRa.write(LocalAddress);
    LoRa.write(jsonString.length());
    LoRa.print(jsonString);
    LoRa.endPacket();
    LoRa.receive();
    Serial.println(jsonString);
}

// ✅ Function to Read Water Tank Levels
float getDistance(int sensorPin) {
    pinMode(sensorPin, OUTPUT);
    digitalWrite(sensorPin, LOW);
    delayMicroseconds(2);
    digitalWrite(sensorPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(sensorPin, LOW);
    
    pinMode(sensorPin, INPUT);
    long duration = pulseIn(sensorPin, HIGH);
    
    return duration * 0.0343 / 2;
}

float getWaterLevel(float distance) {
    float waterHeight = TANK_HEIGHT - distance;
    return (constrain(waterHeight, 0, TANK_HEIGHT) / TANK_HEIGHT) * 100;
}

// ✅ Tank Level Management
void maintainTankLevels() {
    float mixerLevel = getWaterLevel(getDistance(SENSOR_MIXER));
    float supplyLevel = getWaterLevel(getDistance(SENSOR_SUPPLY));
    float rainLevel = getWaterLevel(getDistance(SENSOR_RAIN));

    if (mixerLevel < MIXER_LOW_THRESHOLD) {
        if (rainLevel > RAIN_LOW_THRESHOLD) {
            digitalWrite(RELAY_RAIN, HIGH);
            digitalWrite(RELAY_SUPPLY, LOW);
        } else {
            digitalWrite(RELAY_RAIN, LOW);
            digitalWrite(RELAY_SUPPLY, HIGH);
        }
    }

    if (mixerLevel >= MIXER_HIGH_THRESHOLD) {
        digitalWrite(RELAY_RAIN, LOW);
        digitalWrite(RELAY_SUPPLY, LOW);
    }
}
// ✅ pH Regulation using PID Control with Motor ON Time
void regulatePH() {
    pH_Error = Target_pH - pH_Value;
    integral = constrain(integral + (pH_Error * 0.1), -10, 10);
    double derivative = (pH_Error - prev_pH_Error) / 0.1;
    PID_Output = (Kp * pH_Error) + (Ki * integral) + (Kd * derivative);
    prev_pH_Error = pH_Error;

    int pumpOnTime = constrain(abs(PID_Output) * 100, MIN_ON_TIME, MAX_ON_TIME);

    if (millis() - lastPumpAction > pumpOnTime) {
        lastPumpAction = millis();
        if (PID_Output > 0){
          digitalWrite(BASE_PUMP_PIN, LOW);
          digitalWrite(ACID_PUMP_PIN,HIGH);
          Serial.println("aCID");
        }
        else{
          digitalWrite(BASE_PUMP_PIN, HIGH);
          digitalWrite(ACID_PUMP_PIN,LOW);
          Serial.println("Base");
        }
        
    }
}

// ✅ Setup
void setup() {
    Serial.begin(115200);

    // ✅ Fix: Properly initialize ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);
    if (esp_now_init() != ESP_OK) {
        DEBUG_PRINT("❌ ESP-NOW Init Failed!");
        return;
    }
    esp_now_register_recv_cb(onReceiveESPNow);

    // ✅ Initialize LoRa
    LoRa.setPins(ss, rst, dio0);
    LoRa.onReceive(onReceiveLORA);
    if (!LoRa.begin(433E6)) while (true);
    
    // ✅ Initialize Sensors
    dht.begin();

      // ✅ Set pin modes for sensors and relays
    pinMode(PH_SENSOR_PIN, INPUT);  // pH sensor input

    pinMode(ACID_PUMP_PIN, OUTPUT);  // Acid pump control
    pinMode(BASE_PUMP_PIN, OUTPUT);  // Base pump control

    pinMode(RELAY_MIXER, OUTPUT);  // Mixer relay
    pinMode(RELAY_SUPPLY, OUTPUT); // Supply relay
    pinMode(RELAY_RAIN, OUTPUT);   // Rain relay

    pinMode(SENSOR_MIXER, INPUT);  // Mixer level sensor
    pinMode(SENSOR_SUPPLY, INPUT); // Supply level sensor
    pinMode(SENSOR_RAIN, INPUT);   // Rain level sensor

    // ✅ Ensure all relays are initially off
    digitalWrite(RELAY_MIXER, LOW);
    digitalWrite(RELAY_SUPPLY, LOW);
    digitalWrite(RELAY_RAIN, LOW);

    digitalWrite(ACID_PUMP_PIN, LOW);
    digitalWrite(BASE_PUMP_PIN, LOW);
}

// ✅ Loop
void loop() {
    maintainTankLevels();
    pH_Value = readPH();
    MainTask();
    regulatePH();
            if (HF==true){
          digitalWrite(fan_pin,HIGH);
        }
        else{
          digitalWrite(fan_pin,LOW);
        }

}
