#include <esp_now.h>
#include <WiFi.h>

// Structure to hold sent and received data
typedef struct struct_message {
  char message[50]; // Message buffer to hold the string
} struct_message;

struct_message sendData; // Data to send
struct_message recData;  // Data received

// Slave MAC address (to be set after receiving it during the broadcast phase)
uint8_t slaveMac[6];
bool slaveMacSet = false; // Flag to indicate if the slave's MAC address is saved

// ESP-NOW peer information
esp_now_peer_info_t peerInfo;

// Callback function executed when data is received
void onDataReceive(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&recData, incomingData, sizeof(recData));

  // Convert MAC address to string
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.print("Data received from: ");
  Serial.println(macStr);
  Serial.print("Message: ");
  Serial.println(recData.message);

  // If the message is "searching for a master" and the slave is not set
  if (strcmp(recData.message, "searching for a master") == 0 && !slaveMacSet) {
    Serial.println("Slave found. Saving slave's MAC address.");

    // Save slave's MAC address
    memcpy(slaveMac, mac, 6);
    slaveMacSet = true;

    // Add the slave as a peer
    memcpy(peerInfo.peer_addr, slaveMac, 6);
    peerInfo.channel = 1; // Same Wi-Fi channel
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add slave as a peer");
    } else {
      Serial.println("Slave added as a peer successfully");

      // Send confirmation to the slave
      strcpy(sendData.message, "you are my slave");
      esp_err_t result = esp_now_send(slaveMac, (uint8_t *)&sendData, sizeof(sendData));
      if (result == ESP_OK) {
        Serial.println("Sent 'you are my slave' message to the slave.");
      } else {
        Serial.println("Error sending 'you are my slave' message.");
      }
    }
  }
}

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  Serial.println("ESP-NOW Master");

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the receive callback
  esp_now_register_recv_cb(onDataReceive);
}

void loop() {
  
}