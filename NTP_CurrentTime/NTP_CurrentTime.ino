#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

const char* ssid = "LasithWifi";
const char* password = "12345678";

// NTP server settings
const char* ntpServer = "pool.ntp.org";
const long utcOffsetInSeconds = 19800;  // UTC offset for Sri Lanka (GMT +5:30)

WiFiUDP udp;
NTPClient timeClient(udp, ntpServer, utcOffsetInSeconds);

// Variables for trigger time (hours and minutes)
int triggerHour = 9;   // Default trigger hour
int triggerMinute = 15; // Default trigger minute

void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi!");

  // Initialize NTP client
  timeClient.begin();

  // Optional: Get the trigger time from Serial input (if desired)
  // Uncomment to allow dynamic input from Serial monitor
  /*
  Serial.print("Enter hour to trigger the function: ");
  while (Serial.available() == 0) {}
  triggerHour = Serial.parseInt();
  Serial.print("Enter minute to trigger the function: ");
  while (Serial.available() == 0) {}
  triggerMinute = Serial.parseInt();
  Serial.print("Trigger time set to: ");
  Serial.print(triggerHour);
  Serial.print(":");
  Serial.println(triggerMinute);
  */
}

void loop() {
  timeClient.update();

  // Get the current hour and minute
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  // Print the time
  Serial.print("Current Time: ");
  Serial.print(timeClient.getFormattedTime());
  Serial.println();

  // Check if the current time matches the trigger time
  if (currentHour == triggerHour && currentMinute == triggerMinute) {
    runFunctionAtTriggerTime();
  }

  delay(1000);  // Check every second
}

void runFunctionAtTriggerTime() {
  // Your code to execute at the specified trigger time
  Serial.println("It's the specified time, running the function...");
}
