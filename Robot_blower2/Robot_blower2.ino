// Motor driver pins
#define IN1 27  // Left motor forward
#define IN2 14  // Left motor backward
#define IN3 26  // Right motor forward
#define IN4 25  // Right motor backward

int lastBlacklineL = millis();
int lastBlacklineR = millis();
bool backOff = false;

// Ultrasonic Sensor 1 Pins
const int trigPin1 = 4;
const int echoPin1 = 15;

// PWM speed control pins
#define PWM_LEFT 22   // Left motor speed
#define PWM_RIGHT 23  // Right motor speed

#define FAN_PIN 32

// IR sensor pin
#define IR_SENSOR_PIN 35
#define IR_SENSOR_PIN_2 34

#define SPEED 150 

TaskHandle_t irSensorTaskHandle;

int now_time = millis();

void stopMotors();
void irSensorTask(void *parameter);

volatile bool isCliffDetectedL = false;
volatile bool isCliffDetectedR = false;
const int obstacleDistanceThreshold1 = 15;
int distance1;

void setup() {
  Serial.begin(115200);

  // Set up motor control pins as outputs
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(trigPin1, OUTPUT);
  pinMode(echoPin1, INPUT);

  // Set up PWM pins
  pinMode(PWM_LEFT, OUTPUT);
  pinMode(PWM_RIGHT, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  // Set up IR sensor pin
  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(IR_SENSOR_PIN_2, INPUT);

  // Stop both motors initially
  stopMotors();

  // Create a task for the IR sensor on core 1
  xTaskCreatePinnedToCore(
    irSensorTask,         // Function to be executed
    "IRSensorTask",       // Name of the task
    2048,                 // Stack size
    NULL,                 // Task input parameter
    1,                    // Task priority
    &irSensorTaskHandle,  // Task handle
    1                     // Core 1
  );
}

void loop() {
  analogWrite(FAN_PIN,230);
  delay(100000);   
  if (backOff) {
    // Move backward
    setMotor("left", "backward", SPEED);
    setMotor("right", "backward", SPEED);
    delay(1000);  // Move back for 1 second

    // // Turn (e.g., left turn)
    // setMotor("left", "backward", SPEED);
    // setMotor("right", "forward", SPEED);
    // int randomDelay = random(300, 1500);  // Random turn duration between 300 and 2500 ms

    // delay(randomDelay);  // Turn for a random time

    // // Stop motors
    // stopMotors();

    // Turn (e.g., left turn)
    setMotor("left", "backward", SPEED);
    setMotor("right", "forward", SPEED);


    delay(1000);  // Turn for a random time

    // Stop motors
    stopMotors();

    backOff = false;
  }
  if (distance1 <= obstacleDistanceThreshold1) {
    // Stop motors
    stopMotors();
    Serial.println("Fan ON");
    analogWrite(5,153);
    delay(2000);
    analogWrite(5,0);

    // Turn (e.g., left turn)
    setMotor("left", "backward", SPEED);
    setMotor("right", "forward", SPEED);
    int randomDelay = random(100, 1200);  // Random turn duration between 300 and 2500 ms

    delay(randomDelay);  // Turn for a random time
  }

  if (isCliffDetectedR) {
    Serial.println("Cliff detected! Moving back and turning.");

    // // Move backward
    // setMotor("left", "backward", SPEED);
    // setMotor("right", "backward", SPEED);
    // delay(1000);  // Move back for 1 second

    // Turn (e.g., left turn)
    setMotor("left", "backward", SPEED);
    setMotor("right", "forward", SPEED);
    int randomDelay = random(300, 1500);  // Random turn duration between 300 and 2500 ms

    delay(randomDelay);  // Turn for a random time

    // Stop motors
    stopMotors();

    // Reset cliff detection flag
    isCliffDetectedR = false;
  } else if (isCliffDetectedL) {
    Serial.println("Cliff detected! Moving back and turning.");

    // // Move backward
    // setMotor("left", "backward", SPEED);
    // setMotor("right", "backward", SPEED);
    // delay(1000);  // Move back for 1 second

    // Turn (e.g., left turn)
    setMotor("left", "forward", SPEED);
    setMotor("right", "backward", SPEED);
    int randomDelay = random(300, 1500);  // Random turn duration between 300 and 2500 ms

    delay(randomDelay);  // Turn for a random time

    // Stop motors
    stopMotors();

    // Reset cliff detection flag
    isCliffDetectedL = false;
  } else {
    setMotor("left", "forward", SPEED);
    setMotor("right", "forward", SPEED);
    // // Continue moving forward
    // if (millis() - now_time < 3000) {
    //   setMotor("left", "forward", SPEED);
    //   setMotor("right", "forward", SPEED);
    // } else {
    //   now_time = millis();
    //   setMotor("left", "stop", 0);
    //   setMotor("right", "stop", 0);
    //   delay(300);
    // }
  }

  // delay(100);  // Small delay for stability
}

// Function to control a motor's direction and speed
void setMotor(String motor, String direction, int speed) {
  if (motor == "left") {
    analogWrite(PWM_LEFT, speed);  // Set speed for left motor
    if (direction == "forward") {
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, HIGH);
    } else if (direction == "backward") {
      digitalWrite(IN1, HIGH);
      digitalWrite(IN2, LOW);
    } else if (direction == "stop") {
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, LOW);
    }
  } else if (motor == "right") {
    analogWrite(PWM_RIGHT, speed);  // Set speed for right motor
    if (direction == "forward") {
      digitalWrite(IN3, LOW);
      digitalWrite(IN4, HIGH);
    } else if (direction == "backward") {
      digitalWrite(IN3, HIGH);
      digitalWrite(IN4, LOW);
    } else if (direction == "stop") {
      digitalWrite(IN3, LOW);
      digitalWrite(IN4, LOW);
    }
  }
}

// Function to stop both motors
void stopMotors() {
  setMotor("left", "stop", 0);
  setMotor("right", "stop", 0);
}

// IR sensor task function that runs on core 1
void irSensorTask(void *parameter) {
  while (true) {
    distance1 = getDistance(trigPin1, echoPin1);
    if (digitalRead(IR_SENSOR_PIN) == HIGH) {
      isCliffDetectedR = true;  // Set cliff detection flag
      lastBlacklineR = millis();

      if (lastBlacklineR - lastBlacklineL < 1000) {
        backOff = true;
      }
    }
    if (digitalRead(IR_SENSOR_PIN_2) == HIGH) {
      isCliffDetectedL = true;  // Set cliff detection flag
      lastBlacklineL = millis();
      if (lastBlacklineL - lastBlacklineR < 1000) {
        backOff = true;
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);  // Delay for 50ms for faster updates
  }
}

int getDistance(int trigPin, int echoPin) {
  // Send a pulse to the trigger pin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Read the echo pin, measure the pulse width
  long duration = pulseIn(echoPin, HIGH);

  // Calculate the distance (in cm)
  int distance = duration * 0.0344 / 2;
  return distance;
}