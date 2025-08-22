#include <ESP32Servo.h>
#include <HardwareSerial.h>
#include <ModbusMaster.h>
#include <ArduinoJson.h>

// --- Configuration ---
#define MAX_PLANTS 10

// --- Pins ---
#define RS485_RX 26
#define RS485_TX 27
#define RE 2
#define DE 4
const int servoPin1 = 18, servoPin2 = 19, servoPin3 = 23, servoPin4 = 22;

// --- Servo timings ---
const int durationMs = 3000, updateMs = 1;

// --- Global objects ---
Servo s1, s2, s3, s4;
HardwareSerial mod(2);
ModbusMaster node;
int currentPos[4] = {1400,1600,1300,1800};

TaskHandle_t npkTaskHandle;
uint16_t npk_n = 0, npk_p = 0, npk_k = 0;
bool npk_data_ready = false;
bool modbusPaused = false;
int lastPlantID = 0;

// --- Sequence ---
const int leftSeq[5][4] = {
  {1400,1600,1300,1800}, // Starting
  {1300,1600,1600,1800}, // starting to work
  {1720,1600,1500,1600},// Working
  {1300,1600,1600,1800}, // Stopping WORK
  {1400,1600,1300,1800} // Stopping
};

void preTransmission() {
  digitalWrite(RE, HIGH);
  digitalWrite(DE, HIGH);
}
void postTransmission() {
  digitalWrite(RE, LOW);
  digitalWrite(DE, LOW);
}

void npkTask(void *pvParameters) {
  for (;;) {
    if (!modbusPaused) {
      uint8_t result = node.readHoldingRegisters(0x001E, 3);
      if (result == node.ku8MBSuccess) {
        npk_n = node.getResponseBuffer(0);
        npk_p = node.getResponseBuffer(1);
        npk_k = node.getResponseBuffer(2);
        Serial.printf("📊 N=%d, P=%d, K=%d\n", npk_n, npk_p, npk_k);
        npk_data_ready = true;
      } else {
        Serial.print("Modbus read failed. Code: "); Serial.println(result);
      }
    }
    delay(5000);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RE, OUTPUT); pinMode(DE, OUTPUT);
  digitalWrite(RE, LOW); digitalWrite(DE, LOW);

  mod.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  node.begin(1, mod);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  s1.attach(servoPin1); s2.attach(servoPin2);
  s3.attach(servoPin3); s4.attach(servoPin4);

  xTaskCreatePinnedToCore(npkTask, "NPK Task", 2048, NULL, 1, &npkTaskHandle, 0);
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("TRIGGER,PLANT:")) {
      lastPlantID = line.substring(line.indexOf(":") + 1).toInt();
      Serial.printf("🟢 Trigger received for Plant %d\n", lastPlantID);
      runSequence(leftSeq, 5);
    }
  }
}

void runSequence(const int seq[][4], int steps) {
  StaticJsonDocument<128> deferredDoc;  // <-- Declare outside the loop

  for (int i = 0; i < steps; i++) {
    moveSmooth(currentPos, seq[i]);
    memcpy(currentPos, seq[i], sizeof(currentPos));

    if (i == 2) {
      delay(10000);  // Sampling delay
    }

    if (i == 3) {
      Serial.println("⏳ NPK data captured, deferring JSON send...");

      // 📝 Prepare JSON but don't send yet
      deferredDoc["plantID"] = lastPlantID;
      deferredDoc["N"] = lastPlantID * 100000 + npk_n;
      deferredDoc["P"] = npk_p;
      deferredDoc["K"] = npk_k;

      npk_data_ready = false;

      if (lastPlantID >= MAX_PLANTS) {
        StaticJsonDocument<64> doneDoc;
        doneDoc["status"] = "DONE";
        serializeJson(doneDoc, Serial);
        Serial.println();
        lastPlantID = 0;
      }
    }
  }

  // ✅ After loop (i == 5): now send the deferred JSON
  Serial.println("✅ Sending deferred NPK JSON:");
  serializeJson(deferredDoc, Serial);
  Serial.println();
}


void moveSmooth(const int *start, const int *dest) {
  int steps = durationMs / updateMs;
  float inc[4];
  for (int i = 0; i < 4; i++) inc[i] = float(dest[i] - start[i]) / steps;
  for (int st = 0; st <= steps; st++) {
    int cur[4];
    for (int i = 0; i < 4; i++) cur[i] = start[i] + int(inc[i] * st);
    uint32_t t0 = millis();
    while (millis() - t0 < updateMs) {
      s1.writeMicroseconds(cur[0]); s2.writeMicroseconds(cur[1]);
      s3.writeMicroseconds(cur[2]); s4.writeMicroseconds(cur[3]);
    }
  }
}
