#include <Arduino.h>
#include <FirebaseESP8266.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

// Firebase Credentials
#define API_KEY "AIzaSyDLcouecDW-MWDGEUW-KFUSvRJhOirKFuw"
#define DATABASE_URL "https://hydrolink-testing-default-rtdb.firebaseio.com/"
#define USER_EMAIL "testdlp@gmail.com"
#define USER_PASSWORD "DLP123"
#define DEVICE_ID "HYD00001"

// Pin Definitions
const int trigPin = 5;
const int echoPin = 4;
const int BAT = A0;
const int seloPin1 = 12;
const int seloPin2 = 13;
const int relay = 15;
const int LED = LED_BUILTIN;
const int flowSensorPin = 0;  // Flow sensor (D5)

// Firebase Objects
FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbdo;
FirebaseData stream;

// Global Variables
float waterLevel = 0.0;
bool Is_switch_ON = false;
bool Is_auto = false;
float Battery_value = 0.0;
float LTH, UTH;
String previousStatus = "OFF";

// Flow Sensor Variables
volatile int pulseCount = 0;
float flowRate = 0.0;
float totalLiters = 0.0;
const float calibrationFactor = 7.5;  

const float TANK_HEIGHT = 100.0;
float lastWaterLevel = -1.0;  

// Battery Monitoring
float lastBatteryValue = 0.0;
const float RatioFactor = 5.2;  

// 5-Minute Flow Rate Averaging
float flowDataBuffer[12];  
int flowIndex = 0;  
float flowSum = 0.0;
int flowCount = 0;
unsigned long fiveMinStartTime = millis();

// Interrupt Service Routine for Flow Sensor
void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);
  Serial.println("🔧 Initializing System...");

  WiFiManager wifimanager;
  wifimanager.setTimeout(120);
  if (!wifimanager.autoConnect("HydrolinkConfig")) {
    Serial.println("⚠️ WiFi failed. Restarting ESP...");
    ESP.restart();
  }

  Serial.println("🌐 Connecting to Firebase...");
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Firebase.setDoubleDigits(5);
  Serial.println("✅ Connected to Firebase!");

  Firebase.beginStream(stream, "/RealTimeDB/Tanks/" + String(DEVICE_ID));
  Firebase.setStreamCallback(stream, streamCallback, streamTimeoutCallback);

  pinMode(LED, OUTPUT);
  pinMode(seloPin1, OUTPUT);
  pinMode(seloPin2, OUTPUT);
  pinMode(relay, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  
  pinMode(flowSensorPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(flowSensorPin), pulseCounter, FALLING);

  Serial.println("✅ System Initialized!");
}

// Firebase Stream Callback
void streamCallback(StreamData data) {
  Serial.println("🔥 Firebase Data Changed!");
  onReceive();
}

// Handle Stream Timeout (Reconnect)
void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("⚠️ Firebase Stream Timeout! Reconnecting...");
    Firebase.beginStream(stream, "/RealTimeDB/Tanks/" + String(DEVICE_ID));
  }
}

// Fetch Real-time Data from Firebase
void onReceive() {
  Serial.println("📡 Fetching Firebase Data...");

  Firebase.getBool(fbdo, "/RealTimeDB/Tanks/" + String(DEVICE_ID) + "/Auto");
  Is_auto = fbdo.boolData();

  Firebase.getBool(fbdo, "/RealTimeDB/Tanks/" + String(DEVICE_ID) + "/ManSwitch");
  Is_switch_ON = fbdo.boolData();

  if (Is_auto) {
    Firebase.getFloat(fbdo, "/RealTimeDB/Tanks/" + String(DEVICE_ID) + "/UpperValue");
    UTH = fbdo.floatData();

    Firebase.getFloat(fbdo, "/RealTimeDB/Tanks/" + String(DEVICE_ID) + "/LowerValue");
    LTH = fbdo.floatData();
  }

  Serial.println("✅ Data Received: Auto=" + String(Is_auto) + " | Manual=" + String(Is_switch_ON));
  controlFlow();
}

// Solenoid Control
void controlFlow() {
  Serial.println("🔄 Checking Water Level Control...");
  if (Is_auto) {
    if (waterLevel >= (UTH - 2)) {
      Serial.println("💧 Water Level High: Turning OFF Solenoid");
      sole_off();
    } else if (Is_switch_ON || waterLevel <= LTH) {
      Serial.println("💧 Water Low or Manual ON: Turning ON Solenoid");
      sole_on();
    } else {
      sole_off();
    }
  }

  else if (Is_switch_ON){
    sole_on();
  }
  else if(!Is_switch_ON) {
    sole_off();
  }
}

// Water Level Measurement Using Ultrasonic Sensor
float getWaterLevel() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH);
  float distance = (duration * 0.0343) / 2;
  float level = (TANK_HEIGHT - distance)/100;

  Firebase.setFloat(fbdo, "/RealTimeDB/Tanks/" + String(DEVICE_ID) + "/DistanceCm", distance);

  Serial.println("📏 Water Level: " + String(level) + " cm");
  return (level < 0) ? 0 : level;
}

// Flow Sensor Data Processing (5-minute averaging)
void updateFlowRate() {
  static unsigned long lastMillis = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastMillis >= 1000) {  
    noInterrupts();
    float currentFlow = (pulseCount / calibrationFactor);
    pulseCount = 0;  
    interrupts();

    flowSum += currentFlow;
    flowCount++;  
    Serial.println("⏳ Flow Rate: " + String(currentFlow) + " L/min");
    lastMillis = currentMillis;
  }
}

// Battery Voltage Monitoring
float checkBatteryVoltage() {
  float raw_value = analogRead(BAT);
  float voltage = (raw_value / 1023.0) * 3.3 * RatioFactor;
  Serial.println("🔋 Battery Voltage: " + String(voltage) + "V");
  Firebase.setFloat(fbdo, "/RealTimeDB/Tanks/" + String(DEVICE_ID) + "/Battery", voltage);
  return voltage;
}

// Solenoid ON/OFF Functions
void sole_off() {
  if (previousStatus == "ON") {
    digitalWrite(seloPin1, HIGH);
    digitalWrite(seloPin2, LOW);
    digitalWrite(LED, LOW);
    previousStatus = "OFF";
    Serial.println("✅ Solenoid Turned ON");
    delay(1000);
    digitalWrite(seloPin1, LOW);
    digitalWrite(seloPin2, LOW);
  }
}

void sole_on() {
  if (previousStatus == "OFF") {
    digitalWrite(seloPin1, LOW);
    digitalWrite(seloPin2, HIGH);
    digitalWrite(LED, HIGH);
    previousStatus = "ON";
    Serial.println("✅ Solenoid Turned OFF");
    delay(1000);
    digitalWrite(seloPin1, LOW);
    digitalWrite(seloPin2, LOW);
  }
}

// Main Loop
void loop() {
  static unsigned long lastBatteryCheckTime = 0;
  static unsigned long lastWaterLevelCheckTime = 0;

  if (millis() - lastBatteryCheckTime >= 5000) {
    Battery_value = checkBatteryVoltage();
    lastBatteryCheckTime = millis();
  }

  if (millis() - lastWaterLevelCheckTime >= 1000) {
    waterLevel = getWaterLevel();
    Firebase.setFloat(fbdo, "/RealTimeDB/Tanks/" + String(DEVICE_ID) + "/Water", waterLevel);
    lastWaterLevelCheckTime = millis();
  }

  updateFlowRate();

  if (millis() - fiveMinStartTime >= 2000) {  
    if (flowCount > 0) {
      float fiveMinAverage = flowSum / flowCount;
      Firebase.setFloat(fbdo, "/RealTimeDB/Tanks/"+ String(DEVICE_ID) +"/FlowRate/Minutes/" + String(flowIndex)+"/", fiveMinAverage);
      Serial.println("/RealTimeDB/Tanks/FlowRate/Minutes/" + String(flowIndex));
      Serial.println("📊 5-min avg Flow: " + String(fiveMinAverage) + " L/min");
      flowIndex = (flowIndex + 1) % 12;
      flowSum = 0.0;
      flowCount = 0;
    }
    fiveMinStartTime = millis();
  }

  controlFlow();


}
