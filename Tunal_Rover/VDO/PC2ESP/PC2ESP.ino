#include <TFT_eSPI.h>
#include <SPIFFS.h>

TFT_eSPI tft = TFT_eSPI();  // Create TFT instance

String base64Data = "";     // To hold incoming base64 data
bool receiving = false;     // Flag to indicate if receiving a frame

// Base64 decoding table
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 decoding function
int decode_base64(const char* input, uint8_t* output) {
  int len = strlen(input);
  int i = 0, j = 0, pad = 0;
  
  // Decode input Base64 string
  while (i < len) {
    uint32_t sextet_a = (input[i] == '=') ? 0 & pad++ : base64_table_index(input[i]);
    uint32_t sextet_b = (input[++i] == '=') ? 0 & pad++ : base64_table_index(input[i]);
    uint32_t sextet_c = (input[++i] == '=') ? 0 & pad++ : base64_table_index(input[i]);
    uint32_t sextet_d = (input[++i] == '=') ? 0 & pad++ : base64_table_index(input[i]);
    
    uint32_t combined = (sextet_a << 18) | (sextet_b << 12) | (sextet_c << 6) | sextet_d;
    
    if (j < len) output[j++] = (combined >> 16) & 0xFF;
    if (j < len) output[j++] = (combined >> 8) & 0xFF;
    if (j < len) output[j++] = combined & 0xFF;
  }
  
  return j; // Return length of decoded data
}

// Function to return the base64 index of a character
int base64_table_index(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;  // Invalid character
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);        // Adjust rotation if needed
  tft.fillScreen(TFT_BLACK);
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();

    if (ch == '<') {          // Start of a frame marker
      base64Data = "";
      receiving = true;
    } else if (ch == '>') {   // End of frame marker
      receiving = false;
      displayImage();         // Decode and display the image
    } else if (receiving) {
      base64Data += ch;
    }
  }
}

// Decode base64 and display image
void displayImage() {
  // Calculate required buffer size for decoded data
  int imgLen = base64Data.length() * 3 / 4;
  uint8_t *imgBuffer = (uint8_t *)malloc(imgLen);

  if (imgBuffer == nullptr) {
    Serial.println("Memory allocation failed!");
    return;
  }

  int decodedLen = decode_base64(base64Data.c_str(), imgBuffer);
  if (decodedLen > 0) {
    // Render image on TFT (adjust width & height for your 128x160 display)
    tft.pushImage(0, 0, 128, 160, (uint16_t *)imgBuffer);
  } else {
    Serial.println("Decoding failed!");
  }

  free(imgBuffer);
}
