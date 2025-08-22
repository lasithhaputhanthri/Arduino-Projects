#include <WiFi.h>
#include <esp_now.h>
#include <esp_sleep.h>

#define LED_PIN 2      // GPIO for LED
#define WAKEUP_TIME 4  // Wake-up time in seconds

// MAC Address of ESP32 B (Receiver)
uint8_t receiverMAC[] = {0xcc, 0xdb, 0xa7, 0x16, 0x18, 0x60};  // Updated MAC Address

// Struct to send data
typedef struct struct_message {
    int sensorData;
} struct_message;

struct_message myData;

// Flag to track the send status
volatile bool sendSuccess = false;

// Callback when message is sent
void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
    if (sendSuccess) {
        Serial.println("Sent!");
    } else {
        Serial.println("Failed to send, retrying...");
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Init Failed");
        return;
    }

    esp_now_register_send_cb(onSent);
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, receiverMAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }
}

void loop() {
    // Blink LED to indicate wakeup
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);

    // Send sensor data until success
    myData.sensorData = random(0, 100); // Simulating sensor data
    sendSuccess = false;  // Reset flag before sending

    while (!sendSuccess) {
        Serial.println("Sending data...");
        esp_now_send(receiverMAC, (uint8_t *)&myData, sizeof(myData));

        // Wait for the send callback to update the send status
        delay(100); // Adjust delay if necessary (to prevent excessive polling)
    }

    // Data sent successfully, go to sleep
    Serial.println("Data sent, going to sleep...");
    esp_sleep_enable_timer_wakeup(WAKEUP_TIME * 1000000);
    esp_deep_sleep_start();
}
