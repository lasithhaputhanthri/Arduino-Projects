#include <ArduinoJson.h>
#include <esp_now.h>
#include <WiFi.h>

#define BUTTON_PIN 4  // BOOT button is GPIO0

// Button state
bool lastButtonState = HIGH;

// Boost speed management
const int BASE_SPEED_NORMAL = 84;
const int BASE_SPEED_STARTING = 180;
const int START_BOOST_DURATION_MS = 200;

bool justStarted = true;
unsigned long startBoostStartTime = 0;

// PID Parameters
float Kp = 30, Ki = 0.1, Kd = 10;
float error = 0, previous_error = 0;
float integral = 0, derivative = 0;
float correction = 0;

// IR Sensor pins
const int IR_pins[] = { 33, 34, 35, 36, 32, 39, 25, 26 };
const int num_sensors = 8;

// Motor pins
const int motor_left_forward = 23;
const int motor_left_backward = 22;
const int motor_right_forward = 18;
const int motor_right_backward = 19;

bool justScannedPlant = false;


#define ULTRASONIC_FRONT_PIN 13
#define OBSTACLE_DISTANCE_CM 15

// Ultrasonic
#define ULTRASONIC_LEFT_PIN 27
#define ULTRASONIC_RIGHT_PIN 14
#define PLANT_DETECTION_DISTANCE_CM 30

// State
bool waitingForNPK = false;
bool npkDataReceived = false;
int plantCounter = 0;
bool returning = false;
bool patrolEnabled = false;

// Gateway MAC
uint8_t gatewayAddress[] = { 0xF8, 0xB3, 0xB7, 0x2B, 0x3B, 0x60 };

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < num_sensors; i++) pinMode(IR_pins[i], INPUT);

  pinMode(motor_left_forward, OUTPUT);
  pinMode(motor_left_backward, OUTPUT);
  pinMode(motor_right_forward, OUTPUT);
  pinMode(motor_right_backward, OUTPUT);
  pinMode(ULTRASONIC_LEFT_PIN, OUTPUT);
  pinMode(ULTRASONIC_RIGHT_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // BOOT button uses pull-up

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  esp_now_register_recv_cb(onEspNowReceive);
  Serial.println("📡 Waiting for START command via ESP-NOW or BOOT button...");
}

void loop() {
  handleSerial();
  handleButton();

  if (!patrolEnabled) {
    stopmotors();
    return;
  }

  if (isObstacleDetected()) {
    stopmotors();
    Serial.println("🛑 Obstacle ahead! Waiting...");
    delay(100);  // Small delay before re-check
    return;      // Skip rest of the loop
  }

  if (!returning && !waitingForNPK) {
    if (detectPlantNearby()) {
      if (!justScannedPlant) {
        plantCounter++;
        triggerNPKSensor(plantCounter);
        justScannedPlant = true;
      }
    } else {
      justScannedPlant = false;  // Reset when plant is no longer detected
    }
  }



  if (isLineEnded()) {
    stopmotors();
    delay(500);
    if (!returning) {
      Serial.println("🚧 End of forward path. Turning around...");
      turn180();
      returning = true;
    } else {
      Serial.println("🏁 Returned to base. Waiting for next START command...");
      turn180();
      returning = false;
      plantCounter = 0;
      patrolEnabled = false;
    }
  }

  int position = readSensors();
  error = position - (num_sensors - 1) / 2.0;
  integral += error;
  derivative = error - previous_error;
  correction = (Kp * error) + (Ki * integral) + (Kd * derivative);
  previous_error = error;

  int left_motor_speed = constrain(BASE_SPEED_NORMAL + correction, 0, 1023);
  int right_motor_speed = constrain(BASE_SPEED_NORMAL - correction, 0, 1023);
  driveMotors(left_motor_speed, right_motor_speed);
  delay(10);
}

bool detectPlantNearby() {
  long leftDist = readUltrasonicDistance(ULTRASONIC_LEFT_PIN);
  long rightDist = readUltrasonicDistance(ULTRASONIC_RIGHT_PIN);
  Serial.printf("Left: %ld cm | Right: %ld cm\n", leftDist, rightDist);
  return (leftDist > 0 && leftDist <= PLANT_DETECTION_DISTANCE_CM) || (rightDist > 0 && rightDist < PLANT_DETECTION_DISTANCE_CM);
}

void triggerNPKSensor(int plantNo) {
  stopmotors();
  long leftDist = readUltrasonicDistance(ULTRASONIC_LEFT_PIN);
  long rightDist = readUltrasonicDistance(ULTRASONIC_RIGHT_PIN);

  String turnDirection = "";

  if (leftDist <= PLANT_DETECTION_DISTANCE_CM) {
    Serial.println("🌿 Turning LEFT 90°");
    driveMotors(100, -100);
    delay(500);
    turnDirection = "LEFT";
  } else if (rightDist <= PLANT_DETECTION_DISTANCE_CM) {
    Serial.println("🌿 Turning RIGHT 90°");
    driveMotors(-100, 100);
    delay(500);
    turnDirection = "RIGHT";
  }
  stopmotors();

  Serial.printf("📤 Sending: TRIGGER,PLANT:%d\n", plantNo); // TRIGGER,PLANT:1
  Serial.println("TRIGGER,PLANT:" + String(plantNo));

  waitingForNPK = true;
  npkDataReceived = false;

  unsigned long start = millis();
  while (!npkDataReceived && millis() - start < 30000) {
    handleSerial();
    delay(100);
  }

  if (!npkDataReceived) {
    Serial.println("❌ Timeout waiting for NPK");
  }

  // Turn back to line after data received
  if (turnDirection == "LEFT") {
    Serial.println("↩️ Returning RIGHT to line");
    driveMotors(-100, 100);
    delay(500);
  } else if (turnDirection == "RIGHT") {
    Serial.println("↩️ Returning LEFT to line");
    driveMotors(100, -100);
    delay(500);
  }

  stopmotors();
}


void handleSerial() {
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, msg) == DeserializationError::Ok) {
      String payload;
      serializeJson(doc, payload);
      sendJsonViaESPNow(payload);
      if (doc["status"] == "DONE") {
        Serial.println("✅ DONE received. Resetting counter...");
        plantCounter = 0;
      }
      if (doc["N"] != 0) {
        Serial.println("✅ data received. Resetting counter...");
        npkDataReceived = true;
      }
    } else {
      Serial.println("❌ JSON parse error");
    }
  }
}

void sendJsonViaESPNow(String json) {
  const char *payload = json.c_str();
  esp_err_t result = esp_now_send(gatewayAddress, (uint8_t *)payload, strlen(payload) + 1);
  if (result != ESP_OK) Serial.println("❌ ESP-NOW send failed");
}

void onEspNowReceive(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  String jsonStr = "";
  for (int i = 0; i < len; i++) jsonStr += (char)incomingData[i];
  Serial.println(jsonStr);
  StaticJsonDocument<64> doc;
  if (deserializeJson(doc, jsonStr) == DeserializationError::Ok && doc["value"] == true) {
    patrolEnabled = true;
    justStarted = true;
    startBoostStartTime = millis();
    Serial.println("🚀 START command received. Patrol begins...");
  }
}

int readSensors() {
  int wsum = 0, sum = 0;
  for (int i = 0; i < num_sensors; i++) {
    int val = digitalRead(IR_pins[i]);
    wsum += val * i;
    sum += val;
  }
  return sum == 0 ? (num_sensors - 1) / 2 : wsum / sum;
}

bool isLineEnded() {
  for (int i = 0; i < num_sensors; i++) {
    if (digitalRead(IR_pins[i]) == 1) return false;
  }
  return true;
}

void turn180() {
  driveMotors(100, -100);
  delay(800);
  stopmotors();
}

void driveMotors(int left, int right) {
  static int lastLeft = 0, lastRight = 0;

  // If motors are starting from 0
  if ((lastLeft == 0 && lastRight == 0) && (left != 0 || right != 0)) {
    justStarted = true;
    startBoostStartTime = millis();
  }

  // Apply boost speed
  if (justStarted && (millis() - startBoostStartTime < START_BOOST_DURATION_MS)) {
    if (left > 0) left = max(left, BASE_SPEED_STARTING);
    if (left < 0) left = min(left, -BASE_SPEED_STARTING);
    if (right > 0) right = max(right, BASE_SPEED_STARTING);
    if (right < 0) right = min(right, -BASE_SPEED_STARTING);
  } else {
    justStarted = false;
  }

  analogWrite(motor_left_forward, left > 0 ? left : 0);
  analogWrite(motor_left_backward, left < 0 ? -left : 0);
  analogWrite(motor_right_forward, right > 0 ? right : 0);
  analogWrite(motor_right_backward, right < 0 ? -right : 0);

  lastLeft = left;
  lastRight = right;
}

void stopmotors() {
  driveMotors(0, 0);
  delay(500);
}

long readUltrasonicDistance(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delayMicroseconds(2);
  digitalWrite(pin, HIGH);
  delayMicroseconds(10);
  digitalWrite(pin, LOW);
  pinMode(pin, INPUT);
  long dur = pulseIn(pin, HIGH, 30000);
  return dur * 0.034 / 2;
}

void handleButton() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading == LOW && lastButtonState == HIGH && !patrolEnabled) {
    patrolEnabled = true;
    justStarted = true;
    startBoostStartTime = millis();
    Serial.println("🟢 BOOT button pressed. Starting patrol...");
  }

  lastButtonState = reading;
}

bool isObstacleDetected() {
  long frontDist = readUltrasonicDistance(ULTRASONIC_FRONT_PIN);
  Serial.printf("🔎 Obstacle Distance: %ld cm\n", frontDist);
  return (frontDist > 0 && frontDist < OBSTACLE_DISTANCE_CM);
}
