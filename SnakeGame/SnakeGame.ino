#include <TFT.h>
#include <SPI.h>

// TFT display pins
#define CS   10
#define DC   9
#define RST  8
TFT TFTscreen = TFT(CS, DC, RST);

// Control buttons
#define BTN_UP     2
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_ENTER  6  // Start/Restart button

// Game grid settings
#define CELL_SIZE      8
#define SCREEN_WIDTH   160
#define SCREEN_HEIGHT  128
#define UI_HEIGHT      10

#define GRID_COLS      (SCREEN_WIDTH / CELL_SIZE)
#define GRID_ROWS      ((SCREEN_HEIGHT - UI_HEIGHT) / CELL_SIZE)
#define MAX_SNAKE_LEN  100

// Snake state
int snakeX[MAX_SNAKE_LEN];
int snakeY[MAX_SNAKE_LEN];
int snakeLength;
int dx, dy;

int foodX, foodY;
bool gameOver = false;
bool gameStarted = false;

int score = 0;
unsigned long startTime;
int lastSecondDisplayed = -1;

// Rising edge detection for ENTER
bool lastEnterState = HIGH;

unsigned int color565(byte r, byte g, byte b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void drawCell(int x, int y, byte r, byte g, byte b) {
  unsigned int color = color565(r, g, b);
  TFTscreen.fillRect(x * CELL_SIZE, (y * CELL_SIZE) + UI_HEIGHT, CELL_SIZE - 1, CELL_SIZE - 1, color);
}

void drawUI() {
  TFTscreen.fillRect(0, 0, SCREEN_WIDTH, UI_HEIGHT, color565(0, 0, 0));
  TFTscreen.stroke(255, 255, 255);
  TFTscreen.setTextSize(1);
  TFTscreen.setCursor(5, 5);
  TFTscreen.print("Score:");
  TFTscreen.print(score);

  TFTscreen.setCursor(SCREEN_WIDTH - 60, 5);
  TFTscreen.print("Time:");
  TFTscreen.print((millis() - startTime) / 1000);
}

void spawnFood() {
  foodX = random(0, GRID_COLS);
  foodY = random(0, GRID_ROWS);
}

void gameOverScreen() {
  TFTscreen.background(255, 0, 0);
  TFTscreen.stroke(255, 255, 255);
  TFTscreen.setTextSize(2);
  TFTscreen.setCursor(30, SCREEN_HEIGHT / 2 - 10);
  TFTscreen.text("Game Over!", 30, SCREEN_HEIGHT / 2 - 10);
  TFTscreen.setTextSize(1);
  TFTscreen.setCursor(35, SCREEN_HEIGHT / 2 + 16);
  TFTscreen.text("Press ENTER", 35, SCREEN_HEIGHT / 2 + 16);
}

void showStartScreen(const char* msg) {
  TFTscreen.background(0, 0, 255);
  TFTscreen.stroke(255, 255, 255);
  TFTscreen.setTextSize(2);
  TFTscreen.setCursor(18, SCREEN_HEIGHT / 2 - 10);
  TFTscreen.text(msg, 18, SCREEN_HEIGHT / 2 - 10);
  TFTscreen.setTextSize(1);
  TFTscreen.setCursor(40, SCREEN_HEIGHT / 2 + 16);
  TFTscreen.text("Press ENTER", 40, SCREEN_HEIGHT / 2 + 16);
}

bool detectEnterRisingEdge() {
  bool currentState = digitalRead(BTN_ENTER);
  if (lastEnterState == LOW && currentState == HIGH) {
    lastEnterState = currentState;
    return true;
  }
  lastEnterState = currentState;
  return false;
}

void waitForEnter(const char* label) {
  showStartScreen(label);
  while (!detectEnterRisingEdge()) {
    delay(10);  // debounce loop
  }
  delay(150);  // debounce release
}

void resetGame() {
  gameOver = false;
  gameStarted = true;
  TFTscreen.background(0, 0, 0);

  dx = 1;
  dy = 0;
  snakeLength = 3;
  score = 0;

  snakeX[0] = 5; snakeY[0] = 5;
  snakeX[1] = 4; snakeY[1] = 5;
  snakeX[2] = 3; snakeY[2] = 5;

  startTime = millis();
  lastSecondDisplayed = -1;
  drawUI();
  spawnFood();
}

void setup() {
  TFTscreen.begin();
  TFTscreen.setRotation(3);  // Landscape
  TFTscreen.background(0, 0, 0);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);

  randomSeed(analogRead(A0));

  waitForEnter("SNAKE GAME");
  resetGame();
}

void loop() {
  if (!gameStarted) return;

  if (gameOver) {
    gameOverScreen();
    if (detectEnterRisingEdge()) {
      resetGame();
    }
    return;
  }

  int currentSecond = (millis() - startTime) / 1000;
  if (currentSecond != lastSecondDisplayed) {
    lastSecondDisplayed = currentSecond;
    drawUI();
  }

  if (digitalRead(BTN_UP) == LOW && dy != 1) {
    dx = 0; dy = -1;
  }
  if (digitalRead(BTN_DOWN) == LOW && dy != -1) {
    dx = 0; dy = 1;
  }
  if (digitalRead(BTN_LEFT) == LOW && dx != 1) {
    dx = -1; dy = 0;
  }
  if (digitalRead(BTN_RIGHT) == LOW && dx != -1) {
    dx = 1; dy = 0;
  }

  // Erase tail
  drawCell(snakeX[snakeLength - 1], snakeY[snakeLength - 1], 0, 0, 0);

  // Move snake
  for (int i = snakeLength - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }

  snakeX[0] += dx;
  snakeY[0] += dy;

  // Check wall collision
  if (snakeX[0] < 0 || snakeX[0] >= GRID_COLS || snakeY[0] < 0 || snakeY[0] >= GRID_ROWS) {
    gameOver = true;
    return;
  }

  // Check self collision
  for (int i = 1; i < snakeLength; i++) {
    if (snakeX[0] == snakeX[i] && snakeY[0] == snakeY[i]) {
      gameOver = true;
      return;
    }
  }

  // Redraw food every frame (prevents disappearance)
  if (!(snakeX[0] == foodX && snakeY[0] == foodY)) {
    drawCell(foodX, foodY, 255, 255, 0);
  }

  // Eat food
  if (snakeX[0] == foodX && snakeY[0] == foodY) {
    if (snakeLength < MAX_SNAKE_LEN) snakeLength++;
    score++;
    drawUI();
    spawnFood();
  }

  // Draw head
  drawCell(snakeX[0], snakeY[0], 0, 255, 0);  // Green

  delay(120);  // Control speed
}
