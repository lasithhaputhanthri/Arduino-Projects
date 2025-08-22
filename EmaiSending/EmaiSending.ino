#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP_Mail_Client.h>

#define WIFI_SSID "LasithWifi"
#define WIFI_PASSWORD "12345678"

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

#define AUTHOR_EMAIL "fingerprintattendance28@gmail.com"
#define AUTHOR_PASSWORD "afsl vdjd pnze npii"

#define RECIPIENT_EMAIL "lasithnawanjana123@gmail.com"

SMTPSession smtp;

void smtpCallback(SMTP_Status status);
void sendEmail();

String generateHTMLTable();

void setup() {
  Serial.begin(115200);
  Serial.println();
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

}

void loop() {
  sendEmail();
}

// Generates an HTML table with 128 rows
String generateHTMLTable() {
  String table = "<table border='1' style='border-collapse: collapse;'>";
  table += "<tr><th>Row</th><th>Data</th></tr>";
  for (int i = 1; i <= 128; i++) {
    table += "<tr><td>" + String(i) + "</td><td>Data " + String(i) + "</td></tr>";
  }
  table += "</table>";

  // Use String for concatenation
  String htmlContent = "<div style=\"font-family: Arial, sans-serif; color: #2f4468;\">";
  htmlContent += "<h1>Table with 128 Rows</h1>";
  htmlContent += table;
  htmlContent += "</div>";

  return htmlContent;
}


void smtpCallback(SMTP_Status status) {
  Serial.println(status.info());

  if (status.success()) {
    Serial.println("----------------");
    Serial.printf("Message sent success: %d\n", status.completedCount());
    Serial.printf("Message sent failed: %d\n", status.failedCount());
    Serial.println("----------------\n");

    for (size_t i = 0; i < smtp.sendingResult.size(); i++) {
      SMTP_Result result = smtp.sendingResult.getItem(i);
      Serial.printf("Message No: %d\n", i + 1);
      Serial.printf("Status: %s\n", result.completed ? "success" : "failed");
      Serial.printf("Date/Time: %s\n",
                    MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
      Serial.printf("Recipient: %s\n", result.recipients.c_str());
      Serial.printf("Subject: %s\n", result.subject.c_str());
    }
    smtp.sendingResult.clear();
  }
}

void sendEmail(){
  MailClient.networkReconnect(true);

  smtp.debug(1);
  smtp.callback(smtpCallback);

  Session_Config config;
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;

  config.time.ntp_server = F("pool.ntp.org,time.nist.gov");
  config.time.gmt_offset = 3;
  config.time.day_light_offset = 0;

  SMTP_Message message;
  message.sender.name = F("ESP");
  message.sender.email = AUTHOR_EMAIL;
  message.subject = F("ESP Test Email with Table");
  message.addRecipient(F("Lasith"), RECIPIENT_EMAIL);

  String htmlMsg = generateHTMLTable();
  message.html.content = htmlMsg.c_str();
  message.html.charSet = "us-ascii";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
  message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;

  if (!smtp.connect(&config)) {
    Serial.printf("Connection error, Status Code: %d, Error Code: %d, Reason: %s\n",
                  smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return;
  }

  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.printf("Error, Status Code: %d, Error Code: %d, Reason: %s\n",
                  smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
  }
}
