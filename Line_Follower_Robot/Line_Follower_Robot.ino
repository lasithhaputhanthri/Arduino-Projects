// PID Parameters
float Kp = 30;  // Proportional gain
float Ki = 0.1;   // Integral gain
float Kd = 10;  // Derivative gain

// Variables for PID
float error = 0, previous_error = 0;
float integral = 0, derivative = 0;
float correction = 0;

// IR Sensor pins
const int IR_pins[] = { 33, 34, 35, 36, 32, 39, 25, 26 };  // IR sensor pins
const int num_sensors = 8;

// Motor control pins
const int motor_left_forward = 18;
const int motor_left_backward = 19;
const int motor_right_forward = 23;
const int motor_right_backward = 22;

// Base motor speed
const int base_speed = 65;

const int NumStations = 3;
int currentStation = 0;

bool shiftCompleted = false;

int returncount=0;

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < num_sensors; i++) {
    pinMode(IR_pins[i], INPUT);
  }

  pinMode(motor_left_forward, OUTPUT);
  pinMode(motor_left_backward, OUTPUT);
  pinMode(motor_right_forward, OUTPUT);
  pinMode(motor_right_backward, OUTPUT);
}

void loop() {
  if (shiftCompleted == false) {
    Serial.println(currentStation);
    int position = readSensors();

    // Check if no line is detected (all sensors are off)
    if (isNoLineDetected()) {
      Serial.println("No line detected! Stopping and turning 180 degrees.");
      stopmotors();  // Stop the robot
      delay(2000);   // Wait for 2 seconds
      turn180();     // Turn 180 degrees
      return;
    }

    int junction = detectLJunction();

    if (junction == 3) {
      Serial.println("T-Junction detected! Turning right.");
      stopmotors();  // Stop the robot for 500ms at the junction
      delay(500);
      turnRight();
      return;
    } else if (junction == 1) {
      Serial.println("Left L-Junction detected! Turning Left.");
      stopmotors();  // Stop the robot for 500ms at the junction
      delay(500);
      turnLeft();
      return;
    } else if (junction == 2) {
      Serial.println("Right L-Junction detected! Turning Right.");
      stopmotors();  // Stop the robot for 500ms at the junction
      delay(500);
      turnRight();
      return;
    }

    error = position - (num_sensors - 1) / 2.0;
    integral += error;
    derivative = error - previous_error;
    correction = (Kp * error) + (Ki * integral) + (Kd * derivative);
    previous_error = error;

    int left_motor_speed = base_speed + correction;
    int right_motor_speed = base_speed - correction;

    left_motor_speed = constrain(left_motor_speed, 0, 1023);
    right_motor_speed = constrain(right_motor_speed, 0, 1023);

    Serial.print("Sensor Values: ");
    for (int i = 0; i < num_sensors; i++) {
      Serial.print(digitalRead(IR_pins[i]));
      Serial.print(" ");
    }
    Serial.println();

    Serial.print("Left Speed: ");
    Serial.print(left_motor_speed);
    Serial.print(" Right Speed: ");
    Serial.println(right_motor_speed);

    driveMotors(left_motor_speed, right_motor_speed);
    delay(10);
  } else {

    Serial.println(currentStation);
    int position = readSensors();

    // Check if no line is detected (all sensors are off)
    if (isNoLineDetected()) {
      Serial.println("No line detected! Stopping and turning 180 degrees.");
      stopmotors();  // Stop the robot
      delay(2000);   // Wait for 2 seconds
      turn180();     // Turn 180 degrees
      return;
    }

    int junction = detectLJunction();

    if (junction == 3) {
      Serial.println("T-Junction detected! Turning right.");
      stopmotors();  // Stop the robot for 500ms at the junction
      delay(500);
      //turnRight();
      return;
    } else if (junction == 1) {
      Serial.println("Left L-Junction detected! Turning Left.");
      stopmotors();  // Stop the robot for 500ms at the junction
      delay(500);
      //turnLeft();
      return;
    } else if (junction == 2) {
      Serial.println("Right L-Junction detected! Turning Right.");
      stopmotors();  // Stop the robot for 500ms at the junction
      delay(500);
      returncount=returncount+1;
      if ((returncount==3)||(returncount==4)) {
        driveMotors(70, 70);
        delay(50);
        return;
      }
      turnRight();
      return;
    }

    error = position - (num_sensors - 1) / 2.0;
    integral += error;
    derivative = error - previous_error;
    correction = (Kp * error) + (Ki * integral) + (Kd * derivative);
    previous_error = error;

    int left_motor_speed = base_speed + correction;
    int right_motor_speed = base_speed - correction;

    left_motor_speed = constrain(left_motor_speed, 0, 1023);
    right_motor_speed = constrain(right_motor_speed, 0, 1023);

    Serial.print("Sensor Values: ");
    for (int i = 0; i < num_sensors; i++) {
      Serial.print(digitalRead(IR_pins[i]));
      Serial.print(" ");
    }
    Serial.println();

    Serial.print("Left Speed: ");
    Serial.print(left_motor_speed);
    Serial.print(" Right Speed: ");
    Serial.println(right_motor_speed);

    driveMotors(left_motor_speed, right_motor_speed);
    delay(10);
  }
}

// Function to check if no line is detected
bool isNoLineDetected() {
  if (currentStation == NumStations) {
    shiftCompleted = true;
  }
  for (int i = 0; i < num_sensors; i++) {
    if (digitalRead(IR_pins[i]) == 1) {  // If any sensor detects the line
      return false;
    }
  }
  currentStation = currentStation + 1;
  return true;  // No line detected if all sensors return 0
}

// Function to turn the robot 180 degrees
void turn180() {
  // Turn one of the motors in reverse and the other forward to turn 180 degrees
  driveMotors(120, -120);  // Rotate the robot
  delay(600);              // Adjust this delay to control the turn duration
  stopmotors();
}


// Function to read sensors and calculate position
int readSensors() {
  int weighted_sum = 0;
  int sum = 0;

  for (int i = 0; i < num_sensors; i++) {
    int sensor_value = digitalRead(IR_pins[i]);
    weighted_sum += sensor_value * i;
    sum += sensor_value;
  }

  if (sum == 0) return (num_sensors - 1) / 2;
  return weighted_sum / sum;
}

// Function to detect an L-junction
int detectLJunction() {
  int left_side = digitalRead(IR_pins[0]) * digitalRead(IR_pins[1]) * digitalRead(IR_pins[2]) * digitalRead(IR_pins[3]);
  int right_side = digitalRead(IR_pins[num_sensors - 1]) * digitalRead(IR_pins[num_sensors - 2]) * digitalRead(IR_pins[num_sensors - 3]) * digitalRead(IR_pins[num_sensors - 4]);

  Serial.print("left_side: ");
  Serial.print(left_side);
  Serial.print(" Right side: ");
  Serial.println(right_side);

  if (left_side * right_side == 1) return 3;  // T-Junction
  else if (left_side == 1) return 1;          // Left L-Junction
  else if (right_side == 1) return 2;         // Right L-Junction

  return 0;  // No L-Junction detected
}

// Function to turn left at an L-Junction
void turnLeft() {
  driveMotors(0, 120);
  delay(700);
}

// Function to turn right at an L-Junction
void turnRight() {
  driveMotors(120, 0);
  delay(800);
}

// Function to drive motors
void driveMotors(int left_speed, int right_speed) {
  if (left_speed > 0) {
    analogWrite(motor_left_forward, left_speed);
    analogWrite(motor_left_backward, 0);
  } else {
    analogWrite(motor_left_forward, 0);
    analogWrite(motor_left_backward, -left_speed);
  }

  if (right_speed > 0) {
    analogWrite(motor_right_forward, right_speed);
    analogWrite(motor_right_backward, 0);
  } else {
    analogWrite(motor_right_forward, 0);
    analogWrite(motor_right_backward, -right_speed);
  }
}

void stopmotors() {
  driveMotors(0, 0);
  delay(500);
}
