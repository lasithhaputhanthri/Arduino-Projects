#include <SPI.h>
#include <LoRa.h>

// Pin definitions for the LoRa module
#define ss 5
#define rst 14
#define dio0 2

// Initialize variables
String receivedData = "";   // Data received from another device
String outgoingData = "";   // Data to be sent

byte myAddress = 0xAB;             // Address of this ESP32
byte targetAddress = 0xCD;         // Address of the target device

unsigned long lastTransmission = 0;   // Track last transmission time
const long transmissionInterval = 1200;  // Interval for sending messages (ms)

// Function to transmit data
void transmitData(String payload, byte target) {
  LoRa.beginPacket();                // Begin the LoRa packet
  LoRa.write(target);                // Target device address
  LoRa.write(myAddress);             // Sender's address
  LoRa.write(payload.length());      // Length of the payload
  LoRa.print(payload);               // The actual payload
  LoRa.endPacket();                  // Finalize the packet
}

// Function to process received data
void processIncomingData(int packetSize) {
  if (packetSize == 0) return;       // Exit if no data received

  int recipient = LoRa.read();       // Read the recipient's address
  byte sender = LoRa.read();         // Read the sender's address
  byte dataLength = LoRa.read();     // Read the length of the incoming data

  receivedData = "";                 // Clear the received data buffer

  // Read the incoming message
  while (LoRa.available()) {
    receivedData += (char)LoRa.read();
  }

  // Validate the length of the message
  if (dataLength != receivedData.length()) return; 

  // Ignore messages not intended for this device
  if (recipient != myAddress) return;

  // Print details of the received message
  Serial.println("From: 0x" + String(sender, HEX));
  Serial.println("Content: " + receivedData);
}

void setup() {
  Serial.begin(115200);           // Initialize serial communication

  LoRa.setPins(ss, rst, dio0);    // Assign pins for the LoRa module

  if (!LoRa.begin(433E6)) {       // Initialize LoRa at 433 MHz
    while (true);                 // Halt if initialization fails
  }
}

void loop() {
  unsigned long currentMillis = millis();  // Track current time

  // Check if it's time to send a new message
  if (currentMillis - lastTransmission >= transmissionInterval) {
    lastTransmission = currentMillis;

    outgoingData = "Ping";   // Define the data to be sent
    Serial.println("Sending to Slave: " + outgoingData);
    transmitData(outgoingData, targetAddress);  // Transmit the message
  }

  // Process incoming packets
  processIncomingData(LoRa.parsePacket());
}
