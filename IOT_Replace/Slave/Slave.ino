#include <esp_now.h>
#include <WiFi.h>

bool message_to_send = true;

// Broadcast MAC address
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Structure to hold sent and received data
typedef struct struct_message {
  char message[50]; // Message buffer to hold the string
} struct_message;

struct_message sendData; // Data to be sent
struct_message recData;  // Data received

// ESP-NOW peer information
esp_now_peer_info_t peerInfo;

// Callback function executed when data is received
void onDataReceive(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&recData, incomingData, sizeof(recData));
  // Convert MAC address to string
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  String macString = String(macStr);

  if (strcmp(recData.message, "you are my slave") == 0) {
    Serial.println("Received 'you are my slave'");
    message_to_send = false;
  }
}

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  Serial.println("ESP-NOW Broadcast Sender");

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the receive callback
  esp_now_register_recv_cb(onDataReceive);

  // Add the broadcast peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1; // Set the Wi-Fi channel
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    return;
  }
}

void loop() {
  static unsigned long lastSendTime = 0;
  const unsigned long sendInterval = 8000; // 8 seconds

  // Send data at regular intervals
  if ((millis() - lastSendTime >= sendInterval) && message_to_send) {
    lastSendTime = millis();

    // Populate the message to send
    strcpy(sendData.message, "searching for a master"); // Copy "searching for a master" into the struct

    // Send the data via ESP-NOW
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&sendData, sizeof(sendData));
    if (result == ESP_OK) {
      Serial.println("Broadcast message sent successfully");
    } else {
      Serial.println("Error sending broadcast message.");
    }
  }
}