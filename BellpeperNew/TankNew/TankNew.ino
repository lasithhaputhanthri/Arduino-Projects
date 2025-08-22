#include <WiFi.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>  // Include the ArduinoJSON library
#include "DHT.h"
#include <Wire.h>

#define PH_SENSOR_PIN 34   // Analog pin for pH sensor
#define ACID_PUMP_PIN 26   // GPIO for acid pump
#define BASE_PUMP_PIN 27   // GPIO for base pump

// PID parameters (tune these)
double Kp = 2.0;     // Proportional Gain
double Ki = 0.1;     // Integral Gain
double Kd = 0.5;     // Derivative Gain

double pH_Value, PID_Output;
double Target_pH = 7.0;  // Desired pH level
double pH_Error = 0, prev_pH_Error = 0, integral = 0;

#define WAKEUP_TIME 4  // Wake-up time in seconds

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
#define MIXER_LOW_THRESHOLD 50
#define MIXER_HIGH_THRESHOLD 65
#define RAIN_LOW_THRESHOLD 40

// Random sensor values
float sensor1_value, sensor2_value, sensor3_value, sensor4_value;

int pH_Value;
float Voltage;

float H = 0;
float T = 0;


// Define the pin and type of DHT sensor
#define DHTPIN 4       // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11  // Change to DHT22 if using a DHT22 sensor

// Initialize DHT sensor
DHT dht(DHTPIN, DHTTYPE);

//#define LED_PIN 2  // GPIO for LED

// Struct to receive data
typedef struct struct_message {
  int sensorData;
} struct_message;

struct_message myData;

// LoRa communication function
void sendMessage(String Outgoing, byte Destination) {
  LoRa.beginPacket();
  LoRa.write(Destination);        // Add destination address
  LoRa.write(LocalAddress);       // Add sender address
  LoRa.write(Outgoing.length());  // Add payload length
  LoRa.print(Outgoing);           // Add payload
  LoRa.endPacket();
  LoRa.receive();
}

// Function to handle received LoRa messages
void onReceiveLORA(int packetSize) {
  if (packetSize == 0) return;  // if there's no packet, return

  // Read sender and destination addresses
  byte sender = LoRa.read();
  byte recipient = LoRa.read();
  byte incomingLength = LoRa.read();

  // Check if the recipient is this device
  if (recipient != LocalAddress) return;

  String incoming = "";
  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  // Validate message length
  if (incoming.length() != incomingLength) return;

  Serial.print("Received from: 0x");
  Serial.print(sender, HEX);
  Serial.print(" | Message: ");
  Serial.println(incoming);

  // Parse JSON message if needed
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, incoming);
  if (!error) {
    int command = doc["command"];
    if (command == 1) {
      Serial.println("Command 1 received: Activate relay!");
      //digitalWrite(RELAY_MIXER, LOW);  // Example action
    } else if (command == 2) {
      Serial.println("Command 2 received: Deactivate relay!");
      //digitalWrite(RELAY_MIXER, HIGH);  // Example action
    }
  } else {
    Serial.println("Invalid JSON received.");
  }
}

void DHTReadings() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Check if any reading failed and exit early
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  // Print the readings
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.print(" %\t");
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");

  H = humidity;
  T = temperature;
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
  // if (distance == 0) distance = MAX_DISTANCE;            // No echo, assume max distance
  float waterHeight = TANK_HEIGHT - distance;            // Calculate water height
  waterHeight = constrain(waterHeight, 0, TANK_HEIGHT);  // Constrain to valid range
  return (waterHeight / TANK_HEIGHT) * 100;              // Convert to percentage
}

// Task for Core 0 (running other tasks, e.g., LED blinking)
void taskCore0(void *parameter) {
  // Optionally, add your code here for Core 0 tasks like blinking LED or others
  while (true) {
    pH_Value = readPH();
    pH_Error = Target_pH - pH_Value;  // Calculate error

    PID_Output = computePID(pH_Error);  // Compute new PID output

    Serial.print("pH: ");
    Serial.print(pH_Value);
    Serial.print(" | PID Output: ");
    Serial.println(PID_Output);

    controlPumps(PID_Output);  // Control acid/base pumps

    delay(5000);  // Repeat every 5 seconds
}

// ESP-NOW Receive Callback for Core 1
void onReceive(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Received Sensor Data: ");
  Serial.println(myData.sensorData);

  //Main Code goes Here
  MainTask();

  Serial.println("Staying awake for 100 milliseconds...");
  delay(100);  // Keep the ESP32 awake for a while before sleeping

  Serial.println("Going back to sleep...");
  WiFi.setSleep(WIFI_PS_MIN_MODEM);  // Enable modem sleep to save power but keep WiFi active
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

  pinMode(pH_Value, INPUT);

  // Initialize relay pins as outputs
  pinMode(RELAY_MIXER, OUTPUT);
  pinMode(RELAY_SUPPLY, OUTPUT);
  pinMode(RELAY_RAIN, OUTPUT);

  // Start with trigger pins set to LOW
  digitalWrite(TRIG_MIXER, LOW);
  digitalWrite(TRIG_SUPPLY, LOW);
  digitalWrite(TRIG_RAIN, LOW);

  // Initialize WiFi in STA mode
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onReceive);

  // Enable wake-up from WiFi (ESP-NOW)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 1);  // Optional if you want external wake-up

  Serial.println("ESP is entering light sleep...");
  WiFi.setSleep(WIFI_PS_MIN_MODEM);  // Use modem sleep instead of full light sleep

  // Create tasks for Core 0 and Core 1
  xTaskCreatePinnedToCore(taskCore0, "TaskCore0", 10000, NULL, 1, NULL, 0);  // Task on Core 0
  // No need to create a task for Core 1 as the ESP-NOW callback will run on Core 1 by default
  // Initialize LoRa
  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa initialization failed.");
    while (true)
      ;
  }
  Serial.println("LoRa initialization successful.");

  // Set LoRa receive callback
  LoRa.onReceive(onReceiveLORA);
  LoRa.receive();

  // Initialize relays as off
  digitalWrite(RELAY_MIXER, HIGH);
  digitalWrite(RELAY_SUPPLY, HIGH);
  digitalWrite(RELAY_RAIN, HIGH);
  dht.begin();

      
    pinMode(ACID_PUMP_PIN, OUTPUT);
    pinMode(BASE_PUMP_PIN, OUTPUT);
    digitalWrite(ACID_PUMP_PIN, LOW);
    digitalWrite(BASE_PUMP_PIN, LOW);
}

void loop() {
  // Do nothing, everything is handled in the callback
}

void MainTask() {

  // Measure distances from sensors
  float mixerDistance = getDistance(TRIG_MIXER, ECHO_MIXER);
  float supplyDistance = getDistance(TRIG_SUPPLY, ECHO_SUPPLY);
  float rainDistance = getDistance(TRIG_RAIN, ECHO_RAIN);


  // Calculate water levels
  float mixerLevel = getWaterLevel(mixerDistance);
  float supplyLevel = getWaterLevel(supplyDistance);
  float rainLevel = getWaterLevel(rainDistance);

  //DHTReadings();

  // Print water levels to Serial Monitor
  Serial.println("Water Levels:");
  Serial.print("Mixer Level: ");
  Serial.print(mixerLevel);
  Serial.print(" %");
  Serial.print(" ");
  

  Serial.print("Supply Level: ");
  Serial.print(supplyLevel);
  Serial.print(" %");
  Serial.print(" ");

  Serial.print("Rain Level: ");
  Serial.print(rainLevel);
  Serial.println(" %");
  Serial.print(" ");

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
    if (getWaterLevel(getDistance(TRIG_MIXER, ECHO_MIXER)) < MIXER_HIGH_THRESHOLD) {

      // Print water levels to Serial Monitor
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
      Serial.print(" %");
      Serial.print(" ");

      Serial.print("Supply Level: ");
      Serial.print(supplyLevel);
      Serial.print(" %");
        Serial.print(" ");

      Serial.print("Rain Level: ");
      Serial.print(rainLevel);
      Serial.print(" %");
      Serial.print(" ");
      // Activate relays in a Knight Rider pattern
      delay(500);  // Check levels periodically
    }
    else{
    digitalWrite(RELAY_RAIN, HIGH);
    digitalWrite(RELAY_SUPPLY, HIGH);
    Serial.println("Mixer Tank Filling Complete.");
    }
  }

  // Prepare JSON payload
  DynamicJsonDocument doc(1024);
  doc["MixerTankLevel"] = mixerLevel;
  doc["SupplyTankLevel"] = supplyLevel;
  doc["RainTankLevel"] = rainLevel;
  doc["Humidity"] = 78;
  doc["Temperaure"] = 28.5;

  JsonObject plant1 = doc.createNestedObject("GreenHouse1");
  //plant1["PH"] = Voltage / 10 + 4.6;
  plant1["MoistureLevel"] = sensor2_value;


  // Serialize JSON payload
  String jsonString;
  serializeJson(doc, jsonString);

  // Send JSON payload via LoRa
  sendMessage(jsonString, Destination_Master);

  Serial.println(jsonString);
}

// PID control function with motor control
float readPH() {
    int buffer_arr[10], temp;
    unsigned long avgval = 0;
    
    for (int i = 0; i < 10; i++) {
        buffer_arr[i] = analogRead(PH_SENSOR_PIN);
        delay(30);
    }

    for (int i = 0; i < 9; i++) {
        for (int j = i + 1; j < 10; j++) {
            if (buffer_arr[i] > buffer_arr[j]) {
                temp = buffer_arr[i];
                buffer_arr[i] = buffer_arr[j];
                buffer_arr[j] = temp;
            }
        }
    }

    for (int i = 2; i < 8; i++) {
        avgval += buffer_arr[i];
    }

    float voltage = (float)avgval * 3.3 / 4096 / 6;  // ESP32 ADC (3.3V)
    return -5.70 * voltage + 21.34;  // Adjust with your calibration
}

void controlPumps(double pid_value) {
    if (pid_value > 0) {
        digitalWrite(BASE_PUMP_PIN, HIGH);
        delay(abs(pid_value));  // Run base pump for calculated time
        digitalWrite(BASE_PUMP_PIN, LOW);
    } 
    else if (pid_value < 0) {
        digitalWrite(ACID_PUMP_PIN, HIGH);
        delay(abs(pid_value));  // Run acid pump for calculated time
        digitalWrite(ACID_PUMP_PIN, LOW);
    }
}

double computePID(double error) {
    double derivative = error - prev_pH_Error;
    integral += error;
    prev_pH_Error = error;
    return (Kp * error) + (Ki * integral) + (Kd * derivative);
}
