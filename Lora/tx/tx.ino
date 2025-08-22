#include <SPI.h>
#include <LoRa.h>

#define ss 5
#define rst 14
#define dio0 2

int counter = 0;

void setup() 
{
  Serial.begin(115200);
  while (!Serial);
  Serial.println("LoRa Sender");

  
  int maxRetries = 10;
  int retryCount = 0;

  LoRa.setPins(ss, rst, dio0);

  while (!LoRa.begin(433E6) && retryCount < maxRetries) {
    Serial.print("LoRa Initialization Failed. Attempt ");
    Serial.println(retryCount + 1);
    retryCount++;
  }

  if (retryCount == maxRetries) {
    Serial.println("LoRa Initialization Failed After Maximum Retries");
    while (1); // Stop execution
  }

  // Set consistent parameters with receiver
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);

  Serial.println("LoRa TX Ready");
}

void loop() 
{
  Serial.print("Sending packet: ");
  Serial.println(counter);

  // Send purely numeric data
  LoRa.beginPacket();
  LoRa.println(counter); // Only send the counter value
  LoRa.endPacket();

  counter++;
  delay(3000); // Adjust the delay as needed
}
