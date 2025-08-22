#include <WiFi.h>
#include <WiFiServer.h>

// Wi-Fi credentials
const char* ssid = "LasithWifi";
const char* password = "12345678";

// Modbus TCP server (ESP32 slave) settings
WiFiServer server(502); // Modbus TCP runs on port 502

// Pin definition for GPIO16
const int pin16 = 16;

// Variables for relay blinking
bool toggleRelay = false; // Relay toggle state
unsigned long previousMillis = 0;
const unsigned long blinkInterval = 500; // Relay toggle interval (500ms)

void setup() {
  Serial.begin(115200);

  // Initialize GPIO16 as output
  pinMode(pin16, OUTPUT);
  digitalWrite(pin16, LOW); // Ensure pin is off initially

  // Connect to Wi-Fi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("Slave IP: ");
  Serial.println(WiFi.localIP());

  // Start the Modbus TCP server
  server.begin();
  Serial.println("Modbus TCP Server started. Waiting for requests...");
}

void loop() {
  // Listen for incoming client connections
  WiFiClient client = server.available();
  if (client) {
    // Client connected, process the request
    Serial.println("Client connected.");
    handleModbusRequest(client);

    // After handling the request, close the connection to wait for the next client
    client.stop();
    Serial.println("Client disconnected.");
  }

  // Non-blocking relay toggle logic
  if (toggleRelay) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= blinkInterval) {
      previousMillis = currentMillis;
      digitalWrite(pin16, !digitalRead(pin16)); // Toggle relay
    }
  } else {
    digitalWrite(pin16, LOW); // Ensure relay stays off when toggleRelay is false
  }
}

void handleModbusRequest(WiFiClient& client) {
  byte request[12];
  byte response[256]; // Response buffer
  int bytesRead = client.read(request, sizeof(request));

  if (bytesRead > 0) {
    Serial.print("Bytes received: ");
    Serial.println(bytesRead);

    // Check if the Modbus function code is related to writing a coil (0x05)
    if (request[7] == 0x05) {
      uint16_t coilAddress = (request[8] << 8) | request[9];  // Read coil address from request
      uint16_t coilValue = (request[10] << 8) | request[11];  // Read coil value from request

      // Debugging statements for coil address and value
      Serial.print("Coil Address: 0x");
      Serial.println(coilAddress, HEX);
      Serial.print("Coil Value: 0x");
      Serial.println(coilValue, HEX);

      // If the coil address matches the one we're interested in
      if (coilAddress == 0x0001) {
        if (coilValue == 0xFF00) { // Turn on blinking
          toggleRelay = true;
          Serial.println("Relay blinking started.");
        } else if (coilValue == 0x0000) { // Turn off blinking
          toggleRelay = false;
          Serial.println("Relay blinking stopped.");
        }
      }

      // Prepare and send a response to acknowledge the coil write command
      byte response[12];
      response[0] = request[0];  // Transaction ID
      response[1] = request[1];
      response[2] = request[2];  // Protocol ID
      response[3] = request[3];
      response[4] = 0x00;        // Length
      response[5] = 0x06;
      response[6] = request[6];  // Unit ID
      response[7] = 0x05;        // Function code (Write Single Coil)
      response[8] = request[8];  // Coil address high byte
      response[9] = request[9];  // Coil address low byte
      response[10] = request[10]; // Value high byte
      response[11] = request[11]; // Value low byte

      client.write(response, sizeof(response));  // Send the response
      Serial.println("Coil write response sent.");
    } else {
      Serial.print("Unsupported Function Code: ");
      Serial.println(request[7], HEX);
    }
  } else {
    Serial.println("No bytes received.");
  }
}
