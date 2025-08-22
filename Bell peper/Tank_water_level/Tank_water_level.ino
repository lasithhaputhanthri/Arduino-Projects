#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>  // Include the ArduinoJSON library

// Pin Definitions
#define RELAY_MIXER 22
#define RELAY_SUPPLY 33
#define RELAY_RAIN 32

#define TRIG_MIXER 12
#define ECHO_MIXER 13
#define TRIG_SUPPLY 27
#define ECHO_SUPPLY 15
#define TRIG_RAIN 16
#define ECHO_RAIN 17

#define ss 5
#define rst 14
#define dio0 2
#define LocalAddress 0x02        // Address of this device
#define Destination_Master 0x01  // Destination (Master)

// Tank dimensions (in cm)
#define TANK_HEIGHT 12    // Example: Tank height in cm
#define MAX_DISTANCE 200  // Max sensor distance in cm

// Water level thresholds (percentage)
#define MIXER_LOW_THRESHOLD 25
#define MIXER_HIGH_THRESHOLD 80
#define RAIN_LOW_THRESHOLD 40

// Random sensor values
float sensor1_value, sensor2_value, sensor3_value, sensor4_value;

// Structure for ESP-NOW data reception
typedef struct struct_message {
  float soilMoisture;
} struct_message;

// Create an instance to store ESP-NOW data
struct_message myData;

// Callback function for ESP-NOW reception
void OnDataRecv(const esp_now_recv_info* recv_info, const uint8_t* incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("ESP-NOW Data Received | Soil Moisture: ");
  Serial.println(myData.soilMoisture, 2);
}

// LoRa communication function
void sendMessage(String Outgoing, byte Destination) {
  LoRa.beginPacket();
  LoRa.write(Destination);        // Add destination address
  LoRa.write(LocalAddress);       // Add sender address
  LoRa.write(Outgoing.length());  // Add payload length
  LoRa.print(Outgoing);           // Add payload
  LoRa.endPacket();
}

// Function to measure distance from an ultrasonic sensor
float getDistance(int trigPin, int echoPin) {
  long duration;
  float distance;

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.0343 / 2;

  return distance;
}

// Function to calculate water level percentage
float getWaterLevel(float distance) {
  if (distance == 0) distance = MAX_DISTANCE;            // No echo, assume max distance
  float waterHeight = TANK_HEIGHT - distance;            // Calculate water height
  waterHeight = constrain(waterHeight, 0, TANK_HEIGHT);  // Constrain to valid range
  return (waterHeight / TANK_HEIGHT) * 100;              // Convert to percentage
}

void setup() {
  Serial.begin(115200);

  // Set pin modes for ultrasonic sensor pins
  pinMode(TRIG_MIXER, OUTPUT);
  pinMode(ECHO_MIXER, INPUT);
  pinMode(TRIG_SUPPLY, OUTPUT);
  pinMode(ECHO_SUPPLY, INPUT);
  pinMode(TRIG_RAIN, OUTPUT);
  pinMode(ECHO_RAIN, INPUT);

  // Initialize relay pins as outputs
  pinMode(RELAY_MIXER, OUTPUT);
  pinMode(RELAY_SUPPLY, OUTPUT);
  pinMode(RELAY_RAIN, OUTPUT);

  // Start with trigger pins set to LOW
  digitalWrite(TRIG_MIXER, LOW);
  digitalWrite(TRIG_SUPPLY, LOW);
  digitalWrite(TRIG_RAIN, LOW);

  // Initialize Wi-Fi
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the ESP-NOW receive callback function
  esp_now_register_recv_cb(OnDataRecv);

  // Initialize LoRa
  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa initialization failed.");
    while (true)
      ;
  }
  Serial.println("LoRa initialization successful.");

  // Initialize relays as off
  digitalWrite(RELAY_MIXER, HIGH);
  digitalWrite(RELAY_SUPPLY, HIGH);
  digitalWrite(RELAY_RAIN, HIGH);
}

void loop() {

  // Measure distances from sensors
  float mixerDistance = getDistance(TRIG_MIXER, ECHO_MIXER);
  float supplyDistance = getDistance(TRIG_SUPPLY, ECHO_SUPPLY);
  float rainDistance = getDistance(TRIG_RAIN, ECHO_RAIN);

  // Calculate water levels
  float mixerLevel = getWaterLevel(mixerDistance);
  float supplyLevel = getWaterLevel(supplyDistance);
  float rainLevel = getWaterLevel(rainDistance);

  // Print water levels to Serial Monitor
  Serial.println("Water Levels:");
  Serial.print("Mixer Level: ");
  Serial.print(mixerLevel);
  Serial.println(" cm");

  Serial.print("Supply Level: ");
  Serial.print(supplyLevel);
  Serial.println(" cm");

  Serial.print("Rain Level: ");
  Serial.print(rainLevel);
  Serial.println(" cm");

  // Random values for additional sensors
  sensor1_value = random(20, 30);  // Example random value for sensor 1
  sensor2_value = random(40, 60);  // Example random value for sensor 2
  sensor3_value = random(20, 30);  // Example random value for sensor 3
  sensor4_value = random(40, 60);  // Example random value for sensor 4

  // Control mixer tank filling logic
  if (mixerLevel < MIXER_LOW_THRESHOLD) {
    if (rainLevel > RAIN_LOW_THRESHOLD) {
      Serial.println("Filling Mixer Tank from Rain Water Tank...");
      digitalWrite(RELAY_RAIN, LOW);
      digitalWrite(RELAY_SUPPLY, HIGH);  // Ensure supply is off
    } else {
      Serial.println("Filling Mixer Tank from Supply Tank...");
      digitalWrite(RELAY_RAIN, HIGH);  // Ensure rain is off
      digitalWrite(RELAY_SUPPLY, LOW);
    }

    // Stop filling when the mixer tank reaches the high threshold
    while (getWaterLevel(getDistance(TRIG_MIXER, ECHO_MIXER)) < MIXER_HIGH_THRESHOLD) {
      // // Print water levels to Serial Monitor
      // // Measure distances from sensors
      // float mixerDistance = getDistance(TRIG_MIXER, ECHO_MIXER);
      // float supplyDistance = getDistance(TRIG_SUPPLY, ECHO_SUPPLY);
      // float rainDistance = getDistance(TRIG_RAIN, ECHO_RAIN);

      // // Calculate water levels
      // float mixerLevel = getWaterLevel(mixerDistance);
      // float supplyLevel = getWaterLevel(supplyDistance);
      // float rainLevel = getWaterLevel(rainDistance);

      // // Print water levels to Serial Monitor
      // Serial.println("Water Levels:");
      // Serial.print("Mixer Level: ");
      // Serial.print(mixerLevel);
      // Serial.println(" cm");

      // Serial.print("Supply Level: ");
      // Serial.print(supplyLevel);
      // Serial.println(" cm");

      // Serial.print("Rain Level: ");
      // Serial.print(rainLevel);
      // Serial.println(" cm");
      delay(500);                       // Check levels periodically
    }

    digitalWrite(RELAY_RAIN, HIGH);
    digitalWrite(RELAY_SUPPLY, HIGH);
    Serial.println("Mixer Tank Filling Complete.");
  }

  // Prepare JSON payload
  DynamicJsonDocument doc(1024);
  doc["MixerTankLevel"] = mixerLevel;
  doc["SupplyTankLevel"] = supplyLevel;
  doc["RainTankLevel"] = rainLevel;

  JsonObject plant1 = doc.createNestedObject("GreenHouse1");
  plant1["PH"] = sensor1_value;
  plant1["MoistureLevel"] = sensor2_value;


  // Serialize JSON payload
  String jsonString;
  serializeJson(doc, jsonString);

  // Send JSON payload via LoRa
  sendMessage(jsonString, Destination_Master);

  Serial.println(jsonString);

  delay(1000);  // Short delay between loops
}