#include <esp_now.h>
#include <WiFi.h>

#define motorA 13  // Pin for left motor
#define motorB 12  // Pin for right motor

struct dataPackage {
  bool buttonState;
  int leftMotorSpeed;   // Speed for left motor (0-255)
  int rightMotorSpeed;  // Speed for right motor (0-255)
};

dataPackage receivedData;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed");
    return;
  }

  // Set up the callback to receive data
  esp_now_register_recv_cb(onDataReceive);
}

void loop() {
  // If button is pressed, move robot
  if (receivedData.buttonState) {
    moveRobot(receivedData.leftMotorSpeed, receivedData.rightMotorSpeed);
  } else {
    stopRobot();  // Stop the robot if the button is not pressed
  }

  delay(100);  // Adjust delay as needed
}

void onDataReceive(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&receivedData, incomingData, sizeof(receivedData));
  Serial.print("Received Button: ");
  Serial.print(receivedData.buttonState);
  Serial.print(" Left Speed: ");
  Serial.print(receivedData.leftMotorSpeed);
  Serial.print(" Right Speed: ");
  Serial.println(receivedData.rightMotorSpeed);
}

void moveRobot(int leftSpeed, int rightSpeed) {
  // Motor control code based on speed values
  setMotorSpeed(leftSpeed, rightSpeed);
}

void stopRobot() {
  // Stop both motors
  setMotorSpeed(0, 0);
}

void setMotorSpeed(int leftSpeed, int rightSpeed) {
  // Control motors with PWM based on speed
  analogWrite(motorA, leftSpeed);  // Left motor PWM control
  analogWrite(motorB, rightSpeed); // Right motor PWM control
}
