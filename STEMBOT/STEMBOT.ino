#include <Arduino.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp32-hal-ledc.h>  // <-- added: LEDC pin-based API

// ========== Mode Handling ==========
enum Mode { NO_MODE = 0, LINE_FOLLOW, OBSTACLE_AVOID, LINE_FOLLOW_WITH_ARM, DISTANCE_MEASURE, MOTOR_CONTROL, LINE_DETECT, ROBOT_ARM };

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
#define ULTRA_FRONT_ECHO 12
#define OBSTACLE_DISTANCE_CM 15

// ---- Magnet Sensor (2 digital channels, external pull-ups, active-LOW) ----
#define MAG1_PIN 14 
#define MAG2_PIN 27
#define MAG3_PIN 15   

// Black gives HIGHER ADC on your line sensors:
const bool LINE_IS_BLACK = false;

// ESP32 8-bit PWM range
const int MAX_PWM = 255;

// ---- added: LEDC PWM settings for motors (pin-based API) ----
const uint32_t LEDC_MOTOR_FREQ = 1000; // 20 kHz (quiet)
const uint8_t  LEDC_MOTOR_RES  = 8;     // duty 0..255

// Current mode
Mode currentMode = NO_MODE;  // default is no mode until Start is pressed
static Mode lastMode = NO_MODE;

// ========== Button / Run State ==========
bool patrolEnabled = false;
bool lastButtonState = HIGH;
bool modeLocked = false;

// ========== PID (UNCHANGED as requested) ==========
float Kp =15, Ki = 0, Kd = 100;
float error = 0, previous_error = 0;
float integral = 0, derivative = 0;
float correction = 0;

int offsetleft=0;
int offsetright=0;
// ========== Drive / Boost ==========
const int BASE_SPEED_NORMAL   = 105;
const int BASE_SPEED_STARTING = 105;
const int START_BOOST_DURATION_MS = 10;

bool justStarted = true;
unsigned long startBoostStartTime = 0;

// Last seen line position (for brief line loss)
float lastPosition = (num_sensors - 1) / 2.0;  // center (3.5 with 8 sensors)

// ----- Obstacle-Avoid tuning -----
const int OA_FORWARD_SPEED = 130;
const int TURN_RIGHT_SPEED = 130;
const int TURN_RIGHT_MS    = 1000;  // calibrate

// For LINE_FOLLOW_WITH_ARM
const int GRAB_PAUSE_MS    = 300;

// ====== Servos (3x 9g) ======
Servo servoBase, servoLift, servoGripper;
#define SERVO_BASE_PIN    2
#define SERVO_LIFT_PIN    4
#define SERVO_GRIPPER_PIN 13

// ====== Analog IR thresholds (per sensor, aligned to IR_pins order) ======
int IR_THRESH_LO[num_sensors] = {100,900,100,1701,3400,2000,80,350};
int IR_THRESH_HI[num_sensors] = {160,1000,150,2464,3500,2662,130,450};
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
void distanceMeasureMode();
void motorControlMode();
void lineDetectMode();


bool isObstacleDetected();
float readSensors();
int readRawIR(int pin);    // analog+threshold → 1 on line, 0 background
bool isLineEnded();
void turn180();
void turnRight90();
long readUltrasonicDistanceCM(int trigPin, int echoPin);
// long readUltrasonicDistanceCM(int trigPin);
float computeErrorWithDeadband(float position);

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

const int GRIPPER_MIN_US = 600;
const int GRIPPER_MAX_US = 2350;


// ====== LCD helpers ======
const char* modeShort(Mode m) {
  switch (m) {
    case LINE_FOLLOW:          return "LINE";
    case OBSTACLE_AVOID:       return "AVOID";
    case LINE_FOLLOW_WITH_ARM: return "ARM";
    case DISTANCE_MEASURE: return "DISTANCE";
    case MOTOR_CONTROL: return "MOTOR";
    case LINE_DETECT: return "LINE";
    case ROBOT_ARM: return "ROBOT_ARM";
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

  // ---- added: attach LEDC PWM to motor pins (pin-based API, no channels needed) ----
  ledcAttach(motor_left_forward,   LEDC_MOTOR_FREQ, LEDC_MOTOR_RES);
  ledcAttach(motor_left_backward,  LEDC_MOTOR_FREQ, LEDC_MOTOR_RES);
  ledcAttach(motor_right_forward,  LEDC_MOTOR_FREQ, LEDC_MOTOR_RES);
  ledcAttach(motor_right_backward, LEDC_MOTOR_FREQ, LEDC_MOTOR_RES);

  // Ultrasonic (front dual-pin)
  pinMode(ULTRA_FRONT_TRIG, OUTPUT);
  pinMode(ULTRA_FRONT_ECHO, INPUT);

  // Magnet sensor pins (external pull-ups → plain INPUT)
  pinMode(MAG1_PIN, INPUT);
  pinMode(MAG2_PIN, INPUT);
  pinMode(MAG3_PIN, INPUT);

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
  servoGripper.attach(SERVO_GRIPPER_PIN, GRIPPER_MIN_US, GRIPPER_MAX_US);

  // Neutral pose
  servoBase.write(10);
  servoLift.write(180);
  delay(500);
  servoGripper.write(5); // open
  //delay(1000);

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

    case DISTANCE_MEASURE:
      distanceMeasureMode();
      break;

    case MOTOR_CONTROL:
      motorControlMode();
      break;

    case LINE_DETECT:
      lineDetectMode();
      break;

    case ROBOT_ARM:
      RobotArmMode(); 
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

// ========== Mode: ROBOT ARM ==========
void RobotArmMode() {
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

  int left_speed  = (int)constrain(BASE_SPEED_NORMAL + correction,  0, MAX_PWM)+offsetleft;
  int right_speed = (int)constrain(BASE_SPEED_NORMAL - correction,  0, MAX_PWM)+offsetright;

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
  // long d = readUltrasonicDistanceCM(ULTRA_FRONT_TRIG);
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

// long readUltrasonicDistanceCM(int trigPin) {
//   pinMode(trigPin, OUTPUT);
//   digitalWrite(trigPin, LOW);
//   delayMicroseconds(2);
//   digitalWrite(trigPin, HIGH);
//   delayMicroseconds(10);
//   digitalWrite(trigPin, LOW);

//   pinMode(trigPin, INPUT);
//   delayMicroseconds(50);                 // allow line to settle
//   unsigned long dur = pulseIn(trigPin, HIGH, 40000UL); // 40ms timeout
//   if (dur == 0) return -1;
//   return (long)(dur * 0.034f / 2.0f);
// }



// ====== Magnet Sensor: read & decode pattern (DIGITAL, active-LOW) ======
uint8_t readMagPattern() {
  // external pull-ups: idle=HIGH, active=LOW → invert so "present"=1
  uint8_t b1 = !digitalRead(MAG1_PIN); // LSB
  uint8_t b2 = !digitalRead(MAG2_PIN); // MSB
  uint8_t b3 = !digitalRead(MAG3_PIN);
  return (b3<<2) | (b2 << 1) | b1;
}

// Mode decodeMagPattern(uint8_t pattern) {
//   switch (pattern) {
//     case 0b11: return LINE_FOLLOW;
//     case 0b10: return OBSTACLE_AVOID;
//     case 0b01: return LINE_FOLLOW_WITH_ARM;
//     case 0b00: default: return NO_MODE;
//   }
// }

Mode decodeMagPattern(uint8_t pattern) {
  switch (pattern) {
    case 0b111: return LINE_DETECT; //pink
    case 0b110: return OBSTACLE_AVOID; //cream
    case 0b101: return LINE_FOLLOW_WITH_ARM; //light yello
    case 0b100: return ROBOT_ARM; //blue
    case 0b011: return DISTANCE_MEASURE; // light green
    case 0b010: return MOTOR_CONTROL;//purple
    case 0b001: return LINE_FOLLOW; //yellow
    case 0b000: default: return NO_MODE;
  }
}

const char* modeName(Mode m) {
  switch (m) {
    case LINE_FOLLOW:          return "LINE_FOLLOW";
    case OBSTACLE_AVOID:       return "OBSTACLE_AVOID";
    case LINE_FOLLOW_WITH_ARM: return "LINE_FOLLOW_WITH_ARM";
    case DISTANCE_MEASURE: return "DISTANCE_MEASURE";
    case MOTOR_CONTROL: return "MOTOR_CONTROL";
    case LINE_DETECT: return "LINE_DETECT";
    case ROBOT_ARM: return "ROBOT_ARM";
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

  // ---- changed: use LEDC pin-based API ----
  ledcWrite(motor_left_forward,   left  > 0 ? left  : 0);
  ledcWrite(motor_left_backward,  left  < 0 ? -left : 0);
  ledcWrite(motor_right_forward,  right > 0 ? right : 0);
  ledcWrite(motor_right_backward, right < 0 ? -right: 0);

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
  const uint8_t liftAngles[5] = { 180, 140, 140, 140, 180 };
  const uint8_t gripAngles[5] = { 30, 30, 30, 5, 5 };
  const uint16_t stepDelayMs[5] = { 1000, 1000, 1000, 1000, 1000 };

  for (uint8_t i = 0; i < 5; ++i) {
    Serial.println(i);
    servoBase.write(baseAngles[i]);
    // delay(1000);
    servoLift.write(liftAngles[i]);
    delay(750);
    servoGripper.write(gripAngles[i]);
    delay(500);

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

void distanceMeasureMode() {
  static bool first = true;
  static unsigned long last = 0;
  const unsigned long INTERVAL_MS = 200;  // update ~5 Hz

  if (lastMode != currentMode) {
    first = true;
    lastMode = currentMode;
  }


  // Make sure motors are stopped and set up the LCD header once
  if (first) {
    stopmotors();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Distance (cm)");
    first = false;
  }

  unsigned long now = millis();
  if (now - last >= INTERVAL_MS) {
    last = now;

    long d = readUltrasonicDistanceCM(ULTRA_FRONT_TRIG, ULTRA_FRONT_ECHO);
    // long d = readUltrasonicDistanceCM(ULTRA_FRONT_TRIG);

    // Serial debug
    Serial.print("Distance: ");
    Serial.print(d);
    Serial.println(" cm");

    // LCD line 2 (pad with spaces to clear old text)
    lcd.setCursor(0, 1);
    if (d < 0 || d > 400) {               // typical HC-SR04 useful range
      lcd.print("Out of range    ");
    } else {
      char buf[17];
      snprintf(buf, sizeof(buf), "%4ld cm         ", d);
      lcd.print(buf);
    }
  }
}
void motorControlMode() {
  // Tunables
  const int MC_FWD_SPEED   = 190;    // 0..255
  const int MC_BACK_SPEED  = -190;   // negative = reverse
  const int MC_TURN_SPEED  = 190;    // in-place turns
  const unsigned long MC_FWD_MS   = 700;
  const unsigned long MC_BACK_MS  = 700;
  const unsigned long MC_TURN_MS  = 600;
  const unsigned long MC_PAUSE_MS = 250;

  enum Phase : uint8_t {
    FWD, PAUSE1, BACK, PAUSE2, LEFT_TURN, PAUSE3, RIGHT_TURN, PAUSE4
  };

  static bool first = true;
  static Phase phase = FWD;
  static unsigned long t0 = 0;

  if (lastMode != currentMode) {
    first = true;
    lastMode = currentMode;
  }


  auto setPhase = [&](Phase p, const char* label){
    phase = p;
    t0 = millis();
    // UI
    Serial.println(label);
    lcd.setCursor(0,0); lcd.print("Motor Test      ");
    lcd.setCursor(0,1); 
    lcd.print(label);
    // pad to clear leftover chars on LCD line
    int len = strlen(label);
    for (int i = len; i < 16; ++i) lcd.print(' ');
  };

  if (first) {
    stopmotors();
    lcd.clear();
    setPhase(FWD, "Forward");
    first = false;
  }

  unsigned long now = millis();
  switch (phase) {
    case FWD:
      driveMotors(MC_FWD_SPEED, MC_FWD_SPEED);
      if (now - t0 >= MC_FWD_MS) { stopmotors(); setPhase(PAUSE1, "Pause"); }
      break;

    case PAUSE1:
      stopmotors();
      if (now - t0 >= MC_PAUSE_MS) { setPhase(BACK, "Backward"); }
      break;

    case BACK:
      driveMotors(MC_BACK_SPEED, MC_BACK_SPEED);
      if (now - t0 >= MC_BACK_MS) { stopmotors(); setPhase(PAUSE2, "Pause"); }
      break;

    case PAUSE2:
      stopmotors();
      if (now - t0 >= MC_PAUSE_MS) { setPhase(LEFT_TURN, "Turn Left"); }
      break;

    case LEFT_TURN:
      driveMotors(-MC_TURN_SPEED, MC_TURN_SPEED); // left wheel back, right forward
      if (now - t0 >= MC_TURN_MS) { stopmotors(); setPhase(PAUSE3, "Pause"); }
      break;

    case PAUSE3:
      stopmotors();
      if (now - t0 >= MC_PAUSE_MS) { setPhase(RIGHT_TURN, "Turn Right"); }
      break;

    case RIGHT_TURN:
      driveMotors(MC_TURN_SPEED, -MC_TURN_SPEED); // left forward, right back
      if (now - t0 >= MC_TURN_MS) { stopmotors(); setPhase(PAUSE4, "Pause"); }
      break;

    case PAUSE4:
      stopmotors();
      if (now - t0 >= MC_PAUSE_MS) { setPhase(FWD, "Forward"); } // loop
      break;
  }
}

void lineDetectMode() {
  static bool first = true;
  if (lastMode != currentMode) { first = true; lastMode = currentMode; }

  // One-time LCD setup when entering this mode
  if (first) {
    stopmotors();
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("IR:");
    lcd.setCursor(0,1); lcd.print("pos:");   // label once
    first = false;
  }

  // Update cached IR states/bits and weighted position
  float pos = readSensors();  // updates irBits, irLastRaw, lastPosition

  // Build an 8-char bit string (left → right; MSB = leftmost sensor)
  char bitsStr[9];
  for (int i = num_sensors - 1, k = 0; i >= 0; --i, ++k) {
    bitsStr[k] = ((irBits >> i) & 1) ? '1' : '0';
  }
  bitsStr[8] = '\0';

  // Update LCD without clearing the whole screen (avoids flicker)
  lcd.setCursor(3, 0);           // after "IR:"
  lcd.print(bitsStr);

  lcd.setCursor(4, 1);           // after "pos:"
  char buf[12];
  snprintf(buf, sizeof(buf), "%4.2f   ", pos);  // pad spaces to overwrite old
  lcd.print(buf);

  delay(150);  // throttle so the LCD stays readable
}
