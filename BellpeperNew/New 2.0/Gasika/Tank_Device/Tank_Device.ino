#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>]




#define SENSOR_MIXER 13
#define SENSOR_SUPPLY 15
#define SENSOR_RAIN 16
#define SENSOR_ACID 12
#define SENSOR_BASE 27

#define RELAY_MIXER 17
#define RELAY_SUPPLY 33
#define RELAY_RAIN 22
#define ACID_PUMP_PIN 32
#define BASE_PUMP_PIN 26


// --- Pins ---
#define PH_SENSOR_PIN 39
// #define ACID_PUMP_PIN 22
// #define BASE_PUMP_PIN 33

#define PH_TOLERANCE 0.1
#define MIXER_MOTOR_PIN 21

// #define RELAY_MIXER 17
// #define RELAY_SUPPLY 32
// #define RELAY_RAIN 26

// #define SENSOR_MIXER 13
// #define SENSOR_SUPPLY 15
// #define SENSOR_RAIN 12
// #define SENSOR_ACID 16
// #define SENSOR_BASE 27

// --- pH Calibration ---
#define PH4_VOLTAGE 1.95
#define PH7_VOLTAGE 1.725

// --- Tank Constants ---
#define TANK_HEIGHT 15
#define MIXER_LOW_THRESHOLD 20
volatile int MIXER_HIGH_THRESHOLD = 65;
#define RAIN_LOW_THRESHOLD 10

// --- PID Constants ---
double Kp = 1000,
       Ki = 0.1, Kd = 0.5;
#define MIN_ON_TIME 1000
#define MAX_ON_TIME 5000

#define CONFIRMATION_COUNT 5

int mixerFullCount = 0;
int mixerDispenseCount = 0;
bool isMixerFullConfirmed = false;
bool mixerDispenseConfirmed = false;

#define FULL_FILL_TIME_MS 1009560     // For 100% fill
#define FULL_DISPENSE_TIME_MS 816440  // For 100% dispense

unsigned long scaledFillTime = 0;
unsigned long scaledDispenseTime = 0;

unsigned long stateStartTime = 0;


volatile int WATER_DISPENSE_PERCENT = 10;  // % to dispense during command (adjust as needed)

// --- Global Variables ---
float waterPH = 7.0,
      targetWaterPH = 7.0;
float receivedSoilPH = 5.0;
float receivedSoilMoisture = 0.0;
double phError = 0, prevPhError = 0, integral = 0;
unsigned long lastPumpAction = 0;
float mixerLevel, rainLevel, supplyLevel, acidLevel, baseLevel;

bool phBalancingLocked = false;  // blocks further balancing after it's done once
bool mixerThresholdChanged = false;



#define MAX_UPTIME_BEFORE_RESET 60000
unsigned long lastBootTime = 0;

int phCorrectionCycleCount = 0;
const int MAX_CORRECTION_CYCLES = 10;

bool phRegulationComplete = false;

bool pumpCycleActive = false;
unsigned long pumpStartTime = 0;
int currentPumpDuration = 0;

// --- ESP-NOW Struct ---
typedef struct struct_message {
  float soilMoisture;
  float externalTemp;
  float soilPH;
} struct_message;

struct_message receivedData;
String incomingJsonBuffer = "";
bool newDataReceived = false;


// --- Replace this with actual peer MAC ---
uint8_t peerAddress[] = { 0xF8, 0xB3, 0xB7, 0x2B, 0x3B, 0x60 };

TaskHandle_t TaskControlHandle;

bool normalTankMode = true;
bool wateringActive = false;
float startingMixerLevel = 0.0;

enum SystemState {
  STATE_IDLE,
  STATE_REFILL,
  STATE_BALANCE_PH,
  STATE_DISPENSE
};

SystemState systemState = STATE_IDLE;



// --- Setup ---
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println(" ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onReceiveESPNow);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (!esp_now_is_peer_exist(peerAddress)) {
    esp_now_add_peer(&peerInfo);
  }

  pinMode(PH_SENSOR_PIN, INPUT);
  pinMode(ACID_PUMP_PIN, OUTPUT);
  pinMode(BASE_PUMP_PIN, OUTPUT);
  pinMode(MIXER_MOTOR_PIN, OUTPUT);


  pinMode(RELAY_MIXER, OUTPUT);
  pinMode(RELAY_SUPPLY, OUTPUT);
  pinMode(RELAY_RAIN, OUTPUT);

  pinMode(SENSOR_MIXER, INPUT);
  pinMode(SENSOR_SUPPLY, INPUT);
  pinMode(SENSOR_RAIN, INPUT);
  pinMode(SENSOR_ACID, INPUT);
  pinMode(SENSOR_BASE, INPUT);


  digitalWrite(ACID_PUMP_PIN, LOW);
  digitalWrite(BASE_PUMP_PIN, LOW);

  digitalWrite(RELAY_MIXER, LOW);
  digitalWrite(RELAY_SUPPLY, LOW);
  digitalWrite(RELAY_RAIN, LOW);

  lastBootTime = millis();

  xTaskCreatePinnedToCore(
    TaskControlLoop,     // function to run
    "ControlTask",       // task name
    4096,                // stack size
    NULL,                // parameter
    1,                   // priority
    &TaskControlHandle,  // task handle
    1                    // run on core 1 (APP_CPU)
  );
  xTaskCreatePinnedToCore(
    UltrasonicReadTask,  // Function to run
    "UltrasonicTask",    // Name
    4096,                // Stack size
    NULL,                // Parameter
    1,                   // Priority
    NULL,                // Task handle
    0                    // Core 0
  );
}

// --- Loop ---
void loop() {
  // optionally put debug messages here
}


void TaskControlLoop(void *parameter) {

  for (;;) {

    switch (systemState) {
      case STATE_IDLE:
        if (normalTankMode)
          ;
        break;

      case STATE_REFILL:
        {
          if (stateStartTime == 0) {
            stateStartTime = millis();
            if (rainLevel > RAIN_LOW_THRESHOLD) {
              digitalWrite(RELAY_RAIN, HIGH);
              digitalWrite(RELAY_SUPPLY, LOW);
            } else {
              digitalWrite(RELAY_RAIN, LOW);
              digitalWrite(RELAY_SUPPLY, HIGH);
            }
            Serial.println("🚰 Refill started based on scaled time...");
          }

          unsigned long scaledFillTime = (MIXER_HIGH_THRESHOLD * FULL_FILL_TIME_MS) / 100;
          if (millis() - stateStartTime >= scaledFillTime) {
            digitalWrite(RELAY_RAIN, LOW);
            digitalWrite(RELAY_SUPPLY, LOW);
            stateStartTime = 0;
            systemState = STATE_BALANCE_PH;
            Serial.println("✅ Scaled refill complete. Moving to pH balancing.");
          }
          break;
        }

      case STATE_BALANCE_PH:
        waterPH = readPH();
        targetWaterPH = calculateRequiredWaterPH(receivedSoilPH);
        regulateWaterSolutionPH(waterPH, targetWaterPH);

        if (phRegulationComplete) {
          systemState = STATE_DISPENSE;
          startingMixerLevel = mixerLevel;
          digitalWrite(RELAY_MIXER, HIGH);
          Serial.println("✅ pH Balanced. Dispensing now...");
        }
        break;

      case STATE_DISPENSE:
        if (stateStartTime == 0) {
          stateStartTime = millis();
          digitalWrite(RELAY_MIXER, HIGH);
          Serial.println("🚿 Dispensing started based on scaled time...");
        }

        unsigned long scaledDispenseTime = (WATER_DISPENSE_PERCENT * FULL_DISPENSE_TIME_MS) / 100;
        if (millis() - stateStartTime >= scaledDispenseTime) {
          digitalWrite(RELAY_MIXER, LOW);
          digitalWrite(ACID_PUMP_PIN, LOW);
          digitalWrite(BASE_PUMP_PIN, LOW);
          digitalWrite(MIXER_MOTOR_PIN, LOW);

          pumpCycleActive = false;
          phCorrectionCycleCount = 0;

          normalTankMode = true;
          wateringActive = false;
          systemState = STATE_IDLE;
          stateStartTime = 0;

          Serial.println("✅ Scaled dispense complete. Back to normal mode.");
        }
        break;
    }


    MainTask();


    if (newDataReceived) {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, incomingJsonBuffer);

      if (!error) {
        if (doc.containsKey("ph")) receivedSoilPH = doc["ph"];
        if (doc.containsKey("moisture")) receivedSoilMoisture = doc["moisture"];

        Serial.println(" Soil pH Received: " + String(receivedSoilPH) + ", Soil Moisture Received: " + String(receivedSoilMoisture));
      } else {
        Serial.print(" JSON Error: ");
        Serial.println(error.c_str());
      }

      newDataReceived = false;
    }


    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}


// --- pH Sensor Reading ---
float readPH() {
  int buffer[10];
  unsigned long avg = 0;
  for (int i = 0; i < 10; i++) {
    buffer[i] = analogRead(PH_SENSOR_PIN);
    delay(30);
  }

  for (int i = 0; i < 9; i++)
    for (int j = i + 1; j < 10; j++)
      if (buffer[i] > buffer[j]) std::swap(buffer[i], buffer[j]);

  for (int i = 2; i < 8; i++) avg += buffer[i];
  avg /= 6;

  float voltage = avg * (3.3 / 4095.0);
  float ph_value = 7.0 + ((voltage - PH7_VOLTAGE) / (PH4_VOLTAGE - PH7_VOLTAGE)) * (4.0 - 7.0);
  ph_value = constrain(ph_value, 0.0, 14.0);


  if (isnan(ph_value)) {
    Serial.println("⚠️ Invalid pH reading. Retaining last known value.");
    return waterPH;  // Use previous stable value
  }

  return ph_value;
}

// ========== Calculate Required Water pH ==========
float calculateRequiredWaterPH(float soilPH) {
  float desiredSoilPH = 7.0;

  // ⚠️ Sanity check for soilPH value
  if (soilPH < 3.0 || soilPH > 10.0) {
    Serial.println("⚠️ Soil pH out of range. Using default targetWaterPH = 7.0");
    return desiredSoilPH;
  }

  float requiredWaterPH;
  if (soilPH < desiredSoilPH) {
    requiredWaterPH = desiredSoilPH + (desiredSoilPH - soilPH);  // make more basic
  } else if (soilPH > desiredSoilPH) {
    requiredWaterPH = desiredSoilPH - (soilPH - desiredSoilPH);  // make more acidic
  } else {
    requiredWaterPH = desiredSoilPH;
  }

  return constrain(requiredWaterPH, 0.0, 14.0);  // still safe to bound the result
}


// --- PID to Adjust Water Solution ---
void regulateWaterSolutionPH(float currentPH, float targetPH) {
  phError = targetPH - currentPH;

  // ✅ If within tolerance, stop pumps, reset cycle counter
  if (abs(phError) < PH_TOLERANCE) {
    digitalWrite(ACID_PUMP_PIN, LOW);
    digitalWrite(BASE_PUMP_PIN, LOW);
    digitalWrite(MIXER_MOTOR_PIN, LOW);
    pumpCycleActive = false;
    phCorrectionCycleCount = 0;
    Serial.println("✅ pH balanced. Pumps & Mixer OFF.");
    phRegulationComplete = true;
    return;
  }

  // ⛔ If max cycles reached, stop everything and skip correction
  if (phCorrectionCycleCount >= MAX_CORRECTION_CYCLES) {
    digitalWrite(ACID_PUMP_PIN, LOW);
    digitalWrite(BASE_PUMP_PIN, LOW);
    digitalWrite(MIXER_MOTOR_PIN, LOW);
    pumpCycleActive = false;
    Serial.println("⚠️ Max pH correction cycles reached. No further action.");
    phRegulationComplete = true;
    return;
  }

  // If pump is currently active, check if time elapsed or pH is now OK
  if (pumpCycleActive) {
    if (millis() - pumpStartTime >= currentPumpDuration || abs(phError) < PH_TOLERANCE) {
      digitalWrite(ACID_PUMP_PIN, LOW);
      digitalWrite(BASE_PUMP_PIN, LOW);
      digitalWrite(MIXER_MOTOR_PIN, LOW);
      pumpCycleActive = false;
      Serial.println("✅ Pump cycle ended early or completed. OFF.");
    }
    return;  // Don’t start new cycle while one is running
  }

  // Begin a new correction cycle
  integral = constrain(integral + (phError * 0.1), -10, 10);
  double derivative = (phError - prevPhError) / 0.1;
  double output = (Kp * phError) + (Ki * integral) + (Kd * derivative);
  Serial.println(output);
  prevPhError = phError;

  currentPumpDuration = constrain(abs(output) * 100, MIN_ON_TIME, MAX_ON_TIME);
  pumpStartTime = millis();
  pumpCycleActive = true;
  phCorrectionCycleCount++;

  digitalWrite(MIXER_MOTOR_PIN, HIGH);

  if (output > 0) {
    digitalWrite(ACID_PUMP_PIN, LOW);
    digitalWrite(BASE_PUMP_PIN, HIGH);
    Serial.println("🧪 Cycle " + String(phCorrectionCycleCount) + ": Adding Base + Mixing");
    delay(1000);
  } else {
    digitalWrite(BASE_PUMP_PIN, LOW);
    digitalWrite(ACID_PUMP_PIN, HIGH);
    Serial.println("🧪 Cycle " + String(phCorrectionCycleCount) + ": Adding Acid + Mixing");
    delay(1000);
  }
}


// --- Ultrasonic Functions ---
float getDistance(int pin) {
  float readings[20];

  for (int i = 0; i < 20; i++) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delayMicroseconds(2);
    digitalWrite(pin, HIGH);
    delayMicroseconds(10);
    digitalWrite(pin, LOW);
    pinMode(pin, INPUT);
    long duration = pulseIn(pin, HIGH, 30000);  // 30ms timeout
    readings[i] = duration * 0.0343 / 2.0;      // Convert to cm
    delay(3);
  }

  // Sort readings
  for (int i = 0; i < 99; i++) {
    for (int j = i + 1; j < 20; j++) {
      if (readings[i] > readings[j]) {
        float temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }

  // Average 5 readings around the median (index 22–26)
  float sum = 0;
  for (int i = 8; i <= 12; i++) {
    sum += readings[i];
  }

  return sum / 5.0;
}



float getWaterLevel(float dist) {
  float height = TANK_HEIGHT - dist;
  Serial.println(dist);
  float level = constrain(height / TANK_HEIGHT * 100.0, 0, 100);
  if (level == 100) {
    level = 0;
  }
  return level;
}

// --- Tank Monitoring & Refill ---
void maintainTankLevels() {

  if (rainLevel > RAIN_LOW_THRESHOLD) {
    digitalWrite(RELAY_RAIN, HIGH);
    digitalWrite(RELAY_SUPPLY, LOW);
  } else {
    digitalWrite(RELAY_RAIN, LOW);
    digitalWrite(RELAY_SUPPLY, HIGH);
  }


  if (mixerLevel >= MIXER_HIGH_THRESHOLD) {
    digitalWrite(RELAY_RAIN, LOW);
    digitalWrite(RELAY_SUPPLY, LOW);
  }
}

// --- ESP-NOW Receive Soil pH ---
void onReceiveESPNow(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  // Ensure the data is safely handled as a String
  String jsonStr = String((const char *)incomingData).substring(0, len);
  incomingJsonBuffer = jsonStr;
  newDataReceived = true;

  Serial.println("📥 Incoming JSON: " + jsonStr);

  // Parse JSON safely
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, jsonStr);

  if (error) {
    Serial.print("❌ JSON Parse Error: ");
    Serial.println(error.c_str());
    return;
  }

  // Handle sensor data
  if (doc.containsKey("ph") && doc["ph"].is<float>()) {
    receivedSoilPH = doc["ph"];
  }

  if (doc.containsKey("moisture") && doc["moisture"].is<float>()) {
    receivedSoilMoisture = doc["moisture"];
  }
  if (doc.containsKey("mixer_control")) {
    int fillAmount = doc["mixer_control"].as<int>();
    MIXER_HIGH_THRESHOLD = MIXER_LOW_THRESHOLD + fillAmount;
    scaledFillTime = (MIXER_HIGH_THRESHOLD * FULL_FILL_TIME_MS) / 100;
    mixerThresholdChanged = true;

    Serial.println("🛠 MIXER_HIGH_THRESHOLD set to " + String(MIXER_HIGH_THRESHOLD));
    Serial.println("⏱️ Scaled Fill Time: " + String(scaledFillTime / 1000.0, 2) + " seconds");
  }

  if (doc.containsKey("suppy_control") && doc["suppy_control"].as<int>() != 0) {
    WATER_DISPENSE_PERCENT = doc["suppy_control"].as<int>();
    scaledDispenseTime = (WATER_DISPENSE_PERCENT * FULL_DISPENSE_TIME_MS) / 100;

    wateringActive = true;
    normalTankMode = false;
    phRegulationComplete = false;
    phBalancingLocked = false;
    systemState = STATE_REFILL;

    Serial.println("🚿 Dispense command received. Starting full process...");
    Serial.println("⏱️ Scaled Dispense Time: " + String(scaledDispenseTime / 1000.0, 2) + " seconds");
  }
}






// --- Send Tank & pH Data via ESP-NOW ---
void MainTask() {
  DynamicJsonDocument doc(256);
  doc["waterPH"] = waterPH;
  doc["targetPH"] = targetWaterPH;
  doc["mixerLevel"] = mixerLevel;
  doc["supplyLevel"] = supplyLevel;
  doc["rainLevel"] = rainLevel;
  doc["acidLevel"] = acidLevel;
  doc["baseLevel"] = baseLevel;
  doc["SoilPH"] = receivedSoilPH;
  doc["SoilMoisture"] = receivedSoilMoisture;


  String jsonString;
  serializeJson(doc, jsonString);
  esp_now_send(peerAddress, (uint8_t *)jsonString.c_str(), jsonString.length());

  // Serial print in one row
  Serial.print("📤 Sent: ");
  Serial.println(jsonString);
}

void checkScheduledReset() {
  if ((millis() - lastBootTime) > MAX_UPTIME_BEFORE_RESET) {
    if (!wateringActive && !pumpCycleActive) {
      Serial.println("🔁 Scheduled restart triggered.");
      ESP.restart();
    } else {
      Serial.println("⏳ Skipped reset: system is busy (watering or regulating).");
      // Optional: postpone for another few minutes
      lastBootTime = millis();  // Reschedule if busy
    }
  }
}

void UltrasonicReadTask(void *parameter) {
  while (true) {
    mixerLevel = getWaterLevel(getDistance(SENSOR_MIXER));
    vTaskDelay(pdMS_TO_TICKS(50));

    supplyLevel = getWaterLevel(getDistance(SENSOR_SUPPLY));
    vTaskDelay(pdMS_TO_TICKS(50));

    rainLevel = getWaterLevel(getDistance(SENSOR_RAIN));
    vTaskDelay(pdMS_TO_TICKS(50));

    acidLevel = getWaterLevel(getDistance(SENSOR_ACID));
    vTaskDelay(pdMS_TO_TICKS(50));

    baseLevel = getWaterLevel(getDistance(SENSOR_BASE));
    vTaskDelay(pdMS_TO_TICKS(300));  // Wait the rest of the 500ms cycle
  }
}
