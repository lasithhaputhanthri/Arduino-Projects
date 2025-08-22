#define MODEM_TX 17  // ESP32 TX → SIM900A RX
#define MODEM_RX 16  // ESP32 RX ← SIM900A TX

HardwareSerial sim900(2);  // UART2 on ESP32

const char* phoneNumber = "+94779740585";  // Replace with your number
const int signalThreshold = 20;
bool smsSent = false;

void setup() {
  Serial.begin(115200);
  sim900.begin(9600, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);
  Serial.println("Initializing SIM900A...");
}

void loop() {
  if (!smsSent) {
    Serial.println("Checking SIM and Network...");

    if (!isSimReady()) {
      Serial.println(" SIM not ready.");
    } else if (!isNetworkRegistered()) {
      Serial.println("Not registered to network.");
    } else {
      int signal = getSignalStrength();
      if (signal >= 0) {
        Serial.print(" Signal strength: ");
        Serial.println(signal);
        if (signal > signalThreshold) {
          sendSMS(phoneNumber, "SOS! I need help.");
          smsSent = true;
        } else {
          Serial.println(" Signal too weak to send SMS.");
        }
      } else {
        Serial.println(" Failed to get signal strength.");
      }
    }
  }

  delay(100); // Retry every 5 seconds if SMS not sent
}

bool isSimReady() {
  sim900.println("AT+CPIN?");
  delay(100);
  String res = readResponse(5000);
  Serial.println("CPIN response: [" + res + "]");
  return res.indexOf("READY") >= 0;
}

bool isNetworkRegistered() {
  sim900.println("AT+CREG?");
  delay(500);
  String res = readResponse(7000);
  Serial.println("CREG response: [" + res + "]");
  return res.indexOf("+CREG: 0,1") >= 0 || res.indexOf("+CREG: 0,5") >= 0;
}

int getSignalStrength() {
  sim900.println("AT+CSQ");
  delay(500);
  String res = readResponse(5000);
  Serial.println("CSQ response: [" + res + "]");
  int idx = res.indexOf("+CSQ:");
  if (idx >= 0) {
    int comma = res.indexOf(",", idx);
    String rssiStr = res.substring(idx + 6, comma);
    return rssiStr.toInt();
  }
  return -1;
}

void sendSMS(String number, String message) {
  Serial.println("Sending SMS...");

  sim900.println("AT+CMGF=1"); // Set text mode
  delay(500);
  String res1 = readResponse(5000);
  Serial.println("CMGF response: [" + res1 + "]");

  sim900.print("AT+CMGS=\"");
  sim900.print(number);
  sim900.println("\"");
  delay(500);
  String prompt = readResponse(5000);
  Serial.println("CMGS prompt response: [" + prompt + "]");

  sim900.print(message);
  sim900.write(26);  // Ctrl+Z
  Serial.println(" Waiting for SMS send confirmation...");

  String finalRes = readResponse(10000); // Give time for modem to respond
  Serial.println("SMS send response: [" + finalRes + "]");

  if (finalRes.indexOf("+CMGS:") >= 0) {
    Serial.println(" SMS sent successfully.");
  } else if (finalRes.indexOf("ERROR") >= 0) {
    Serial.println(" SMS send failed.");
    smsSent = false; // Retry allowed
  } else {
    Serial.println(" Unknown or no response after sending SMS.");
    smsSent = false;
  }
}

String readResponse(unsigned long timeoutMs) {
  String response = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (sim900.available()) {
      response += sim900.readString();
      delay(10); // Buffering time
    }
  }
  return response;
}
