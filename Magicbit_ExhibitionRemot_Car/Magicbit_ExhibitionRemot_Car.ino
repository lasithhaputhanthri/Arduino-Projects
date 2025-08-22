#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

#define R_MOTOR_A 17  // Right motor A-IA
#define R_MOTOR_B 16  // Right motor A-IB
#define L_MOTOR_A 18  // Left motor B-IA
#define L_MOTOR_B 27  // Left motor B-IB

#define NEOPIXEL_PIN_1 32  // NeoPixel on pin 32
#define NEOPIXEL_PIN_2 33  // NeoPixel on pin 33
#define NUM_PIXELS 8       // Adjust if you have more or fewer NeoPixels

String command;            // String to store app command state.
int speedCar = 1023;       // Set speed to full (maximum PWM value: 1023)
const char* ssid = "ESP32 Car";

WebServer server(80);
Adafruit_NeoPixel strip1(NUM_PIXELS, NEOPIXEL_PIN_1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(NUM_PIXELS, NEOPIXEL_PIN_2, NEO_GRB + NEO_KHZ800);

void setup() {
  // Configure motor pins as outputs
  pinMode(R_MOTOR_A, OUTPUT);
  pinMode(R_MOTOR_B, OUTPUT);
  pinMode(L_MOTOR_A, OUTPUT);
  pinMode(L_MOTOR_B, OUTPUT);

  // Configure NeoPixel pins with internal pull-down resistors
  pinMode(NEOPIXEL_PIN_1, INPUT_PULLDOWN);
  pinMode(NEOPIXEL_PIN_2, INPUT_PULLDOWN);

  // Initialize NeoPixels
  strip1.begin();
  strip2.begin();
  strip1.show();  // Initialize all pixels to 'off'
  strip2.show();  // Initialize all pixels to 'off'

  stopRobot();  // Stop the robot initially

  Serial.begin(115200);

  // Setup Wi-Fi Access Point
  WiFi.softAP(ssid);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  // Start web server
  server.on("/", HTTP_handleRoot);
  server.onNotFound(HTTP_handleRoot);
  server.begin();
}

void setNeoPixelColor(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    strip1.setPixelColor(i, strip1.Color(r, g, b));
    strip2.setPixelColor(i, strip2.Color(r, g, b));
  }
  strip1.show();
  strip2.show();
}

// Function to display rainbow pattern
void rainbow() {
  for (int i = 0; i < NUM_PIXELS; i++) {
    int color = (i * 256 / NUM_PIXELS);  // Gradual color change
    strip1.setPixelColor(i, strip1.Color(color, 255 - color, 128));
    strip2.setPixelColor(i, strip2.Color(color, 255 - color, 128));
  }
  strip1.show();
  strip2.show();
}

void goAhead() {
  analogWrite(R_MOTOR_A, speedCar);  // Right motor forward
  analogWrite(R_MOTOR_B, 0);
  analogWrite(L_MOTOR_A, speedCar);  // Left motor forward
  analogWrite(L_MOTOR_B, 0);

  rainbow();  // Activate rainbow pattern when moving forward
  Serial.println("Forward");
}

void goBack() {
  analogWrite(R_MOTOR_A, 0);         // Right motor backward
  analogWrite(R_MOTOR_B, speedCar);
  analogWrite(L_MOTOR_A, 0);         // Left motor backward
  analogWrite(L_MOTOR_B, speedCar);

  rainbow();  // Activate rainbow pattern when moving backward
  Serial.println("Backward");
}

void goRight() {
  analogWrite(R_MOTOR_A, speedCar);  // Right motor forward
  analogWrite(R_MOTOR_B, 0);
  analogWrite(L_MOTOR_A, 0);         // Left motor backward
  analogWrite(L_MOTOR_B, speedCar);

  rainbow();  // Activate rainbow pattern when turning right
  Serial.println("Right");
}

void goLeft() {
  analogWrite(R_MOTOR_A, 0);         // Right motor backward
  analogWrite(R_MOTOR_B, speedCar);
  analogWrite(L_MOTOR_A, speedCar);  // Left motor forward
  analogWrite(L_MOTOR_B, 0);

  rainbow();  // Activate rainbow pattern when turning left
  Serial.println("Left");
}

void stopRobot() {
  analogWrite(R_MOTOR_A, 0);
  analogWrite(R_MOTOR_B, 0);
  analogWrite(L_MOTOR_A, 0);
  analogWrite(L_MOTOR_B, 0);

  setNeoPixelColor(0, 0, 0);  // Turn off NeoPixels
  Serial.println("Stopped");
}

void loop() {
  server.handleClient();

  command = server.arg("State");
  if (command == "F") goAhead();
  else if (command == "B") goBack();
  else if (command == "L") goLeft();
  else if (command == "R") goRight();
  else if (command == "0") speedCar = 100;   // Adjust speeds as needed
  else if (command == "1") speedCar = 200;
  else if (command == "2") speedCar = 300;
  else if (command == "3") speedCar = 400;
  else if (command == "4") speedCar = 500;
  else if (command == "5") speedCar = 600;
  else if (command == "6") speedCar = 700;
  else if (command == "7") speedCar = 800;
  else if (command == "8") speedCar = 900;
  else if (command == "9") speedCar = 1023;  // Full speed
  else if (command == "S") stopRobot();
}

void HTTP_handleRoot() {
  if (server.hasArg("State")) {
    Serial.println(server.arg("State"));
  }
  server.send(200, "text/html", "");
  delay(1);
}
