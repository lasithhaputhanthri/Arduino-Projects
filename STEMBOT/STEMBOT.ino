#include <Arduino.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ========== Mode Handling ==========
enum Mode { NO_MODE = 0, LINE_FOLLOW, OBSTACLE_AVOID, LINE_FOLLOW_WITH_ARM };

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Forward: LCD helpers
void lcdShowMode(Mode m, bool running);
const char* modeShort(Mode m);

// ========== Pins & Config ==========
#define BUTTON_PIN 5  // Start/stop button (GPIO5 with pull-up)

// IR sensor array (left -> right)  **ORDER MATTERS**
const int IR_pins[] = {26,25,33,32,35,34,39,36};
const int num_sensors = 8;

// Motor pins (H-bridge: forward/backward each side)
const int motor_left_forward   = 18;
const int motor_left_backward  = 19;
const int motor_right_forward  = 17;
const int motor_right_backward = 16;

// Ultrasonic (front, dual-pin like HC-SR04)
#define ULTRA_FRONT_TRIG 23
#define ULTRA_FRONT_ECHO 15
#define OBSTACLE_DISTANCE_CM 15

// ---- Magnet Sensor (2 digital channels, external pull-ups, active-LOW) ----
#define MAG1_PIN 27   // LSB (bit0)
#define MAG2_PIN 14   // MSB (bit1)

// Black gives HIGHER ADC on your line sensors:
const bool LINE_IS_BLACK = false;

// ESP32 8-bit PWM range
const int MAX_PWM = 255;

// Current mode
Mode currentMode = NO_MODE;  // default is no mode until Start is pressed

// ========== Button / Run State ==========
bool patrolEnabled = false;
bool lastButtonState = HIGH;
bool modeLocked = false;

// ========== PID (UNCHANGED as requested) ==========
float Kp = 70, Ki = 0, Kd = 50;
float error = 0, previous_error = 0;
float integral = 0, derivative = 0;
float correction = 0;

// ========== Drive / Boost ==========
const int BASE_SPEED_NORMAL   = 160;
const int BASE_SPEED_STARTING = 160;
const int START_BOOST_DURATION_MS = 200;

bool justStarted = true;
unsigned long startBoostStartTime = 0;

// Last seen line position (for brief line loss)
float lastPosition = (num_sensors - 1) / 2.0;  // center (3.5 with 8 sensors)

// ----- Obstacle-Avoid tuning -----
const int OA_FORWARD_SPEED = 255;
const int TURN_RIGHT_SPEED = 200;
const int TURN_RIGHT_MS    = 1000;  // calibrate

// For LINE_FOLLOW_WITH_ARM
const int GRAB_PAUSE_MS    = 300;

// ====== Servos (3x 9g) ======
Servo servoBase, servoLift, servoGripper;
#define SERVO_BASE_PIN    2
#define SERVO_LIFT_PIN    4
#define SERVO_GRIPPER_PIN 13

// ====== Analog IR thresholds (per sensor, aligned to IR_pins order) ======
int IR_THRESH_LO[num_sensors] = {100,900,100,1701,2900,2000,80,350};
int IR_THRESH_HI[num_sensors] = {160,1000,150,2464,3000,2662,130,450};
// Remember last on/off state to apply hysteresis
static uint8_t irState[num_sensors] = {0};

// Cached IR snapshot (MSB = leftmost sensor)
uint8_t irBits = 0;

// Cached raw values for each sensor (left -> right)
int irLastRaw[num_sensors] = {0};

// Serial debug rate for IR printing (not strictly used; we print each PID step)
unsigned long irPrintLast = 0;
const unsigned long IR_PRINT_INTERVAL_MS = 100;  // 10 Hz

// ========== Forward Decls ==========
void handleButton();
void stopmotors();
void driveMotors(int left, int right);
void followLineMode();
void obstacleAvoidMode();
void lineFollowWithArmMode();

bool isObstacleDetected();
float readSensors();
int readRawIR(int pin);    // analog+threshold → 1 on line, 0 background
bool isLineEnded();
void turn180();
void turnRight90();
long readUltrasonicDistanceCM(int trigPin, int echoPin);

// PID step
void lineFollowPIDStep();

// Arm routine
void armGrabObject();

// Magnet helpers (DIGITAL, active-LOW)
uint8_t readMagPattern();
Mode decodeMagPattern(uint8_t pattern);
const char* modeName(Mode m);

// Cached-IR print helper (forward-declare since used before definition)
void printIRBitsCached();

const int SERVO_MIN_US = 500;
const int SERVO_MAX_US = 2400;
const int ANG_MIN = 0, ANG_MAX = 180;

// ====== LCD helpers ======
const char* modeShort(Mode m) {
  switch (m) {
    case LINE_FOLLOW:          return "LINE";
    case OBSTACLE_AVOID:       return "AVOID";
    case LINE_FOLLOW_WITH_ARM: return "ARM";
    case NO_MODE:
    default:                   return "NONE";
  }
}

void lcdShowMode(Mode m, bool running) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Mode: ");
  lcd.print(modeShort(m));

  lcd.setCursor(0,1);
  lcd.print(running ? "RUNNING" : "STOPPED");
}

// ========== Setup ==========
void setup() {
  Serial.begin(115200);

  // Better ADC behavior on ESP32 (optional but recommended)
  analogSetWidth(12);              // 0..4095
  analogSetAttenuation(ADC_11db);  // ~0..3.3V

  // IR sensors
  for (int i = 0; i < num_sensors; i++) pinMode(IR_pins[i], INPUT);

  // Motors
  pinMode(motor_left_forward, OUTPUT);
  pinMode(motor_left_backward, OUTPUT);
  pinMode(motor_right_forward, OUTPUT);
  pinMode(motor_right_backward, OUTPUT);

  // Ultrasonic (front dual-pin)
  pinMode(ULTRA_FRONT_TRIG, OUTPUT);
  pinMode(ULTRA_FRONT_ECHO, INPUT);

  // Magnet sensor pins (external pull-ups → plain INPUT)
  pinMode(MAG1_PIN, INPUT);
  pinMode(MAG2_PIN, INPUT);

  // allocate PWM timers for servos (ESP32 has 4 general-purpose PWM timers)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // standard 50 Hz servos; set safe pulse range for 9g servos
  servoBase.setPeriodHertz(50);
  servoLift.setPeriodHertz(50);
  servoGripper.setPeriodHertz(50);

  // Attach with safe pulse range
  servoBase.attach(SERVO_BASE_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoLift.attach(SERVO_LIFT_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoGripper.attach(SERVO_GRIPPER_PIN, SERVO_MIN_US, SERVO_MAX_US);

  // Neutral pose
  servoBase.write(10);
  servoLift.write(170);
  servoGripper.write(15); // open

  // Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // LCD
  Wire.begin();        // ESP32 default SDA=21, SCL=22 (change if you wired differently)
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("STEMBOT Ready");
  lcd.setCursor(0,1); lcd.print("Press START");

  Serial.println("Boot OK. Waiting for Start. Present magnet pattern to choose mode.");
}

// ========== Main Loop ==========
void loop() {
  handleButton();

  if (!patrolEnabled || currentMode == NO_MODE) {
    stopmotors();
    return;
  }

  switch (currentMode) {
    case LINE_FOLLOW:
      if (isObstacleDetected()) {
        stopmotors();
        Serial.println("🛑 Obstacle ahead! Waiting...");
        delay(100);
        return;
      }
      followLineMode();
      break;

    case OBSTACLE_AVOID:
      Serial.println("OBSTACLE_AVOID");
      obstacleAvoidMode();
      break;

    case LINE_FOLLOW_WITH_ARM:
      Serial.println("LINE_FOLLOW_WITH_ARM");
      lineFollowWithArmMode();
      break;

    case NO_MODE:
    default:
      Serial.println("NO_MODE");
      stopmotors();
      break;
  }
}

// ========== Mode: Line Follow ==========
void followLineMode() {
  lineFollowPIDStep();
}

// ========== Mode: Obstacle Avoid ==========
void obstacleAvoidMode() {
  if (isObstacleDetected()) {
    stopmotors();
    Serial.println("🧱 Obstacle detected → turning RIGHT");
    turnRight90();
    delay(50);
  } else {
    driveMotors(OA_FORWARD_SPEED, OA_FORWARD_SPEED);
  }
  delay(8);
}

// ========== Mode: Line Follow WITH ARM ==========
void lineFollowWithArmMode() {
  if (isObstacleDetected()) {
    stopmotors();
    Serial.println("🦾 Obstacle detected → grabbing...");
    delay(GRAB_PAUSE_MS);
    armGrabObject();
    delay(GRAB_PAUSE_MS);

    integral = 0; previous_error = 0;

    Serial.println("✅ Grab sequence complete. ");
    return;
  }

  lineFollowPIDStep();
  delay(10);
}

// ========== PID step (shared) ==========
void lineFollowPIDStep() {
  float position = readSensors();
  float center   = (num_sensors - 1) / 2.0;

  // PID (unchanged)
  error = computeErrorWithDeadband(position);;
  integral += error;
  derivative = error - previous_error;
  correction = (Kp * error) + (Ki * integral) + (Kd * derivative);
  previous_error = error;

  int left_speed  = (int)constrain(BASE_SPEED_NORMAL + correction,  0, MAX_PWM);
  int right_speed = (int)constrain(BASE_SPEED_NORMAL - correction,  0, MAX_PWM);

  Serial.print(left_speed);
  Serial.print(" ");
  Serial.println(right_speed);

  driveMotors(left_speed, right_speed);
  printIRBitsCached();
}

// ========== Analog IR (with per-sensor hysteresis) ==========
static inline int indexOfIRPin(int pin) {
  for (int i = 0; i < num_sensors; i++) if (IR_pins[i] == pin) return i;
  return -1;
}

int readRawIR(int pin) {
  int idx = indexOfIRPin(pin);
  if (idx < 0) {
    int raw = analogRead(pin);
    if (!LINE_IS_BLACK) return (raw > 2000) ? 1 : 0;
    else                return (raw < 2000) ? 1 : 0;
  }

  int raw = analogRead(pin);         // 0..4095  (single read)
  irLastRaw[idx] = raw;              // cache the raw value for printing

  if (LINE_IS_BLACK) {
    if (raw < IR_THRESH_LO[idx]) irState[idx] = 1;
    else if (raw > IR_THRESH_HI[idx]) irState[idx] = 0;
  } else {
    if (raw > IR_THRESH_HI[idx]) irState[idx] = 1;
    else if (raw < IR_THRESH_LO[idx]) irState[idx] = 0;
  }

  return irState[idx]; // 1 = line, 0 = background
}

float readSensors() {
  float wsum = 0.0, sum = 0.0;
  uint8_t bits = 0;

  for (int i = 0; i < num_sensors; i++) {
    int val = readRawIR(IR_pins[i]);         // reading + caching here
    if (val) {
      // Set bit so MSB corresponds to leftmost sensor (IR_pins[0])
      bits |= (1 << (num_sensors - 1 - i));
    }
    wsum += (float)val * i;
    sum  += (float)val;
  }

  irBits = bits;                              // cache for debug printing

  if (sum == 0.0) return lastPosition;        // keep heading if line lost
  lastPosition = wsum / sum;                  // 0..(num_sensors-1)
  return lastPosition;
}

void printIRBitsCached() {
  // Bits (left → right)
  Serial.print("IR: ");
  for (int i = num_sensors - 1; i >= 0; --i) {
    Serial.print((irBits >> i) & 1);
    if (i) Serial.print(' ');
  }

  // Raw ADC (left → right, same order as IR_pins)
  Serial.print("  RAW: ");
  for (int i = 0; i < num_sensors; ++i) {
    // Highlight the 5th sensor (index 4) to tune its thresholds
    if (i == 4) Serial.print('[');
    Serial.print(irLastRaw[i]);
    if (i == 4) Serial.print(']');
    if (i < num_sensors - 1) Serial.print('\t');  // tab-separated
  }

  Serial.print("  pos=");
  Serial.println(lastPosition, 2);
}

bool isLineEnded() {
  for (int i = 0; i < num_sensors; i++) {
    if (readRawIR(IR_pins[i]) == 1) return false;
  }
  return true;
}

// ========== Ultrasonic / Obstacle ==========
bool isObstacleDetected() {
  long d = readUltrasonicDistanceCM(ULTRA_FRONT_TRIG, ULTRA_FRONT_ECHO);
  return (d > 0 && d < OBSTACLE_DISTANCE_CM);
}

long readUltrasonicDistanceCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long dur = pulseIn(echoPin, HIGH, 30000); // 30 ms timeout
  if (dur == 0) return -1;                  // no echo
  return (long)(dur * 0.034 / 2);           // in cm
}

// ====== Magnet Sensor: read & decode pattern (DIGITAL, active-LOW) ======
uint8_t readMagPattern() {
  // external pull-ups: idle=HIGH, active=LOW → invert so "present"=1
  uint8_t b1 = !digitalRead(MAG1_PIN); // LSB
  uint8_t b2 = !digitalRead(MAG2_PIN); // MSB
  return (b2 << 1) | b1;
}

Mode decodeMagPattern(uint8_t pattern) {
  switch (pattern) {
    case 0b11: return LINE_FOLLOW;
    case 0b10: return OBSTACLE_AVOID;
    case 0b01: return LINE_FOLLOW_WITH_ARM;
    case 0b00: default: return NO_MODE;
  }
}

const char* modeName(Mode m) {
  switch (m) {
    case LINE_FOLLOW:          return "LINE_FOLLOW";
    case OBSTACLE_AVOID:       return "OBSTACLE_AVOID";
    case LINE_FOLLOW_WITH_ARM: return "LINE_FOLLOW_WITH_ARM";
    case NO_MODE:
    default:                   return "NO_MODE";
  }
}

// ========== Motion ==========
void driveMotors(int left, int right) {
  static int lastLeft = 0, lastRight = 0;

  if ((lastLeft == 0 && lastRight == 0) && (left != 0 || right != 0)) {
    justStarted = true;
    startBoostStartTime = millis();
  }

  // Clamp to 8-bit PWM
  left  = constrain(left,  -MAX_PWM, MAX_PWM);
  right = constrain(right, -MAX_PWM, MAX_PWM);

  analogWrite(motor_left_forward,   left  > 0 ? left  : 0);
  analogWrite(motor_left_backward,  left  < 0 ? -left : 0);
  analogWrite(motor_right_forward,  right > 0 ? right : 0);
  analogWrite(motor_right_backward, right < 0 ? -right: 0);

  lastLeft = left;
  lastRight = right;
}

void stopmotors() {
  driveMotors(0, 0);
  delay(100);
}

void turn180() {
  driveMotors(100, -100);
  delay(800);
  stopmotors();
}

void turnRight90() {
  driveMotors(TURN_RIGHT_SPEED, -TURN_RIGHT_SPEED);
  delay(TURN_RIGHT_MS);
  stopmotors();
}

// ========== Arm: 5-step routine ==========
void armGrabObject() {
  const uint8_t GRIP_OPEN   = 30;
  const uint8_t GRIP_CLOSED = 0;

  const uint8_t baseAngles[5] = { 10,  40,  40,  40, 10 };
  const uint8_t liftAngles[5] = { 170, 140, 140, 140, 170 };
  const uint8_t gripAngles[5] = { 30, 30, 15, 15, 15 };
  const uint16_t stepDelayMs[5] = { 1000, 1000, 1000, 1000, 1000 };

  for (uint8_t i = 0; i < 5; ++i) {
    Serial.println(i);
    servoBase.write(   constrain(baseAngles[i],  0, 180) );
    delay(1000);
    servoLift.write(   constrain(liftAngles[i],  0, 180) );
    delay(1000);
    servoGripper.write(constrain(gripAngles[i],  0, 180) );
    delay(1000);
  }
}

// ========== Button ==========
void handleButton() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading == LOW && lastButtonState == HIGH) {
    patrolEnabled = !patrolEnabled;

    if (patrolEnabled) {
      uint8_t pattern = readMagPattern();
      currentMode = decodeMagPattern(pattern);
      modeLocked = true;

      Serial.print("🟢 Start pressed. Mode locked: ");
      Serial.println(modeName(currentMode));

      // Update LCD to show current mode + running
      lcdShowMode(currentMode, true);

      if (currentMode == NO_MODE) {
        Serial.println("⚠️ NO_MODE (11). Staying stopped.");
        patrolEnabled = false;   // don't run if no mode selected
        modeLocked = false;
        lcdShowMode(NO_MODE, false);  // reflect not running
      }

    } else {
      stopmotors();
      currentMode = NO_MODE;
      modeLocked = false;
      Serial.println("⛔ Stop pressed. Mode reset to 000 (NO_MODE).");

      // Update LCD to show stopped
      lcdShowMode(NO_MODE, false);
    }
  }

  lastButtonState = reading;
}

// Error = 0 when position is between sensors 3 and 4 (deadband).
inline float computeErrorWithDeadband(float position) {
  const float centerL = 3.0f;  // sensor index 3
  const float centerR = 4.0f;  // sensor index 4
  if (position < centerL) return position - centerL;  // negative → turn left
  if (position > centerR) return position - centerR;  // positive → turn right
  return 0.0f;  // between 3 and 4 → perfect center
}

