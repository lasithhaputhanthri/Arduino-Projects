#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_INA219 ina219;

void setup() {
  Serial.begin(115200);
  ina219.begin();
}

void loop() {
  float current_mA = ina219.getCurrent_mA();
  float power_mW = current_mA * 5.0; // Fixed 5V

  Serial.println(power_mW); // Just send number, Python adds timestamp
  delay(1000);
}
