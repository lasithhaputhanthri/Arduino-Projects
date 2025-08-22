const int zeroCrossPin = 34;  // Input pin for zero-crossing detection
const int triacPin = 2;       // Output pin to control the TRIAC

volatile bool zeroCrossDetected = false;
unsigned long delayTime = 10000;  // Delay time for half intensity (50%)

void setup() {
  pinMode(zeroCrossPin, INPUT_PULLUP);  // Zero-crossing pin
  pinMode(triacPin, OUTPUT);            // TRIAC trigger pin
  attachInterrupt(digitalPinToInterrupt(zeroCrossPin), zeroCrossISR, FALLING); // Interrupt on falling edge of zero-crossing
  Serial.begin(115200);
}

void loop() {
  // Wait for the zero-crossing detection interrupt
  if (zeroCrossDetected) {
    zeroCrossDetected = false;
    // Wait for the appropriate delay to control dimming intensity
    delayMicroseconds(delayTime); // Half intensity (50% dimming)
    
    // Trigger the TRIAC to turn on the AC
    digitalWrite(triacPin, HIGH);
    delayMicroseconds(10);  // Keep the TRIAC on for a brief moment
    digitalWrite(triacPin, LOW); // Turn off the TRIAC
    
    Serial.println("TRIAC Triggered");
  }
}

void zeroCrossISR() {
  zeroCrossDetected = true;
}
