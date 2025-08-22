const int supplyVoltagePin = A1;
const int capacitorVoltagePin = A0;

const int switch1Pin = 2;  // S1: supply ➝ output
const int switch2Pin = 3;  // S2: supply ➝ caps
const int switch3Pin = 4;  // S3: caps ➝ output

const float capacitorUpperThreshold = 13.0; // Cap full
const float capacitorLowerThreshold = 12.8;         // Output too low = fallback

bool useCapacitor = false;

void setup() {
  pinMode(switch1Pin, OUTPUT);
  pinMode(switch2Pin, OUTPUT);
  pinMode(switch3Pin, OUTPUT);
  Serial.begin(9600);
}

void loop() {
  float supplyVoltage = analogRead(supplyVoltagePin) * (5.0 / 1023.0) * 3.195;
  float capacitorVoltage = analogRead(capacitorVoltagePin) * (5.0 / 1023.0) * 3.185;

  Serial.print("Supply: "); Serial.print(supplyVoltage);
  Serial.print("V, Cap: "); Serial.print(capacitorVoltage);
  Serial.println("V");

  // Check if we can switch to capacitor
  if (!useCapacitor && capacitorVoltage >= capacitorUpperThreshold) {
    useCapacitor = true;
    Serial.println("↑ Caps fully charged → Switching to capacitor output");
  }

  // Check if we need to switch back to supply
  if (useCapacitor && capacitorVoltage < capacitorLowerThreshold ) {
    useCapacitor = false;
    Serial.println("↓ Output voltage low → Switching to main supply");
  }

  // Apply switching logic
  if (useCapacitor) {
    // Use caps
    digitalWrite(switch1Pin, LOW);   // supply ➝ output OFF
    digitalWrite(switch2Pin, LOW);   // supply ➝ cap OFF
    digitalWrite(switch3Pin, HIGH);  // caps ➝ output ON
    Serial.println("→ Power from capacitors");
  } else {
    // Charge caps and supply output
    digitalWrite(switch1Pin, HIGH);  // supply ➝ output ON
    digitalWrite(switch2Pin, HIGH);  // supply ➝ cap ON
    digitalWrite(switch3Pin, LOW);   // caps ➝ output OFF
    Serial.println("→ Charging caps & supplying output from main");
  }

  delay(10);
}
