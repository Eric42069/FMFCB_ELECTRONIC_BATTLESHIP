#include <Arduino.h>
#include <Keypad.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

// Which pin on the Arduino is connected to the NeoPixels?
// On a Trinket or Gemma we suggest changing this to 1:
#define LED_PIN    37

// How many NeoPixels are attached to the Arduino?
#define LED_COUNT 100

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Indicator LED and button
const int IND_LED_PIN = 35;
const int BUTTON_PIN = 36;

// Keypad setup
const byte ROWS = 4, COLS = 3;
char hexaKeys[ROWS][COLS] = {
  { '1','2','3' },
  { '4','5','6' },
  { '7','8','9' },
  { '*','0','#' }
};
byte rowPins[ROWS] = {41, 40, 39, 38};
byte colPins[COLS] = {1, 2, 42};
Keypad keypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// Reference matrix and tracking
int refMat[10][10];
bool found[10][10];
int totalOnes = 0;
int foundCount = 0;
int y = 0;
uint32_t previousColors[LED_COUNT];

// User inputs
int inputRow = -1;  // 0–7
int inputCol = -1;
char displayRow;
int displayCol;

bool aimingActive = false;
unsigned long lastAimUpdate = 0;
int aimStep = 0;
int aimMax = 0;
int aimRow = 0;
int aimCol = 0;


void setup() {
  #if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif
  // END of Trinket-specific code.
  int i = 0;
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  while(i < LED_COUNT){
    strip.setPixelColor(i, 0, 0, 255);
    i++;
  }
  strip.show();            // Turn OFF all pixels ASAP

  Serial.begin(115200);

  pinMode(IND_LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  initRefMatrix();
  saveColors();
}

void loop() {
  char key = keypad.getKey();
  if (key) {
    if (key == '*' or key == '#') {
      inputRow = inputCol = -1;
      Serial.println(" ");
      digitalWrite(IND_LED_PIN, LOW);
      aimingActive = false;
      refreshColors();
      strip.show();
    } else {
      int val = key - '0';
      if (val == 0){
        val = 10;
      }
      if (val >= 1 && val <= 10) {
        if (inputRow < 0) {
          inputRow = val - 1;
          displayRow = (val + 64);
          Serial.print(displayRow);
        } else if (inputCol < 0) {
          inputCol = val - 1;
          displayCol = val;
          Serial.println(displayCol);
        }
      }
    }
  }
    if (inputRow >= 0 && inputCol >= 0) {
      blinkIndicator();
      aiming();
      if (digitalRead(BUTTON_PIN) == LOW) {
        digitalWrite(IND_LED_PIN, LOW);
        refreshColors();
        checkGuess();
        aimingActive = false;
        inputRow = inputCol = -1;
      }
    }
    refreshColors();
  

  if (foundCount >= totalOnes) {
    winSequence();
    while (true) delay(1000);
  }
}

// --- Matrix Initialization --- //
void initRefMatrix() {
  memset(refMat, 0, sizeof(refMat));
  memset(found, 0, sizeof(found));

  struct Block { int len; };
  Block blocks[] = {{5}, {4}, {3}, {3}, {2}};
  randomSeed(analogRead(A0));

  for (auto &b : blocks) {
    bool placed = false;
    while (!placed) {
      int len = b.len;
      bool horiz = random(2);
      int line = random(8);
      int start = random(0, 9 - len);

      bool ok = true;
      for (int i = 0; i < len; i++) {
        int r = horiz ? line : start + i;
        int c = horiz ? start + i : line;
        if (refMat[r][c] == 1) { ok = false; break; }
      }
      if (!ok) continue;

      for (int i = 0; i < len; i++) {
        int r = horiz ? line : start + i;
        int c = horiz ? start + i : line;
        refMat[r][c] = 1;
      }
      totalOnes += len;
      placed = true;
    }
  }
}

void saveColors(){
  for ( int i = 0; i < LED_COUNT; i++){
    previousColors[i] = strip.getPixelColor(i);
  }
}

void refreshColors(){
    for ( int i = 0; i < LED_COUNT; i++){
    strip.setPixelColor(i, previousColors[i]);
  }
}


// --- Aiming --- //
void aiming() {
  if (!aimingActive) {
    // Start a new aiming animation
    aimingActive = true;
    aimRow = inputRow;
    aimCol = inputCol;
    aimStep = 0;
    aimMax = max(max(9 - aimCol, aimCol), max(9 - aimRow, aimRow));
    lastAimUpdate = millis();
    saveColors();
    return;
  }

  // Update animation step (every 50 ms)
  if (millis() - lastAimUpdate >= 50) {
    lastAimUpdate = millis();

    refreshColors(); // restore base colors before drawing current step

    int i = aimMax - aimStep;
    if (i <= (9 - aimCol)) strip.setPixelColor(indexConvert(aimRow, aimCol + i), 255, 255, 0);
    if (i <= aimCol)       strip.setPixelColor(indexConvert(aimRow, aimCol - i), 255, 255, 0);
    if (i <= aimRow)       strip.setPixelColor(indexConvert(aimRow - i, aimCol), 255, 255, 0);
    if (i <= (9 - aimRow)) strip.setPixelColor(indexConvert(aimRow + i, aimCol), 255, 255, 0);

    strip.show();

    aimStep++;

    // Once done, highlight target and stop animation
    if (aimStep > aimMax) {
      refreshColors();
      strip.setPixelColor(indexConvert(aimRow, aimCol), 255, 255, 0);
      strip.show();
      aimingActive = false;
    }
  }
}


// --- Indicator LED blink --- //
void blinkIndicator() {
  static unsigned long prevBlink = 0;
  static bool stateBlink = false;
  if (millis() - prevBlink > 100) {
    prevBlink = millis();
    stateBlink = !stateBlink;
    digitalWrite(IND_LED_PIN, stateBlink);
  }
}

// --- Guess evaluation --- //
void checkGuess() {
  int r = inputRow, c = inputCol;
  if (found[r][c]) {
    hitLightUp(r, c);
    Serial.println("Hit!");
  } else {
  if (refMat[r][c] == 1) {
    found[r][c] = true;
    foundCount++;
    hitLightUp(r, c);
    saveColors();
    Serial.println("Hit!");
  } else {
    missLightUp(r, c);
    saveColors();
    Serial.println("Miss!");
  }
  }
}

// 8×8 Matrix helpers
void hitLightUp(int r, int c) {
  strip.setPixelColor(indexConvert(r, c), 255, 0, 0);
  strip.show();
}

void missLightUp(int r, int c) {
  strip.setPixelColor(indexConvert(r, c), 127, 127, 127);
  strip.show();
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

// --- Win sequence --- //
void winSequence() {
  Serial.println("All targets found! You win!");
}



