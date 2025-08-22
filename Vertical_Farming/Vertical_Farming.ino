#include <Arduino.h>
#include <Ticker.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "DHTesp.h" // DHT library
#include <ArduinoJson.h> // ArduinoJson library

#ifndef ESP32
#pragma message(THIS EXAMPLE IS FOR ESP32 ONLY!)
#error Select ESP32 board.
#endif

// Define ultrasonic sensor pins (left-side pins)
#define FERT_TRIG 32
#define FERT_ECHO 25
#define MIX_TRIG 26
#define MIX_ECHO 27
#define SUP_TRIG 12
#define SUP_ECHO 14

// Define relay pins (left-side pins)
#define RELAY_SUP 13
#define RELAY_FERT 33
#define RELAY_MIX 18

// Define button pin (left-side pin)
#define BUTTON_PIN 4

// Tank thresholds (percentage)
#define LOWER_THRESHOLD 20
#define UPPER_THRESHOLD 80

// Time durations for relays (in milliseconds)
int SUPPLY_DURATION=5000;
int FERTILIZER_DURATION=5000;
int MIX_DURATION=5000;

/** Wi-Fi Credentials */
const char* ssid = "LasithWifi";
const char* password = "12345678";

/** Server URL */
const String serverURL = "http://192.168.1.194:8080/receive_json"; // Replace <server_ip> with your Flask server's IP

/** Initialize DHT sensors */
DHTesp dhtSensor1;
DHTesp dhtSensor2;
DHTesp dhtSensor3;
DHTesp dhtSensor4;

/** Pin definitions */
int dhtPin1 = 19;
int dhtPin2 = 21;
int dhtPin3 = 22;
int dhtPin4 = 23;
int ldrPin1 = 34;
int ldrPin2 = 35;

/** Tickers for periodic tasks */
Ticker dataTicker;

/** Sensor data */
TempAndHumidity sensor1Data;
TempAndHumidity sensor2Data;
TempAndHumidity sensor3Data;
TempAndHumidity sensor4Data;
int ldr1Value;
int ldr2Value;

// Tank heights in cm (adjust as per your tanks)
const float FERT_MAX_HEIGHT = 50.0;
const float MIX_MAX_HEIGHT = 50.0;
const float SUP_MAX_HEIGHT = 50.0;

//ratios
float NPK_ratio=0.5;
int Water_Velocity=5;

// State machine variables
enum State { IDLE, SUPPLY_OPEN, FERTILIZER_OPEN, MIX_OPEN, COMPLETE };
State currentState = IDLE;

unsigned long previousMillis = 0;
float fertLevel = 0, mixLevel = 0, supLevel = 0;
bool buttonPressed = false;

// Function to calculate water level percentage using ultrasonic sensor
float getWaterLevel(int trigPin, int echoPin, float maxHeightCM) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH);
  float distance = (duration * 0.0343) / 2; // Convert to cm
  float level = (maxHeightCM - distance) / maxHeightCM * 100; // Calculate percentage

  Serial.println(distance);

  return constrain(level, 0, 100); // Ensure the percentage is between 0 and 100
}

/**
 * URL-encodes a string
 */
String urlEncode(String str) {
  String encoded = "";
  char c;
  char bufHex[4];
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c; // Keep alphanumeric characters as-is
    } else {
      sprintf(bufHex, "%%%02X", c); // Convert other characters to %HEX
      encoded += bufHex;
    }
  }
  return encoded;
}

/**
 * Send JSON data to the server
 */
void sendDataToServer() {
  // Read sensor data
  sensor1Data = dhtSensor1.getTempAndHumidity();
  sensor2Data = dhtSensor2.getTempAndHumidity();
  sensor3Data = dhtSensor3.getTempAndHumidity();
  sensor4Data = dhtSensor4.getTempAndHumidity();
  ldr1Value = analogRead(ldrPin1);
  ldr2Value = analogRead(ldrPin2);

  // Create JSON string
  String jsonData = "{";
  jsonData += "\"sensor1\": {\"temperature\": " + String(sensor1Data.temperature, 2) + ", \"humidity\": " + String(sensor1Data.humidity, 1) + "},";
  jsonData += "\"sensor2\": {\"temperature\": " + String(sensor2Data.temperature, 2) + ", \"humidity\": " + String(sensor2Data.humidity, 1) + "},";
  jsonData += "\"sensor3\": {\"temperature\": " + String(sensor3Data.temperature, 2) + ", \"humidity\": " + String(sensor3Data.humidity, 1) + "},";
  jsonData += "\"sensor4\": {\"temperature\": " + String(sensor4Data.temperature, 2) + ", \"humidity\": " + String(sensor4Data.humidity, 1) + "},";
  jsonData += "\"ldr1\": " + String(ldr1Value) + ",";
  jsonData += "\"ldr2\": " + String(ldr2Value);
  jsonData += "}";

  // Encode JSON for URL
  String encodedJson = urlEncode(jsonData);

  Serial.println(jsonData);
  // Send JSON to server
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverURL + "?data=" + encodedJson); // Append encoded JSON as query parameter
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Data sent successfully: " + jsonData);
      Serial.println("Response code: " + String(httpResponseCode));
      Serial.println("Response: " + response);

      // Parse the JSON response to extract relay states
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, response);

      if (error) {
        Serial.println("Failed to parse response");
        return;
      }

      // Extract relay states from the response
      JsonArray relayStates = doc["relay_states"].as<JsonArray>();

      // Print the relay states
      for (int i = 0; i < relayStates.size(); i++) {
        Serial.print("Relay ");
        Serial.print(i + 1);
        Serial.print(" state: ");
        Serial.println(relayStates[i].as<int>());
      }

    } else {
      Serial.println("Failed to send data. HTTP code: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi not connected!");
  }
}

/**
 * Arduino setup function
 */

void setup() {
  // Initialize serial monitor
  Serial.begin(115200);

  // Initialize ultrasonic pins
  pinMode(FERT_TRIG, OUTPUT);
  pinMode(FERT_ECHO, INPUT);
  pinMode(MIX_TRIG, OUTPUT);
  pinMode(MIX_ECHO, INPUT);
  pinMode(SUP_TRIG, OUTPUT);
  pinMode(SUP_ECHO, INPUT);

  // Initialize relay pins
  pinMode(RELAY_SUP, OUTPUT);
  pinMode(RELAY_FERT, OUTPUT);
  pinMode(RELAY_MIX, OUTPUT);
  digitalWrite(RELAY_SUP, HIGH); // Turn off relays initially
  digitalWrite(RELAY_FERT, HIGH);
  digitalWrite(RELAY_MIX, HIGH);

  // Initialize button pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize DHT sensors
  dhtSensor1.setup(dhtPin1, DHTesp::DHT11);
  dhtSensor2.setup(dhtPin2, DHTesp::DHT11);
  dhtSensor3.setup(dhtPin3, DHTesp::DHT11);
  dhtSensor4.setup(dhtPin4, DHTesp::DHT11);

  // Configure pins for LDR sensors
  pinMode(ldrPin1, INPUT);
  pinMode(ldrPin2, INPUT);

  // Start periodic data sending
  dataTicker.attach(2, sendDataToServer); // Trigger every 2 seconds
}

void loop() {
  // Read water levels
  fertLevel = getWaterLevel(FERT_TRIG, FERT_ECHO, FERT_MAX_HEIGHT);
  mixLevel = getWaterLevel(MIX_TRIG, MIX_ECHO, MIX_MAX_HEIGHT);
  supLevel = getWaterLevel(SUP_TRIG, SUP_ECHO, SUP_MAX_HEIGHT);

  // Print water levels to serial
  Serial.printf("Fertilizer: %.2f%%, Mixing: %.2f%%, Supply: %.2f%%\n", fertLevel, mixLevel, supLevel);

  if (Serial.available()) {
    String Serial_INPUT = Serial.readStringUntil('\n');

    int Water_amount=Serial_INPUT.toInt();
    SUPPLY_DURATION=Water_amount/Water_Velocity;
    FERTILIZER_DURATION=SUPPLY_DURATION*NPK_ratio;
    MIX_DURATION=SUPPLY_DURATION+FERTILIZER_DURATION;

    // Log if tanks are below threshold
    if (supLevel < LOWER_THRESHOLD) {
      Serial.println("Error: Supply tank water below lower threshold!");
    }
    if (fertLevel < LOWER_THRESHOLD) {
      Serial.println("Error: Fertilizer tank water below lower threshold!");
    }

    // Transition to SUPPLY_OPEN state if thresholds are met
    if (supLevel >= LOWER_THRESHOLD && fertLevel >= LOWER_THRESHOLD) {
      currentState = SUPPLY_OPEN;
      previousMillis = millis(); // Start timing
      digitalWrite(RELAY_SUP, LOW); // Turn on supply tank relay
    }
  } else if (digitalRead(BUTTON_PIN) == HIGH) {
    buttonPressed = false;
  }

  // State machine for relay operation
  switch (currentState) {
    case IDLE:
      // Do nothing in IDLE state
      break;

    case SUPPLY_OPEN:
      if (millis() - previousMillis >= SUPPLY_DURATION) {
        digitalWrite(RELAY_SUP, HIGH); // Turn off supply tank relay
        currentState = FERTILIZER_OPEN;
        previousMillis = millis(); // Start timing for next state
        digitalWrite(RELAY_FERT, LOW); // Turn on fertilizer tank relay
      }
      break;

    case FERTILIZER_OPEN:
      if (millis() - previousMillis >= FERTILIZER_DURATION) {
        digitalWrite(RELAY_FERT, HIGH); // Turn off fertilizer tank relay
        currentState = MIX_OPEN;
        previousMillis = millis(); // Start timing for next state
        digitalWrite(RELAY_MIX, LOW); // Turn on mixing tank relay
      }
      break;

    case MIX_OPEN:
      if (millis() - previousMillis >= MIX_DURATION) {
        digitalWrite(RELAY_MIX, HIGH); // Turn off mixing tank relay
        currentState = COMPLETE; // Transition to COMPLETE state
      }
      break;

    case COMPLETE:
      Serial.println("Process complete!");
      currentState = IDLE; // Go back to IDLE state
      break;
  }

  delay(100); // Short delay for stability (can be adjusted)
}
