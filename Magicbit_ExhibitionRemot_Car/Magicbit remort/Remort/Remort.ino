#include <esp_now.h>
#include <WiFi.h>

const int buttonPin = 35;  // Button pin
const int potPin = 39;     // Potentiometer pin (analog)

// Define the MAC address of the robot (Global declaration)
uint8_t robotMacAddr[] = {0x94, 0xE6, 0x86, 0x05, 0x44, 0xB8};  // Replace with your robot's MAC address

struct dataPackage {
  bool buttonState;
  int leftMotorSpeed;   // Speed for left motor (0-255)
  int rightMotorSpeed;  // Speed for right motor (0-255)
};

dataPackage sendData;

void setup() {
  Serial.begin(115200);

  pinMode(buttonPin, INPUT);
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed");
    return;
  }
  Serial.println("ESP-NOW initialization done");

  // Set up the peer (the robot's MAC address)
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, robotMacAddr, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

void loop() {
  sendData.buttonState = digitalRead(buttonPin);  // Check if button is pressed

  int potValue = analogRead(potPin);  // Read the potentiometer (0 to 4095)
  Serial.println(potValue);
  // Map potentiometer value to motor speeds (0-255 for each motor)
  //if (potValue < 2048) {
    // Right motor is faster (turn left)
  if (potValue<0){
    sendData.rightMotorSpeed = map(potValue, 0, 4096, 1023, 0);  // Right motor slow down
    sendData.leftMotorSpeed = map(4096-potValue, 0, 4096, 1023, 0);
    //sendData.leftMotorSpeed = 255;  // Left motor full speed
  } else {
    // Left motor is faster (turn right)
    sendData.rightMotorSpeed = map(4096-potValue, 0, 4096, 1023, 0);  // Right motor slow down
    sendData.leftMotorSpeed = map(potValue, 0, 4096, 1023, 0);
    //sendData.rightMotorSpeed = 255;  // Right motor full speed
  }

  // Send data to the robot
  esp_now_send(robotMacAddr, (uint8_t*)&sendData, sizeof(sendData));

  delay(100);  // Adjust delay as needed
}
