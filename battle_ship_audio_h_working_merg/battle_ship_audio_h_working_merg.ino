#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

#include "battleship_types.h"
#include "audio.h"

#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

// How many NeoPixels are attached to the Arduino?
#define LED_COUNT 100

// -------- PLAYER 1 PINS --------
#define P1_GREEN_BTN   42
#define P1_RED_BTN     41
#define P1_GREEN_LED   40
#define P1_RED_LED     39
#define P1_POT_ROW     6
#define P1_POT_COL     7
#define P1_LED_PIN1     4   // NeoPixel data pin
#define P1_LED_PIN2     8

// -------- PLAYER 2 PINS --------
#define P2_GREEN_BTN   38
#define P2_RED_BTN     37
#define P2_GREEN_LED   36
#define P2_RED_LED     35
#define P2_POT_ROW     9
#define P2_POT_COL     10
#define P2_LED_PIN1     1
#define P2_LED_PIN2     2

// NOTE: Player / GameState enums are now in battleship_types.h

Player activePlayer = PLAYER_1;
GameState gameState = WAITING_FOR_AIM;

Player otherPlayer(Player p) {
  return (p == PLAYER_1) ? PLAYER_2 : PLAYER_1;
}

struct Board {
  int ships[10][10];
  bool found[10][10];
  int remaining;
};

Board boards[2];

void initrandomMatrix(Board &b);

struct PlayerHW {
  Adafruit_NeoPixel strip1;
  Adafruit_NeoPixel strip2;
  int potRowPin;
  int potColPin;
  int greenBtn;
  int redBtn;
  int greenLED;
  int redLED;

  int inputRow;
  int inputCol;
  bool aimingActive;
  uint32_t previousColors1[LED_COUNT];
  uint32_t previousColors2[LED_COUNT];
};

PlayerHW players[2] = {
  {
    Adafruit_NeoPixel(LED_COUNT, P1_LED_PIN1, NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(LED_COUNT, P1_LED_PIN2, NEO_GRB + NEO_KHZ800),
    P1_POT_ROW, P1_POT_COL,
    P1_GREEN_BTN, P1_RED_BTN,
    P1_GREEN_LED, P1_RED_LED,
    -1, -1, false
  },
  {
    Adafruit_NeoPixel(LED_COUNT, P2_LED_PIN1, NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(LED_COUNT, P2_LED_PIN2, NEO_GRB + NEO_KHZ800),
    P2_POT_ROW, P2_POT_COL,
    P2_GREEN_BTN, P2_RED_BTN,
    P2_GREEN_LED, P2_RED_LED,
    -1, -1, false
  }
};

void refreshColors(PlayerHW &pl);
void saveColors(PlayerHW &pl);
void aiming(PlayerHW &pl);
void blinkIndicatorR(PlayerHW &pl);
void blinkIndicatorG(PlayerHW &pl);
void hitLightUp(PlayerHW &pl, int r, int c);
void missLightUp(PlayerHW &pl, int r, int c);
bool commitShot(PlayerHW &pl);
void preaim(PlayerHW &pl);

int y = 0;

// User inputs
int preInputRow = -1;
int preInputCol = -1;
char displayRow;
int displayCol;

bool aimingActive = false;
unsigned long lastAimUpdate = 0;
int aimStep = 0;
int aimMax = 0;
int aimRow = 0;
int aimCol = 0;
int potVal1 = 0;
int potVal2 = 0;

void setup() {
  #if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
  #endif

  Serial.begin(115200);
  delay(300);

  // ===== ONLY AUDIO CHANGE (startup sound) =====
  if (audioBegin(true)) {
    delay(200);
    audioPlayWav("/audio/pipes.wav");
  }

  for (int p = 0; p < 2; p++) {
    players[p].strip1.begin();
    players[p].strip2.begin();
    for (int i = 0; i < LED_COUNT; i++) {
      players[p].strip1.setPixelColor(i, 0, 0, 255);
    }
    for (int i = 0; i < LED_COUNT; i++) {
      players[p].strip2.setPixelColor(i, 5, 75, 5);
    }
    players[p].strip1.show();
    players[p].strip2.show();
    players[p].strip1.setBrightness(255);
    players[p].strip2.setBrightness(255);
    saveColors(players[p]);

    pinMode(players[p].greenBtn, INPUT_PULLUP);
    pinMode(players[p].redBtn, INPUT_PULLUP);
    pinMode(players[p].greenLED, OUTPUT);
    pinMode(players[p].redLED, OUTPUT);

    digitalWrite(players[p].greenLED, LOW);
    digitalWrite(players[p].redLED, LOW);
  }

  randomSeed(analogRead(A0));
  initrandomMatrix(boards[0]);
  initrandomMatrix(boards[1]);
}

void loop() {

  PlayerHW &pl = players[activePlayer];
  pl.inputRow = getPosition(pl.potRowPin);
  pl.inputCol = getPosition(pl.potColPin);

  bool green = buttonPressed(pl.greenBtn);
  bool red   = buttonPressed(pl.redBtn);

  switch (gameState) {

    case WAITING_FOR_AIM:
      blinkIndicatorG(pl);
      if (green) {
        gameState = WAITING_FOR_CONFIRM;
        digitalWrite(pl.greenLED, HIGH);
      }
      if(pl.inputRow != preInputRow || pl.inputCol != preInputCol){
        refreshColors(pl);
        pl.strip1.show();
        pl.strip2.show();
      }
      preaim(pl);
      break;

    case WAITING_FOR_CONFIRM:
      aiming(pl);
      blinkIndicatorR(pl);
      if (red) {
        bool hit = commitShot(pl);
        aimingActive = false;
        refreshColors(pl);
        pl.strip1.show();
        pl.strip2.show();

        if (!hit && gameState != GAME_OVER) {
          endTurn();              // miss → switch player
          digitalWrite(pl.redLED, LOW);
          digitalWrite(pl.greenLED, LOW);
        } else {
          gameState = WAITING_FOR_AIM;  // hit → same player aims again
          digitalWrite(pl.redLED, LOW);
          digitalWrite(pl.greenLED, LOW);
        }
      }
      if(pl.inputRow != preInputRow || pl.inputCol != preInputCol){
        gameState = WAITING_FOR_AIM;
        aimingActive = false;
        refreshColors(pl);
        pl.strip1.show();
        pl.strip2.show();
      }
      break;

    case GAME_OVER:
      winSequence();
      while (true) delay(1000);
  }
  preInputRow = pl.inputRow;
  preInputCol = pl.inputCol;
}

void endTurn() {
  aimingActive = false;
  players[activePlayer].inputRow = -1;
  players[activePlayer].inputCol = -1;
  activePlayer = otherPlayer(activePlayer);
  gameState = WAITING_FOR_AIM;
}

// --- Matrix Initialization --- //
void initrandomMatrix(Board &b) {
  memset(b.ships, 0, sizeof(b.ships));
  memset(b.found, 0, sizeof(b.found));
  b.remaining = 0;

  struct Block { int len; };
  Block blocks[] = {{5}, {4}, {3}, {3}, {2}};

  for (auto &blk : blocks) {
    bool placed = false;
    while (!placed) {
      int len = blk.len;
      bool horiz = random(2);
      int line = random(10);
      int start = random(0, 10 - len);

      bool ok = true;
      for (int i = 0; i < len; i++) {
        int r = horiz ? line : start + i;
        int c = horiz ? start + i : line;
        if (b.ships[r][c]) { ok = false; break; }
      }
      if (!ok) continue;

      for (int i = 0; i < len; i++) {
        int r = horiz ? line : start + i;
        int c = horiz ? start + i : line;
        b.ships[r][c] = 1;
      }
      b.remaining += len;
      placed = true;
    }
  }
}

void saveColors(PlayerHW &pl){
  for ( int i = 0; i < LED_COUNT; i++){
    pl.previousColors1[i] = pl.strip1.getPixelColor(i);
  }
  for ( int i = 0; i < LED_COUNT; i++){
    pl.previousColors2[i] = pl.strip2.getPixelColor(i);
  }
}

void refreshColors(PlayerHW &pl){
  for ( int i = 0; i < LED_COUNT; i++){
    pl.strip1.setPixelColor(i, pl.previousColors1[i]);
  }
  for ( int i = 0; i < LED_COUNT; i++){
    pl.strip2.setPixelColor(i, pl.previousColors2[i]);
  }
}

void preaim(PlayerHW &pl){
  int refRow = 0;
  int refCol = 0;
  pl.strip2.setPixelColor(indexConvert(pl.inputRow, refCol), 255, 255, 0);
  pl.strip2.setPixelColor(indexConvert(refRow, pl.inputCol), 255, 255, 0);
  pl.strip2.show();
}

// --- Aiming --- //
void aiming(PlayerHW &pl){
  if (!aimingActive) {
    // Start a new aiming animation
    aimingActive = true;
    aimRow = pl.inputRow;
    aimCol = pl.inputCol;
    aimStep = 0;
    aimMax = max(max(9 - aimCol, aimCol), max(9 - aimRow, aimRow));
    lastAimUpdate = millis();
    refreshColors(pl);
    pl.strip1.show();
    pl.strip2.show();
    return;
  }

  // Update animation step (every 50 ms)
  if (millis() - lastAimUpdate >= 50) {
    lastAimUpdate = millis();

    refreshColors(pl); // restore base colors before drawing current step
    pl.strip2.setPixelColor(indexConvert(pl.inputRow, pl.inputCol), 255, 255, 0);

    int i = aimMax - aimStep;
    if (i <= (9 - aimCol)) pl.strip2.setPixelColor(indexConvert(aimRow, aimCol + i), 255, 255, 0);
    if (i <= aimCol)       pl.strip2.setPixelColor(indexConvert(aimRow, aimCol - i), 255, 255, 0);
    if (i <= aimRow)       pl.strip2.setPixelColor(indexConvert(aimRow - i, aimCol), 255, 255, 0);
    if (i <= (9 - aimRow)) pl.strip2.setPixelColor(indexConvert(aimRow + i, aimCol), 255, 255, 0);

    pl.strip2.show();

    aimStep++;

    // Once done, highlight target and stop animation
    if (aimStep > aimMax) {
      refreshColors(pl);
      //pl.strip.setPixelColor(indexConvert(aimRow, aimCol), 255, 255, 0);
      pl.strip2.show();
      aimingActive = false;
    }
  }
}

// --- Red Indicator LED blink --- //
void blinkIndicatorR(PlayerHW &pl) {
  static unsigned long rPrevBlink = 0;
  static bool rStateBlink = false;
  if (millis() - rPrevBlink > 100) {
    rPrevBlink = millis();
    rStateBlink = !rStateBlink;
    digitalWrite(pl.redLED, rStateBlink);
  }
}

// --- Green Indicator LED blink --- //
void blinkIndicatorG(PlayerHW &pl) {
  static unsigned long gPrevBlink = 0;
  static bool gStateBlink = false;
  if (millis() - gPrevBlink > 100) {
    gPrevBlink = millis();
    gStateBlink = !gStateBlink;
    digitalWrite(pl.greenLED, gStateBlink);
  }
}

// --- Guess evaluation --- //
bool commitShot(PlayerHW &pl) {

  if (pl.inputRow < 0 || pl.inputRow > 9 || pl.inputCol < 0 || pl.inputCol > 9)
    return false;

  Board &enemy = boards[otherPlayer(activePlayer)];

  if (enemy.found[pl.inputRow][pl.inputCol])
    return false;

  refreshColors(pl);

  if (enemy.ships[pl.inputRow][pl.inputCol]) {
    enemy.found[pl.inputRow][pl.inputCol] = true;
    enemy.remaining--;
    hitLightUp(pl, pl.inputRow, pl.inputCol);
    saveColors(pl);

    if (enemy.remaining == 0) {
      gameState = GAME_OVER;
    }

    return true;   // HIT
  }

  missLightUp(pl, pl.inputRow, pl.inputCol);
  saveColors(pl);
  return false;    // MISS
}

// 10×10 Matrix helpers
void hitLightUp(PlayerHW &pl, int r, int c) {
  pl.strip1.setPixelColor(indexConvert(r, c), 255, 0, 0);
  pl.strip2.setPixelColor(indexConvert(r, c), 255, 0, 0);
  pl.strip1.show();
  pl.strip2.show();
}

void missLightUp(PlayerHW &pl, int r, int c) {
  pl.strip1.setPixelColor(indexConvert(r, c), 127, 127, 127);
  pl.strip2.setPixelColor(indexConvert(r, c), 127, 127, 127);
  pl.strip1.show();
  pl.strip2.show();
}

int indexConvert(int r, int c){
  int x = 0;
  if(r % 2 == 0){
    x = ((r*10) + c);
  } else {
    x = ((r*10) + (9-c));
  }
  return x;
}

int getPosition(int positionPin){
  // Read the analog value (0-4095 on ESP32 ADC)
  int positionValue = 0;
  int sensorValue = analogRead(positionPin);

  // Map the value to a specific position (adjust ranges based on your resistor values)
  if (sensorValue < 210) positionValue = 9;
  else if (sensorValue < 635) positionValue = 8;
  else if (sensorValue < 1070) positionValue = 7;
  else if (sensorValue < 1510) positionValue = 6;
  else if (sensorValue < 1960) positionValue = 5;
  else if (sensorValue < 2400) positionValue = 4;
  else if (sensorValue < 2860) positionValue = 3;
  else if (sensorValue < 3380) positionValue = 2;
  else if (sensorValue < 3880) positionValue = 1;
  else if (sensorValue > 3880) positionValue = 0;

  return positionValue;
}

bool buttonPressed(int pin) {
  static unsigned long lastTime[100];
  static bool lastState[100];

  bool reading = !digitalRead(pin);
  unsigned long now = millis();

  if (reading != lastState[pin] && now - lastTime[pin] > 100) {
    lastTime[pin] = now;
    lastState[pin] = reading;
    return reading;
  }
  return false;
}

// --- Win sequence --- //
void winSequence() {
  Serial.println("All targets found! You win!");
}
