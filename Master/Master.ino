#include <WiFi.h>

// Wi-Fi credentials
const char* ssid = "LasithWifi";
const char* password = "12345678";

// Modbus server (slave) IP and port
const char* modbusServerIP = "192.168.192.142";  // Update with the slave ESP32 IP
const int modbusPort = 502;

// Create WiFi client object
WiFiClient client;

void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("Master IP: ");
  Serial.println(WiFi.localIP());

  // Attempt to connect to Modbus server (slave)
  if (!connectToModbusServer()) {
    Serial.println("Failed to connect to Modbus server.");
  }
}

void loop() {
  // Ensure connection is still alive
  if (!client.connected()) {
    Serial.println("Reconnecting to Modbus server...");
    if (!connectToModbusServer()) {
      Serial.println("Reconnection failed. Will retry in 5 seconds.");
      delay(5000);
      return;  // Skip sending request if unable to reconnect
    }
  }

  // Check for serial input
  if (Serial.available() > 0) {
    int input = Serial.parseInt();  // Parse numeric input from Serial Monitor
    if (input >= 0 && input <= 255) {
      sendModbusRequest(input);  // Send the numeric value as the coil state
    } else {
      Serial.println("Invalid input. Enter a value between 0 and 255.");
    }
  }

  delay(500);  // Wait before checking for more input
}

bool connectToModbusServer() {
  // Attempt to connect to Modbus server (slave)
  Serial.println("Attempting to connect to Modbus server...");
  if (client.connect(modbusServerIP, modbusPort)) {
    Serial.println("Connected to Modbus server.");
    return true;  // Successfully connected
  }
  return false;  // Failed to connect
}

void sendModbusRequest(int value) {
  byte request[12];  // Modbus TCP request
  
  // Fill MBAP header (Transaction ID, Protocol ID, Length, Unit ID)
  request[0] = 0x00;  // Transaction ID (high byte)
  request[1] = 0x01;  // Transaction ID (low byte)
  request[2] = 0x00;  // Protocol ID (high byte)
  request[3] = 0x00;  // Protocol ID (low byte)
  request[4] = 0x00;  // Length (high byte)
  request[5] = 0x06;  // Length (low byte)
  request[6] = 0x01;  // Unit ID (Slave address)
  
  // Modbus Function Code (5 = Write Single Coil)
  request[7] = 0x05;  
  request[8] = 0x00;  // Address high byte
  request[9] = 0x00;  // Address low byte
  request[10] = (byte)value;  // Value to write (numeric input)
  request[11] = 0x00;         // Padding byte

  // Send the Modbus TCP request
  client.write(request, sizeof(request));
  Serial.println("Modbus request sent.");

  // Print out the request in hex format for debugging
  Serial.print("Request sent: ");
  for (int i = 0; i < sizeof(request); i++) {
    Serial.print(request[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Read the response
  readModbusResponse();
}

void readModbusResponse() {
  byte response[256];  // Response buffer
  int bytesRead = client.read(response, sizeof(response));

  if (bytesRead > 0) {
    Serial.print("Received response: ");
    for (int i = 0; i < bytesRead; i++) {
      Serial.print(response[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  } else {
    Serial.println("No response received.");
  }
}
