#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// --- LoRa Configuration ---
#define SS      5
#define RST     14
#define DIO0    2

#define LOCAL_ADDRESS     0x01
#define EXPECTED_SENDER   0x02

// --- WiFi Credentials ---
#define WIFI_SSID "LasithWifi"
#define WIFI_PASSWORD "12345678"

// --- Firebase Credentials ---
#define USER_EMAIL "projectbellpepper@gmail.com"
#define USER_PASSWORD "BellPepper2024"
#define FIREBASE_HOST "https://project-bell-pepper-default-rtdb.firebaseio.com/"
#define FIREBASE_API_KEY "AIzaSyC3_OGQOuyo-WH4rzCxaz0zDOWR6i9KeIY"

// --- Firebase Objects ---
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// --- Setup ---
void setup() {
  Serial.begin(115200);
  while (!Serial);

  // --- Init LoRa ---
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("❌ LoRa init failed");
    while (true);
  }
  Serial.println("✅ LoRa Receiver Initialized");
  LoRa.receive();

  // --- Connect to WiFi ---
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("🌐 Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected!");

  // --- Firebase Init ---
  config.api_key = FIREBASE_API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = FIREBASE_HOST;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// --- Main Loop ---
void loop() {
  if (LoRa.parsePacket()) {
    byte recipient = LoRa.read();
    byte sender = LoRa.read();
    byte length = LoRa.read();

    if (recipient != LOCAL_ADDRESS || sender != EXPECTED_SENDER) {
      while (LoRa.available()) LoRa.read();
      return;
    }

    String message = "";
    while (LoRa.available()) message += (char)LoRa.read();

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      Serial.println("❌ JSON parse error: " + String(error.c_str()));
      return;
    }

    float humidity      = doc["Humidity"] | 0;
    float temperature   = doc["Temperature"] | 0;
    float waterPH       = doc["GreenHouse1"]["waterPH"] | 0;
    float targetPH      = doc["GreenHouse1"]["targetPH"] | 0;
    float mixer         = doc["GreenHouse1"]["mixerLevel"] | 0;
    float supply        = doc["GreenHouse1"]["supplyLevel"] | 0;
    float rain          = doc["GreenHouse1"]["rainLevel"] | 0;
    float ph            = doc["GreenHouse1"]["SoilPH"] | 0;
    float moisture      = doc["GreenHouse1"]["SoilMoisture"] | 0;

    // 🌐 Push to Firebase
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Green_House_Sensors/Humidity", humidity);
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Green_House_Sensors/Temperature", temperature);
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Soil_Data/PH", ph);
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Soil_Data/Moisture/0", moisture);
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Tank_PH/Acid/0", waterPH);
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Target_PH", targetPH);
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Water_Tank_Data/Balancing", mixer);
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Water_Tank_Data/Supply", supply);
Firebase.RTDB.setFloat(&firebaseData, "/GreenHouse_1/Water_Tank_Data/rainwater", rain);


    // ✅ Serial print in one row (tab-separated)
    Serial.printf("💧Hum: %.1f\t🌡Temp: %.1f\tpH: %.1f\tMoist: %.1f\tW.pH: %.1f\tTgt.pH: %.1f\tMix: %.1f\tSup: %.1f\tRain: %.1f\n",
                  humidity, temperature, ph, moisture, waterPH, targetPH, mixer, supply, rain);
  }

  delay(10);
}
