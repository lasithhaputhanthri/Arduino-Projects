#include <LoRa.h>
#define SS 5
#define RST 14
#define DIO0 2
#define ledpin 27

int Level = 0;

void setup() {
  Serial.begin(115200);
  pinMode(ledpin, OUTPUT);       // Set ledpin as output
  digitalWrite(ledpin, LOW);
  Serial.println("Starting LoRa RX");

  LoRa.setPins(SS, RST, DIO0);

  int maxRetries = 10;
  int retryCount = 0;

  while (!LoRa.begin(433E6) && retryCount < maxRetries) {
    Serial.print("LoRa Initialization Failed. Attempt ");
    Serial.println(retryCount + 1);
    digitalWrite(ledpin, HIGH);
    delay(300);
    digitalWrite(ledpin, LOW);
    delay(300);
    retryCount++;
  }

  if (retryCount == maxRetries) {
    Serial.println("LoRa Initialization Failed After Maximum Retries");
    while (1); // Stop execution
  }

  Serial.println("LoRa RX Ready");
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
}

void loop() {
  int packetSize = LoRa.parsePacket();

  if (packetSize) {
    String received_message = "";

    while (LoRa.available()) {
      received_message += (char)LoRa.read();
    }

    Level = received_message.toInt(); // Convert received data to integer
    Serial.print("Received Level: ");
    Serial.println(Level);

    if (Level > 0) { // Validate non-zero values
      digitalWrite(ledpin, HIGH);
      delay(200);
    }
  } else {
    digitalWrite(ledpin, LOW);
  }

  delay(1000); // Adjust delay as needed
}
