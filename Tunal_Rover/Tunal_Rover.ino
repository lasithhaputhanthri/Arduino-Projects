#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>
#include <DHT.h>
#include <DHT_U.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <NewPing.h>  // Include Ping library
#include <TinyGPS++.h>

HardwareSerial gpsSerial(1);  // Use UART1 (second hardware serial port)
TinyGPSPlus gps;

// Pin Definitions
const int MQ4_PIN = 34;
const int CO2_PIN = 35;
const int MQ7_PIN = 39;
const int LDR_PIN = 36;
const int DHT_PIN = 33;
const int ULTRASONIC1_PIN = 15;   // Changed to GPIO2
const int ULTRASONIC2_PIN = 25;  // Changed to GPIO25

#define DUST_SENSOR_PIN 35  // DSM501A output (use a 10k pull-up resistor to 3.3V)
#define SAMPLE_TIME_MS 1000

// DHT Sensor Configuration
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// LoRa Configuration
#define LORA_SS 5
#define LORA_RST 32
#define LORA_DIO0 2
#define LOCAL_ADDRESS 0xB1
#define DESTINATION_MASTER 0xAB

// ADC Configurations
const float ADC_MAX_VOLTAGE = 3.3;
const int ADC_RESOLUTION = 4095;

// MQ Sensor Calibration Constants
const float R0_MQ4 = 10.0;
const float R0_MQ7 = 10.0;
const float R0_CO2 = 10.0;

// BMP180 sensor object
Adafruit_BMP085_Unified bmp;

// HSPI Pins for NRF24L01
#define CE_PIN 26
#define CSN_PIN 27

RF24 radio(CE_PIN, CSN_PIN);

// Custom SPI instance for HSPI
SPIClass SPI_HSPI(HSPI);
const byte address[6] = "00001";

unsigned long lowPulseOccupancy = 0;
unsigned long startTime;
float concentration = 0;

// Function Prototypes
float calculateGasPPM(float ratio, float m, float b);
void sendMessage(String outgoing, byte destination);
float getUltrasonicDistance(int pin);
void TaskNRF24(void *pvParameters);
void TaskMain(void *pvParameters);
void initSensor();
void readDustSensor();
float calculateConcentration();
void printData();

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17); // GPS TX -> IO16 (RX), GPS RX -> IO17 (TX)
  initSensor();
  startTime = millis();
  Serial.println("Initializing sensors...");

  // Initialize BMP180 Sensor
  if (!bmp.begin()) {
    Serial.println("BMP180 sensor initialization failed!");
    while (1);
  }

  // Initialize DHT Sensor
  dht.begin();

  // Initialize LoRa
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa initialization failed!");
    while (1);
  }
  Serial.println("LoRa initialized successfully!");

  // Initialize HSPI with custom pins
  SPI_HSPI.begin(14, 12, 13, 27);
  radio.begin(&SPI_HSPI);
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_250KBPS);
  radio.openReadingPipe(1, address);
  radio.startListening();

  // Create FreeRTOS tasks on separate cores
  xTaskCreatePinnedToCore(TaskNRF24, "NRF24_Task", 4096, NULL, 1, NULL, 0); // Core 0
  xTaskCreatePinnedToCore(TaskMain, "Main_Task", 8192, NULL, 1, NULL, 1);    // Core 1
}

// NRF24L01 Task (Runs on Core 0)
void TaskNRF24(void *pvParameters) {
  while (true) {
    if (radio.available()) {
      char receivedText[32] = "";
      radio.read(&receivedText, sizeof(receivedText));
      Serial.print("Received: ");
      Serial.println(receivedText);
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// Main Task for Sensors and LoRa (Runs on Core 1)
void TaskMain(void *pvParameters) {
  while (true) {
    StaticJsonDocument<1024> doc;
    readGPSData(doc);
    readDustSensor();

        if ((millis() - startTime) > SAMPLE_TIME_MS) {
        concentration = calculateConcentration();
        printData(); // Print data to Serial Monitor
        startTime = millis();
    }

    doc["Dust"]=concentration;

    // Read sensors
    doc["MQ-4"] = calculateGasPPM((ADC_MAX_VOLTAGE / analogRead(MQ4_PIN)) / R0_MQ4, -2.0, 1.5);
    doc["CO2"] = calculateGasPPM((ADC_MAX_VOLTAGE / analogRead(CO2_PIN)) / R0_CO2, -2.0, 1.5);
    doc["MQ-7"] = calculateGasPPM((ADC_MAX_VOLTAGE / analogRead(MQ7_PIN)) / R0_MQ7, -2.5, 1.7);
    doc["LDR"] = analogRead(LDR_PIN) * (ADC_MAX_VOLTAGE / ADC_RESOLUTION);

    float temperature =dht.readTemperature();
    float humidity = dht.readHumidity();
    if (!isnan(temperature) && !isnan(humidity)) {
      doc["Temperature"] = temperature;
      doc["Humidity"] = humidity;
    } else {
      Serial.println("Failed to read from DHT sensor!");
    }

    doc["Ultrasonic Sensor 1"] = getUltrasonicDistance(ULTRASONIC1_PIN);
    doc["Ultrasonic Sensor 2"] = getUltrasonicDistance(ULTRASONIC2_PIN);

    sensors_event_t event;
    bmp.getEvent(&event);
    if (event.pressure) {
      bmp.getTemperature(&temperature);
      doc["BMP180 Temperature"] = temperature;
      doc["BMP180 Pressure"] = event.pressure;
    }

    // Serialize and send data via LoRa
    String output;
    serializeJson(doc, output);
    Serial.println(output);
    sendMessage(output, DESTINATION_MASTER);

    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// Function to calculate gas PPM for MQ sensors
float calculateGasPPM(float ratio, float m, float b) {
  return pow(10, (m * log10(ratio) + b));
}

// Function to send LoRa message
void sendMessage(String outgoing, byte destination) {
  LoRa.beginPacket();
  LoRa.write(destination);
  LoRa.write(LOCAL_ADDRESS);
  LoRa.write(outgoing.length());
  LoRa.print(outgoing);
  LoRa.endPacket();
}

// Function to get ultrasonic distance using ping.h
float getUltrasonicDistance(int pin) {
  NewPing sensor(pin,pin,1000);
  int distance = sensor.ping_cm();  // Get distance in cm
  if (distance <= 0) return -1;     // Error handling
  return distance;
}

// Function to Initialize the DSM501A Sensor
void initSensor() {
    pinMode(DUST_SENSOR_PIN, INPUT);
}

// Function to Read the DSM501A Sensor
void readDustSensor() {
    unsigned long duration = pulseInLong(DUST_SENSOR_PIN, LOW); // Measure LOW duration
    lowPulseOccupancy += duration;
}

// Function to Calculate Dust Concentration
float calculateConcentration() {
    float ratio = lowPulseOccupancy / (SAMPLE_TIME_MS * 10.0);
    lowPulseOccupancy = 0;
    return 1.1 * pow(ratio, 3) - 3.8 * pow(ratio, 2) + 520 * ratio + 0.62;
}

// Function to Print Data to Serial Monitor
void printData() {
    Serial.print("Concentration: ");
    Serial.print(concentration);
    Serial.println(" ug/m3");

    Serial.print("Air Quality: ");
    if (concentration < 1000) Serial.println("Clean");
    else if (concentration < 10000) Serial.println("Good");
    else if (concentration < 20000) Serial.println("Acceptable");
    else if (concentration < 50000) Serial.println("Heavy");
    else Serial.println("Hazard");

    Serial.println("------------------------");
}

void readGPSData(JsonDocument &doc) {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isValid()) {
    doc["Latitude"] = gps.location.lat();
    doc["Longitude"] = gps.location.lng();
  } else {
    doc["Latitude"] = "N/A";
    doc["Longitude"] = "N/A";
  }
}

void loop(){

}


