#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// Custom HSPI Pins
#define CE_PIN 26
#define CSN_PIN 27

RF24 radio(CE_PIN, CSN_PIN);

// Custom SPI instance for HSPI
SPIClass SPI_HSPI(HSPI);

const byte address[6] = "00001";

void setup() {
    Serial.begin(115200);

    // Initialize HSPI with custom pins
    SPI_HSPI.begin(14, 12, 13, 27);  // SCK, MISO, MOSI, CS
    radio.begin(&SPI_HSPI);
    
    radio.setPALevel(RF24_PA_HIGH);
    radio.setDataRate(RF24_250KBPS);
    radio.openReadingPipe(1, address);
    radio.startListening(); // Set as receiver
    Serial.println("Setup Done");
}

void loop() {
    //Serial.println("loop");
    if (radio.available()) {
        char receivedText[32] = "";
        radio.read(&receivedText, sizeof(receivedText));
        
        Serial.print("Received: ");
        Serial.println(receivedText);
    }
}
