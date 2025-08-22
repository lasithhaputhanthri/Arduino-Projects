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
#define ACID_PUMP_PIN 17 //ok
#define BASE_PUMP_PIN 27//ok

#define RELAY_MIXER 22 //ok
#define RELAY_SUPPLY 33 //ok
#define RELAY_RAIN 32 //ok

#define SENSOR_MIXER 12 //ok
#define SENSOR_SUPPLY 27 //ok
#define SENSOR_RAIN 16 // ok

#define ss 5
#define rst 14
#define dio0 2
#define LocalAddress 0x02
#define Destination_Master 0x01

#define TANK_HEIGHT 12
#define MAX_DISTANCE 200

#define MIXER_LOW_THRESHOLD 50
#define MIXER_HIGH_THRESHOLD 65
#define RAIN_LOW_THRESHOLD 40

// pH Calibration
#define PH4_VOLTAGE 2.56 
#define PH7_VOLTAGE 2.245 

// PID parameters
double Kp = 2.0, Ki = 0.1, Kd = 0.5;
double pH_Value, PID_Output, Target_pH = 7.0;
double pH_Error = 0, prev_pH_Error = 0, integral = 0;

// Temperature and Humidity Sensor
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

float H = 0, T = 0;
bool espNowDataReceived = false;

// Struct for ESP-NOW communication
typedef struct struct_message {
    float soilMoisture;
    float externalTemp;
} struct_message;

struct_message receivedData;

// ✅ ESP-NOW callback function with validation
void onReceiveESPNow(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (len != sizeof(receivedData)) {
        DEBUG_PRINT("Error: Invalid ESP-NOW packet received");
        return;
    }
    memcpy(&receivedData, incomingData, sizeof(receivedData));

    DEBUG_PRINT("ESP-NOW Data Received:");
    DEBUG_PRINT("Soil Moisture: " + String(receivedData.soilMoisture));
    DEBUG_PRINT("External Temp: " + String(receivedData.externalTemp));

    espNowDataReceived = true;
}

// ✅ LoRa function with acknowledgment
bool sendLoRaMessage(String message, byte destination) {
    LoRa.beginPacket();
    LoRa.write(destination);
    LoRa.write(LocalAddress);
    LoRa.write(message.length());
    LoRa.print(message);
    LoRa.endPacket();
    LoRa.receive();

    unsigned long start = millis();
    while (millis() - start < 2000) {
        if (LoRa.parsePacket()) {
            String ack = "";
            while (LoRa.available()) ack += (char)LoRa.read();
            if (ack == "ACK") return true;
        }
    }
    return false;
}

// ✅ LoRa reception
void onReceiveLORA(int packetSize) {
    if (packetSize == 0) return;
    
    byte sender = LoRa.read();
    byte recipient = LoRa.read();
    byte incomingLength = LoRa.read();
    
    if (recipient != LocalAddress) return;

    String incoming = "";
    while (LoRa.available()) incoming += (char)LoRa.read();
    if (incoming.length() != incomingLength) return;

    DEBUG_PRINT("Received via LoRa: " + incoming);
    sendLoRaMessage("ACK", sender);
}

// ✅ Single-Pin Ultrasonic Sensor Function
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

// ✅ Continuous Tank Level Maintenance (checked every 60s)
void maintainTankLevels() {
    float mixerLevel = getWaterLevel(getDistance(SENSOR_MIXER));
    float supplyLevel = getWaterLevel(getDistance(SENSOR_SUPPLY));
    float rainLevel = getWaterLevel(getDistance(SENSOR_RAIN));

    if (mixerLevel < MIXER_LOW_THRESHOLD) {
        if (rainLevel > RAIN_LOW_THRESHOLD) {
            DEBUG_PRINT("Filling Mixer Tank from Rain Water Tank...");
            digitalWrite(RELAY_RAIN, LOW);
            digitalWrite(RELAY_SUPPLY, HIGH);
        } else {
            DEBUG_PRINT("Filling Mixer Tank from Supply Tank...");
            digitalWrite(RELAY_RAIN, HIGH);
            digitalWrite(RELAY_SUPPLY, LOW);
        }
    }

    if (mixerLevel >= MIXER_HIGH_THRESHOLD) {
        DEBUG_PRINT("Mixer Tank Filled. Stopping water supply...");
        digitalWrite(RELAY_RAIN, HIGH);
        digitalWrite(RELAY_SUPPLY, HIGH);
    }
}

// ✅ pH Regulation using PID Control
void regulatePH() {
    pH_Error = Target_pH - pH_Value;
    integral += pH_Error * 0.1; // Integrate error over time
    double derivative = (pH_Error - prev_pH_Error) / 0.1;
    PID_Output = (Kp * pH_Error) + (Ki * integral) + (Kd * derivative);
    
    prev_pH_Error = pH_Error;

    if (PID_Output > 0) {
        DEBUG_PRINT("Adding Base...");
        digitalWrite(BASE_PUMP_PIN, LOW);
        digitalWrite(ACID_PUMP_PIN, HIGH);
    } else {
        DEBUG_PRINT("Adding Acid...");
        digitalWrite(BASE_PUMP_PIN, HIGH);
        digitalWrite(ACID_PUMP_PIN, LOW);
    }
}

// ✅ Main Task (Only sends LoRa when ESP-NOW data is received)
void MainTask() {
    if (!espNowDataReceived) return;

    DynamicJsonDocument doc(512);
    doc["MixerTankLevel"] = getWaterLevel(getDistance(SENSOR_MIXER));
    doc["SupplyTankLevel"] = getWaterLevel(getDistance(SENSOR_SUPPLY));
    doc["RainTankLevel"] = getWaterLevel(getDistance(SENSOR_RAIN));
    doc["Humidity"] = H;
    doc["Temperature"] = T;
    
    JsonObject plant1 = doc.createNestedObject("GreenHouse1");
    plant1["SoilMoisture"] = receivedData.soilMoisture;
    plant1["ExternalTemp"] = receivedData.externalTemp;

    String jsonString;
    serializeJson(doc, jsonString);
    sendLoRaMessage(jsonString, Destination_Master);

    espNowDataReceived = false;
}

// ✅ Setup Function
void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    pinMode(RELAY_RAIN, OUTPUT);
    pinMode(RELAY_SUPPLY, OUTPUT);
    digitalWrite(RELAY_RAIN, HIGH);
    digitalWrite(RELAY_SUPPLY, HIGH);

    if (esp_now_init() != ESP_OK) {
        DEBUG_PRINT("ESP-NOW Init Failed");
        return;
    }
    esp_now_register_recv_cb(onReceiveESPNow);

    LoRa.setPins(ss, rst, dio0);
    if (!LoRa.begin(433E6)) while (true);

    dht.begin();
}

// ✅ Loop with optimized sensor polling (No Deep Sleep)

void loop() {
    static unsigned long lastTankCheck = 0;
    static unsigned long lastSensorRead = 0;

    // Show ultrasonic sensor readings every 5 seconds
    if (millis() - lastSensorRead > 100) {
        float mixerLevel = getWaterLevel(getDistance(SENSOR_MIXER));
        float supplyLevel = getWaterLevel(getDistance(SENSOR_SUPPLY));
        float rainLevel = getWaterLevel(getDistance(SENSOR_RAIN));

        //Serial.println("🔵 Ultrasonic Sensor Readings:");
        Serial.print("Mixer Tank Level: " + String(mixerLevel) + " %");
        Serial.print("Supply Tank Level: " + String(supplyLevel) + " %");
        Serial.print("Rain Tank Level: " + String(rainLevel) + " %");
        Serial.println("---------------------------------");

        lastSensorRead = millis();
    }

    // Perform main tank checks and pH regulation every 60 seconds
    if (millis() - lastTankCheck > 60000) {
        maintainTankLevels();
        regulatePH();
        lastTankCheck = millis();
    }

    MainTask();
}


