#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <Firebase_ESP_Client.h>
#include <Adafruit_Fingerprint.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Provide the token generation process info
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Insert your network credentials
#define WIFI_SSID "LasithWifi"
#define WIFI_PASSWORD "12345678"

#define SMTP_SERVER "smtp.gmail.com"
#define SMTP_PORT 465

#define AUTHOR_EMAIL "fingerprintattendance28@gmail.com"
#define AUTHOR_PASSWORD "afslvdjdpnzenpii"
#define RECIPIENT_EMAIL "lasithnawanjana123@gmail.com"

// Insert Firebase project API Key
#define API_KEY "AIzaSyCHQbn47QeX6oTJ0-1gf0Mn_SA_dzhza48"

// Insert Firebase Realtime Database URL
#define DATABASE_URL "https://attendance-count-default-rtdb.firebaseio.com/"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
// NTP server settings
const char* ntpServer = "pool.ntp.org";
const long utcOffsetInSeconds = 19800;  // UTC offset for Sri Lanka (GMT +5:30)

WiFiUDP udp;
NTPClient timeClient(udp, ntpServer, utcOffsetInSeconds);

// Variables for trigger time (hours and minutes)
int triggerHour = 23;   // Default trigger hour
int triggerMinute = 55; // Default trigger minute

long previousMillis = 0;  // For displaying the detected finger message for 2 seconds

// Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;

// Fingerprint Sensor
#define RX_PIN D5 // Replace with your RX pin
#define TX_PIN D6 // Replace with your TX pin
SoftwareSerial fingerSerial(RX_PIN, TX_PIN); // Use SoftwareSerial for fingerprint sensor
Adafruit_Fingerprint finger(&fingerSerial); // Pass the SoftwareSerial object to the Adafruit_Fingerprint class

// Array to store attendance: 0 = Absent, 1 = Present
int attendance[127][1] = {0}; // Initialize all elements to 0

// Variable to track the last day
int lastDay = -1;

int lastEmailMinute = -1; // Initialize to an invalid minute

int currentHour = 0;
int currentMinute = 0;
int currentDay = 0;

void setup() {
  Serial.begin(115200);

    // SSD1306_SWITCHAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC,   SCREEN_ADDRESS))
    {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ;
    }
  
  display.display();
  delay(2000);  // Initial splash screen delay

  fingerSerial.begin(57600); // Fingerprint sensor communication baud rate

  // Initialize Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Assign API key and RTDB URL
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // Sign up to Firebase
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signup successful");
    signupOK = true;
  } else {
    Serial.printf("Firebase signup failed: %s\n", config.signer.signupError.message.c_str());
  }

  // Assign the callback function for the token status
  config.token_status_callback = tokenStatusCallback;

  // Initialize Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Initialize fingerprint sensor
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor initialized successfully");
  } else {
    Serial.println("Fingerprint sensor initialization failed. Check wiring!");
    while (1); // Halt execution if sensor initialization fails
  }

  // Initialize NTP client
  timeClient.begin();
  display.clearDisplay();
}

void loop() {
  timeClient.update();
      // Display current time
  displayTime(timeClient.getFormattedTime());
  displayMessage("Scan Your Finger");

  // Get the current hour, minute, and day
  currentHour = timeClient.getHours();
  currentMinute = timeClient.getMinutes();
  currentDay = timeClient.getDay();

  // Reset attendance array at the start of a new day
  if (currentDay != lastDay) {
    resetAttendanceArray();
    lastDay = currentDay;
    Serial.println("Attendance array reset for the new day.");
  }

  // Print the current time
  Serial.print("Current Time: ");
  Serial.print(timeClient.getFormattedTime());
  Serial.println();

  // Check if the current time matches the trigger time and email hasn't been sent this minute
  if (currentHour == triggerHour && currentMinute == triggerMinute && currentMinute != lastEmailMinute) {
    sendEmailWithTable();
    lastEmailMinute = currentMinute; // Update the last email minute
  }

  if (Firebase.ready() && signupOK) {
    // Check for fingerprint
    int fingerprintID = getFingerprintID();

    // Display "Scan Your Finger" until a finger is detected

    if (fingerprintID != -1) {
      displayFingerStatus(true);
      markAttendanceAndSend(fingerprintID);
    }

  }
  Firebase.RTDB.readStream(&fbdo); // Keep reading the Firebase stream
}

// Function to get fingerprint ID
int getFingerprintID() {
  int result = finger.getImage();
  if (result != FINGERPRINT_OK) return -1;

  result = finger.image2Tz();
  if (result != FINGERPRINT_OK) return -1;

  result = finger.fingerFastSearch();
  if (result != FINGERPRINT_OK) return -1;

  displayFingerStatus(true);
  Serial.print("Fingerprint ID: ");
  Serial.println(finger.fingerID);
  return finger.fingerID;
}

// Function to mark attendance and send data to Firebase
void markAttendanceAndSend(int id) {
  time_t rawTime = timeClient.getEpochTime();
struct tm* timeInfo = localtime(&rawTime);

char dateString[11]; // YYYY-MM-DD + null terminator
sprintf(dateString, "%04d-%02d-%02d", timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday);

  if (id > 0 && id <= 127) {
    String Late="No";
    int Late_time=0;
    // Update the attendance array
    attendance[id - 1][0] = 1; // Mark the student as present (array index starts from 0)
    if (Late=="No"){
      attendance[id - 1][1]=0;
    }
    else if(Late=="Yes"){
      attendance[id - 1][1]=1;
    }

    // Print attendance array for debugging
    Serial.print("Updated Attendance Array: ");
    for (int i = 0; i < 127; i++) {
      Serial.print("[");
      Serial.print(attendance[i][0]);
      Serial.print(",");
      Serial.print(attendance[i][1]);
      Serial.print("] ");
    }

    Serial.println();

    if((currentHour*60+currentMinute-triggerHour*60-triggerMinute)<0){
      Late="No";
      Late_time=0;
    }
    else{
      Late="Yes";
      Late_time=currentHour*60+currentMinute-triggerHour*60-triggerMinute;
    }

    String studentPath = "/attendance/0/students/" + String(id);

    // Create JSON object for the student
    FirebaseJson studentData;
    Firebase.RTDB.setString(&fbdo, "/attendance/0/date", dateString);
    studentData.set("attendance", attendance[id - 1][0] == 1 ? "Present" : "Absent");
    studentData.set("late", Late); // Example: Add logic for lateness calculation
    studentData.set("lateness_minutes", Late_time); // Example: Add lateness minutes

    // Send data to Firebase
    if (Firebase.RTDB.setJSON(&fbdo, studentPath, &studentData)) {
      Serial.print("Attendance marked for student ID: ");
      Serial.println(id);
      Serial.println("Data sent to Firebase successfully");
    } else {
      Serial.print("Failed to send data for student ID: ");
      Serial.println(id);
      Serial.println("Reason: " + fbdo.errorReason());
    }
  }
}

// Function to reset attendance array
void resetAttendanceArray() {
  for (int i = 0; i < 127; i++) {
    attendance[i][0] = 0; // Reset all attendance to 0
    attendance[i][1] = 0; // Reset all attendance to 0
  }
}

void sendEmailWithTable() {
  WiFiClientSecure client;
  client.setInsecure(); // Disable SSL verification for testing

  Serial.println("Connecting to SMTP server...");
  while (!client.connect(SMTP_SERVER, SMTP_PORT)) {
    Serial.println("Connection to SMTP server failed.");
    client.connect(SMTP_SERVER, SMTP_PORT);
    return;
  }

  Serial.println("Connected to SMTP server");

  // SMTP handshake and authentication
  client.printf("EHLO %s\r\n", SMTP_SERVER);
  waitForResponse(client);

  client.printf("AUTH LOGIN\r\n");
  waitForResponse(client);

  client.printf("%s\r\n", encodeBase64(AUTHOR_EMAIL).c_str());
  waitForResponse(client);

  client.printf("%s\r\n", encodeBase64(AUTHOR_PASSWORD).c_str());
  waitForResponse(client);

  // Email composition
  client.printf("MAIL FROM:<%s>\r\n", AUTHOR_EMAIL);
  waitForResponse(client);

  client.printf("RCPT TO:<%s>\r\n", RECIPIENT_EMAIL);
  waitForResponse(client);

  client.printf("DATA\r\n");
  waitForResponse(client);

  // Email headers
  client.printf("From: %s\r\n", AUTHOR_EMAIL);
  client.printf("To: %s\r\n", RECIPIENT_EMAIL);
  client.printf("Subject: Attendance Sheet\r\n");
  client.printf("Content-Type: text/html\r\n\r\n");

  // Email body with table
  client.printf("<html><body>");
  client.printf("<h1>Attendance Sheet</h1>");
  client.printf("<table border='1' style='border-collapse: collapse; width: 100%%;'>");
  client.printf("<tr><th>Student ID</th><th>Attendance</th></tr>");
  
  // Generate 128 rows
  for (int i = 1; i <= 128; i++) {
    client.printf("<tr><td>%d</td><td> %d</td></tr>", i,attendance[i - 1][0] );
  }

  client.printf("</table>");
  client.printf("</body></html>\r\n.\r\n");
  waitForResponse(client);

  client.printf("QUIT\r\n");
  waitForResponse(client);

  Serial.println("Email sent successfully with table!");
}

String encodeBase64(const char* input) {
  String encoded = "";
  const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int i = 0, j = 0;
  unsigned char arr3[3];
  unsigned char arr4[4];

  while (*input) {
    arr3[i++] = *(input++);
    if (i == 3) {
      arr4[0] = (arr3[0] & 0xfc) >> 2;
      arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
      arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
      arr4[3] = arr3[2] & 0x3f;

      for (i = 0; (i < 4); i++)
        encoded += lookup[arr4[i]];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++)
      arr3[j] = '\0';

    arr4[0] = (arr3[0] & 0xfc) >> 2;
    arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
    arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);

    for (j = 0; (j < i + 1); j++)
      encoded += lookup[arr4[j]];

    while ((i++ < 3))
      encoded += '=';
  }

  return encoded;
}

void waitForResponse(WiFiClient& client) {
  while (!client.available()) {
    delay(10);
  }
  while (client.available()) {
    String line = client.readStringUntil('\n');
    Serial.println(line);
  }
}

// Function to display the current time from NTP
void displayTime(String timeStr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Time: ");
  display.println(timeStr);
  display.display();
}

// Function to display a custom message
void displayMessage(String message) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 40);
  display.println(message);
  display.display();
}

// Function to display finger status (either "Detected" or "Unidentified")
void displayFingerStatus(bool isIdentified) {
  
  display.setCursor(0, 0);
  display.setTextSize(1);

  if (isIdentified) {
    display.clearDisplay();
    display.println("Finger Detected");
    display.display();
    delay(2000);  // Show for 2 seconds
  } else {
    display.println("Unidentified");
    display.display();
  }
}

