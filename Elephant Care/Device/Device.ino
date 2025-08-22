#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <MPU6050.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define SS 5
#define RST 14
#define DIO0 2
#define batpin 39

byte myAddress = 0xCD;
byte targetAddress = 0xAF;

MAX30105 particleSensor;
MPU6050 mpu;
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);  // Use UART1 for GPS

struct SensorData {
  float HR;
  float SPO2;
  float gyro_x, gyro_y, gyro_z;
  float acceleration_x, acceleration_y, acceleration_z;
  float temperature;
  float latitude;
  float longitude;
  float battery;

};

QueueHandle_t loraQueue;

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg = 76;

double avered = 0, aveir = 0;
double sumirrms = 0, sumredrms = 0;
double SpO2 = 0, ESpO2 = 60.0, FSpO2 = 0.7;
double frate = 0.95;
int i = 0, Num = 30;
#define FINGER_ON 7000
#define MINIMUM_SPO2 60.0

uint8_t temp_msb, temp_lsb;
int temp_address = 0x48;
uint16_t temp_reg;
float temperatureC;
float batValue;

int16_t ax, ay, az;
int16_t gx, gy, gz;

void readTemperature() {
  Wire.beginTransmission(temp_address);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(temp_address, 2);
  if (Wire.available() == 2) {
    temp_msb = Wire.read();
    temp_lsb = Wire.read();
    temp_reg = (temp_msb << 8) | temp_lsb;
    temperatureC = temp_reg / 256.0f;
    if (temperatureC == 256) temperatureC = 31;
  }
}

void loraTask(void *parameter) {
  static int packetCount = 0;
  while (1) {
    SensorData data;
    if (xQueueReceive(loraQueue, &data, portMAX_DELAY)) {
      StaticJsonDocument<512> jsonDoc;
      jsonDoc["HR"] = data.HR;
      jsonDoc["SPO2"] = data.SPO2;
      jsonDoc["gyro_x"] = data.gyro_x;
      jsonDoc["gyro_y"] = data.gyro_y;
      jsonDoc["gyro_z"] = data.gyro_z;
      jsonDoc["acceleration_x"] = data.acceleration_x;
      jsonDoc["acceleration_y"] = data.acceleration_y;
      jsonDoc["acceleration_z"] = data.acceleration_z;
      jsonDoc["temperature"] = data.temperature;
      jsonDoc["latitude"] = data.latitude;
      jsonDoc["longitude"] = data.longitude;
      jsonDoc["battery"] = data.battery;

      String jsonString;
      serializeJson(jsonDoc, jsonString);

      LoRa.beginPacket();
      LoRa.write(targetAddress);
      LoRa.write(myAddress);
      LoRa.write(jsonString.length());
      LoRa.print(jsonString);
      LoRa.endPacket();

      Serial.println("Sent via LoRa: " + jsonString);

      packetCount++;
      if (packetCount % 10 == 0) {
        Serial.printf("[LoRa] Sent %d packets\n", packetCount);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("System Start");

  Wire.begin();
  gpsSerial.begin(9600, SERIAL_8N1, 26, 25);  // RX=16, TX=17
  pinMode(13, OUTPUT);

  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa init failed");
    while (1);
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    while (1);
  }
  Serial.println("MAX30102 Found!");
  particleSensor.setup(0x7F, 4, 2, 800, 215, 16384);
  particleSensor.enableDIETEMPRDY();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
  delay(100);

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 not found");
    while (1);
  }
  Serial.println("MPU6050 Found!");

  loraQueue = xQueueCreate(20, sizeof(SensorData));
  if (loraQueue == NULL) {
    Serial.println("Queue create failed!");
    while (1);
  }

  xTaskCreatePinnedToCore(
    loraTask,
    "LoRaTask",
    10000,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
  batValue=analogRead(batpin)*3.3/4095*3.533;

  float latitude = gps.location.isValid() ? gps.location.lat() : 0.0;
  float longitude = gps.location.isValid() ? gps.location.lng() : 0.0;

  long irValue = particleSensor.getIR();

  if (irValue > FINGER_ON) {
    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      beatsPerMinute = 60 / (delta / 1000.0);
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }

    particleSensor.check();
    if (particleSensor.available()) {
      i++;
      double fir = (double)particleSensor.getFIFOIR();
      double fred = (double)particleSensor.getFIFORed();
      aveir = aveir * frate + fir * (1.0 - frate);
      avered = avered * frate + fred * (1.0 - frate);
      sumirrms += (fir - aveir) * (fir - aveir);
      sumredrms += (fred - avered) * (fred - avered);

      if ((i % Num) == 0) {
        double R = (sqrt(sumirrms) / aveir) / (sqrt(sumredrms) / avered);
        SpO2 = -23.3 * (R - 0.4) + 120;
        ESpO2 = FSpO2 * ESpO2 + (1.0 - FSpO2) * SpO2;
        if (ESpO2 <= MINIMUM_SPO2) ESpO2 = MINIMUM_SPO2;
        if (ESpO2 > 100) ESpO2 = 99.9;
        sumredrms = 0.0;
        sumirrms = 0.0;
        SpO2 = 0;
        i = 0;
      }
      particleSensor.nextSample();
    }

    readTemperature();
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    Serial.printf("HR: %d bpm, SpO2: %.1f%%, Temp: %.2f°C\n", beatAvg, ESpO2, temperatureC);
    Serial.printf("Accel: X=%d Y=%d Z=%d, Gyro: X=%d Y=%d Z=%d\n", ax, ay, az, gx, gy, gz);
    Serial.printf("GPS: Lat=%.6f, Lon=%.6f\n", latitude, longitude);

    if (beatAvg == 0 && millis() > 30000) {
      Serial.println("Restarting due to missing heartbeat...");
      esp_restart();
    }

    SensorData data;
    data.HR = beatAvg;
    data.SPO2 = ESpO2;
    data.gyro_x = gx;
    data.gyro_y = gy;
    data.gyro_z = gz;
    data.acceleration_x = ax;
    data.acceleration_y = ay;
    data.acceleration_z = az;
    data.temperature = temperatureC;
    data.latitude = latitude;
    data.longitude = longitude;
    data.battery=batValue;


    if (uxQueueSpacesAvailable(loraQueue) > 2) {
      if (xQueueSend(loraQueue, &data, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("Failed to send to LoRa queue!");
        digitalWrite(13, HIGH); delay(100); digitalWrite(13, LOW);
      }
    } else {
      Serial.println("Skipped sending: LoRa queue nearly full.");
    }
  } else {
    for (byte rx = 0; rx < RATE_SIZE; rx++) rates[rx] = 0;
    beatAvg = 0;
    rateSpot = 0;
    lastBeat = 0;
    avered = 0;
    aveir = 0;
    sumirrms = 0;
    sumredrms = 0;
    SpO2 = 0;
    ESpO2 = 90.0;
  }

  delay(200);
}
