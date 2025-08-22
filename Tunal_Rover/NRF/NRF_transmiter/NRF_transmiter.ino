#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <ArduinoJson.h>

// HSPI Pins for NRF24L01
#define CE_PIN 26
#define CSN_PIN 27

RF24 radio(CE_PIN, CSN_PIN);

// Custom SPI instance for HSPI
SPIClass SPI_HSPI(HSPI);
const byte address[6] = "00001";

#define LEFT_PIN 36
#define RIGHT_PIN 39

int VRX;
int VRY;

void setup() {
  Serial.begin(115200);

  // Initialize HSPI with correct settings
   SPI_HSPI.begin(14, 12, 13, 27);
  radio.begin(&SPI_HSPI);

  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_250KBPS);
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
}

void loop() {
  StaticJsonDocument<1024> doc;

  VRX = analogRead(LEFT_PIN) - 2048;
  VRY = analogRead(RIGHT_PIN) - 2048;

  doc["X"] = VRX;
  doc["Y"] = VRY;

  char output[32];  // Fixed-size buffer
  size_t len = serializeJson(doc, output, sizeof(output));

  Serial.print(VRX);
  Serial.print(",");
  Serial.println(VRY);

  // Send a message
  if (len > 0) {
    radio.stopListening();  // Switch to transmit mode
    bool success = radio.write(output, len);

    if (success) {
      Serial.println("Message Sent Successfully!");
    } else {
      Serial.println("Message Failed!");
    }
  }

  // Switch to receive mode to check for messages
  radio.startListening();

  // Check if there is a message available
  if (radio.available()) {
    char receivedText[32] = {0};  // Buffer for received data
    radio.read(receivedText, sizeof(receivedText));
    Serial.print("Received: ");
    Serial.println(receivedText);
  }
}
