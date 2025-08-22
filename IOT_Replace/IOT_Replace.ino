#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>

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

// WiFi
const char *ssid = "LasithWifi";    // Enter your Wi-Fi name
const char *password = "12345678";  // Enter Wi-Fi password

// MQTT Broker
const char *mqtt_broker = "broker.emqx.io";
const char *topic = "vdl/replace";
const char *mqtt_username = "emqx";
const char *mqtt_password = "public";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

StaticJsonDocument<200> doc1;

// Define pins
const int voltagePin = 2;  // GPIO2 for Voltage
const int currentPin = 4;  // GPIO4 for Current
const int relayPin = 12;   // GPIO12 for Relay control
const int buttonPin = 13;  // GPIO13 for Pushbutton input
const int RED = 14;        // GPIO13 for Pushbutton input
const int GREEN = 25;      // GPIO13 for Pushbutton input
const int BLUE = 15;       // GPIO13 for Pushbutton input

// Task handles
TaskHandle_t mainTaskHandle;
TaskHandle_t communicationTaskHandle;

// Function prototypes
void mainTask(void *pvParameters);
void communicationTask(void *pvParameters);
void callback(char *topic, byte *payload, unsigned int length);

bool relayState = false;  // Tracks the relay state (ON/OFF)
bool lock = false;
bool OnOffswitch = false;
unsigned long lastDebounceTime = 0;  // Time of the last debounce
unsigned long debounceDelay = 50;    // Debounce delay in milliseconds
float Energy=0;
String slaveData="";

// Variables to store JSON data
String feedbackCommand = "";  // Example field to store data from MQTT feedback

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
  slaveData=recData.message;
  Serial.println(slaveData);

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
  pinMode(RED, OUTPUT);
  pinMode(GREEN, INPUT_PULLUP);
  pinMode(BLUE, OUTPUT);

  pinMode(voltagePin, INPUT);
  pinMode(currentPin, INPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);  // Button input with internal pull-up resistor

  // Initialize Serial
  Serial.begin(115200);

  // Connecting to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to Wi-Fi");

  // Connecting to MQTT broker
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
  while (!client.connected()) {
    String client_id = "esp32-client-";
    client_id += String(WiFi.macAddress());
    Serial.printf("The client %s connects to the public MQTT broker\n", client_id.c_str());
    if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Public EMQX MQTT broker connected");
    } else {
      Serial.print("Failed with state ");
      Serial.println(client.state());
      delay(2000);
    }
  }
  client.subscribe(topic);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the receive callback
  esp_now_register_recv_cb(onDataReceive);

  // Create tasks
  xTaskCreatePinnedToCore(mainTask, "Main Task", 10000, NULL, 1, &mainTaskHandle, 0);                             // Core 0
  xTaskCreatePinnedToCore(communicationTask, "Communication Task", 10000, NULL, 1, &communicationTaskHandle, 1);  // Core 1
}

void loop() {
  // Do nothing, tasks handle everything
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);

  String payloadStr;
  for (int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }
  Serial.print("Payload: ");
  Serial.println(payloadStr);
  DeserializationError error = deserializeJson(doc1, payloadStr);

  if (error) {
    Serial.print("JSON deserialization failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Check if the required keys are present in the JSON
  if (!doc1.containsKey("command") || !doc1.containsKey("LOCK")) {
    Serial.println("Missing command or LOCK in JSON payload");
    return;
  }

  OnOffswitch = doc1["command"];
  lock = doc1["LOCK"];

  Serial.print("Command: ");
  Serial.println(OnOffswitch);
  Serial.print("Lock: ");
  Serial.println(lock);

  if (!lock) {
    relayState = OnOffswitch;
    digitalWrite(relayPin, relayState ? HIGH : LOW);
    Serial.print("Relay state: ");
    Serial.println(relayState ? "ON" : "OFF");
  }
}


void mainTask(void *pvParameters) {

  static bool lastButtonState = LOW;  // Previous state of the button
  bool buttonState = LOW;             // Current state of the button

  while (true) {
    if (lock) {
      digitalWrite(RED, HIGH);
      digitalWrite(GREEN, HIGH);
      digitalWrite(BLUE, LOW);
    } else if (relayState) {
      digitalWrite(RED, LOW);
      digitalWrite(GREEN, LOW);
      digitalWrite(BLUE, HIGH);
    }else{
      digitalWrite(RED, HIGH);
      digitalWrite(GREEN, HIGH);
      digitalWrite(BLUE, HIGH);
    }
    buttonState = digitalRead(buttonPin);
    Energy =+ random(0.005,0.006);

    //Check if the button state has changed (button press detected)
    if (!lock && buttonState == HIGH && lastButtonState == LOW && (millis() - lastDebounceTime) > debounceDelay) {
      relayState = !relayState;                         // Toggle relay state
      digitalWrite(relayPin, relayState ? HIGH : LOW);  // Update relay
      Serial.print("Relay state: ");
      Serial.println(relayState ? "ON" : "OFF");  // Print relay state for debugging

      lastDebounceTime = millis();  // Reset debounce timer
    }

    lastButtonState = buttonState;  // Update the last button state

    // Read current and voltage values
    int voltageValue = analogRead(voltagePin);  // Read voltage from pin 2
    int currentValue = analogRead(currentPin);  // Read current from pin 4

    // Convert the analog readings to voltage (0-3.3V range)
    float voltage = voltageValue * (3.3 / 4095.0);  // ESP32 ADC range: 0-4095
    float current = currentValue * (3.3 / 4095.0);  // Same scaling


    // Print values to Serial Monitor
    Serial.print("Voltage: ");
    Serial.print(voltage);
    Serial.println(" V");

    Serial.print("Current: ");
    Serial.print(current);
    Serial.println(" A");

    delay(50);  // Adjust for fast sampling

    // Handle incoming MQTT messages
    client.loop();
    Serial.print("Command: ");
    Serial.println(OnOffswitch);
    Serial.print("Lock: ");
    Serial.println(lock);
  }
}

void communicationTask(void *pvParameters) {
  while (true) {
    // Create a JSON document
    StaticJsonDocument<200> doc;

    JsonObject RV0220A01 = doc.createNestedObject("RV0220A01");

    RV0220A01["wifi_status"] = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
    RV0220A01["mqtt_status"] = client.connected() ? "Connected" : "Disconnected";
    RV0220A01["relay_state"] = relayState ? "ON" : "OFF";
    RV0220A01["Voltage_Status"] = "Healthy";
    RV0220A01["Energy"] = Energy;
    // Create a JSON string

    JsonObject RV0220A02 = doc.createNestedObject("RV0220A02");
    RV0220A02["Data"] = slaveData;


    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);
    Serial.println(jsonBuffer);
    // Publish the JSON data to MQTT
    client.publish(topic, jsonBuffer);



    // Send data every 2 seconds
    delay(2000);
  }
}
