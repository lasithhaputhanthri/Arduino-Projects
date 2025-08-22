#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

// Define SoftwareSerial pins for ESP8266 (RX and TX)
SoftwareSerial mySerial(D5, D6); // D6 (RX), D5 (TX)

// Initialize the fingerprint sensor
Adafruit_Fingerprint finger(&mySerial);

void setup() {
  Serial.begin(115200);          // Serial monitor communication
  mySerial.begin(57600);         // Fingerprint sensor baud rate

  Serial.println("Fingerprint Enroll Example");
  // Check if the fingerprint sensor is detected
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor detected.");
  } else {
    Serial.println("Fingerprint sensor not detected. Please check wiring.");
    while (true); // Stop execution
  }

  finger.getTemplateCount(); // Get the number of fingerprints stored
  Serial.print("Fingerprints stored: ");
  Serial.println(finger.templateCount);
}

void loop() {
  int id;
  Serial.println("Enter an ID (1-127) to enroll a new fingerprint:");
  while (Serial.available() == 0); // Wait for user input
  id = Serial.parseInt();
  if (id < 1 || id > 127) {
    Serial.println("Invalid ID! Must be between 1 and 127.");
    return;
  }

  Serial.print("Enrolling ID #");
  Serial.println(id);
  enrollFingerprint(id);
}

// Function to enroll a fingerprint
void enrollFingerprint(int id) {
  int p = -1;
  Serial.println("Place your finger on the sensor.");

  // Wait for a finger to be placed
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) continue;
    if (p == FINGERPRINT_PACKETRECIEVEERR) Serial.println("Communication error");
    if (p == FINGERPRINT_IMAGEFAIL) Serial.println("Imaging error");
  }

  // Convert the image
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("Image conversion failed.");
    return;
  }

  Serial.println("Remove your finger.");
  delay(2000);

  Serial.println("Place the same finger again.");
  // Wait for the finger to be placed again
  p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) continue;
    if (p == FINGERPRINT_PACKETRECIEVEERR) Serial.println("Communication error");
    if (p == FINGERPRINT_IMAGEFAIL) Serial.println("Imaging error");
  }

  // Convert the second image
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("Image conversion failed.");
    return;
  }

  // Create a template and store it in memory
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    if (p == FINGERPRINT_ENROLLMISMATCH)
      Serial.println("Fingerprints do not match.");
    else
      Serial.println("Error creating fingerprint model.");
    return;
  }

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Fingerprint successfully enrolled!");
  } else {
    Serial.println("Error storing fingerprint.");
  }
}
