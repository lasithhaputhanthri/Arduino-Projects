const int pulsePin = 9; // Pin to generate the square wave
const int readPin = A0; // Pin to read the square wave
const int frequency = 50; // Frequency in Hz
const int period = 1000 / frequency; // Period in milliseconds

void setup() {
  pinMode(pulsePin, OUTPUT); // Set pulse pin as output
  Serial.begin(115200); // Initialize serial communication at 115200 bits per second

  // Adjust analog read settings for faster sampling
                       randomSeed(analogRead(0));
  // Set ADC prescaler to 32 (for Arduino Uno)
  ADCSRA = (ADCSRA & 0xF8) | 0x04;
}

void loop() {
  unsigned long currentTime = micros();
  static unsigned long previousTime = 0;

  float randomValue =random(-100, 101);;
  
  // Generate the square wave
  if ((currentTime - previousTime) >= (period * 1000) / 2) {
    previousTime = currentTime;
    PORTB ^= _BV(PORTB1); // Toggle the pulse pin using direct port manipulation
  }
  
  // Read the signal from A0
  int signal = analogRead(readPin);
  Serial.println(signal); // Send the signal value to the Serial Plotter
  //Serial.print(" ,");
  //Serial.println(signal+randomValue);
}