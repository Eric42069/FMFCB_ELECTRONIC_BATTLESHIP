#include <Arduino.h>
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
const int RED_IND_LED_PIN = 35;
const int GREEN_IND_LED_PIN = 38;
const int RED_BUTTON_PIN = 36;
const int GREEN_BUTTON_PIN = 39;
const int POS_POT1_PIN = 5;
const int POS_POT2_PIN = 6;


// Reference matrix and tracking
int randomMat[10][10];
bool found[10][10];
int totalOnes = 0;
int foundCount = 0;
int y = 0;
uint32_t previousColors[LED_COUNT];

// User inputs
int inputRow = -1;  // 0–7
int inputCol = -1;
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

volatile bool redPressed = false;
volatile bool greenPressed = false;

const unsigned long DEBOUNCE_DELAY = 100; // Debounce time in milliseconds
volatile unsigned long lastRedPressTime = 0;
volatile unsigned long lastGreenPressTime = 0;

void IRAM_ATTR redButtonISR() { // ISR for button press
  unsigned long now = millis();
  if (!greenPressed) return;
  if (now - lastRedPressTime > DEBOUNCE_DELAY) { // Debounce check
    redPressed = true; // Set flag
    lastRedPressTime = now;
  }
}

void IRAM_ATTR greenButtonISR() { // ISR for button press
  unsigned long now = millis();
  if (now - lastGreenPressTime > DEBOUNCE_DELAY) { // Debounce check
    greenPressed = true; // Set flag
    lastGreenPressTime = now;
  }
}

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

  pinMode(RED_IND_LED_PIN, OUTPUT);
  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_IND_LED_PIN, OUTPUT);
  pinMode(GREEN_BUTTON_PIN, INPUT_PULLUP);

  initrandomMatrix();
  saveColors();

  digitalWrite(RED_IND_LED_PIN, LOW);
  digitalWrite(GREEN_IND_LED_PIN, LOW);

  attachInterrupt(digitalPinToInterrupt(RED_BUTTON_PIN), redButtonISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(GREEN_BUTTON_PIN), greenButtonISR, FALLING);

  inputRow = getPosition(POS_POT1_PIN);
  inputCol = getPosition(POS_POT2_PIN);

}

void loop() {
  //digitalWrite(RED_IND_LED_PIN, LOW);
  //refreshColors();
  //strip.show();

  inputRow = getPosition(POS_POT1_PIN);
  inputCol = getPosition(POS_POT2_PIN);

  if(greenPressed){
    //Serial.println("Green Press");
    aiming();
    blinkIndicatorR();
    if(preInputRow != inputRow || preInputCol != inputCol){
      greenPressed = false;
      aimingActive = false;
      Serial.println("Reset");
      digitalWrite(RED_IND_LED_PIN, LOW);
      refreshColors();
      strip.show();
    }
    //Serial.println(inputRow);
    //Serial.println(inputCol);
    digitalWrite(GREEN_IND_LED_PIN, HIGH);

    if(redPressed){
      Serial.println("Red Pressed");
      digitalWrite(RED_IND_LED_PIN, LOW);
      checkGuess();
      refreshColors();
      strip.show();
      aimingActive = false;
      inputRow = inputCol = -1;
      greenPressed = false;
      redPressed = false;
    }
  } else {
    blinkIndicatorG();
    redPressed = false;
  }
  if (foundCount >= totalOnes) {
    winSequence();
    while (true) delay(1000);
  }
  preInputCol = inputCol;
  preInputRow = inputRow;
}

// --- Matrix Initialization --- //
void initrandomMatrix() {
  memset(randomMat, 0, sizeof(randomMat));
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
        if (randomMat[r][c] == 1) { ok = false; break; }
      }
      if (!ok) continue;

      for (int i = 0; i < len; i++) {
        int r = horiz ? line : start + i;
        int c = horiz ? start + i : line;
        randomMat[r][c] = 1;
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
    refreshColors();
    strip.show();
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


// --- Red Indicator LED blink --- //
void blinkIndicatorR() {
  static unsigned long rPrevBlink = 0;
  static bool rStateBlink = false;
  if (millis() - rPrevBlink > 100) {
    rPrevBlink = millis();
    rStateBlink = !rStateBlink;
    digitalWrite(RED_IND_LED_PIN, rStateBlink);
  }
}

// --- Green Indicator LED blink --- //
void blinkIndicatorG() {
  static unsigned long gPrevBlink = 0;
  static bool gStateBlink = false;
  if (millis() - gPrevBlink > 100) {
    gPrevBlink = millis();
    gStateBlink = !gStateBlink;
    digitalWrite(GREEN_IND_LED_PIN, gStateBlink);
  }
}

// --- Guess evaluation --- //
void checkGuess() {
  int r = inputRow, c = inputCol;
  refreshColors();
  strip.show();
  if (found[r][c]) {
    hitLightUp(r, c);
    Serial.println("Hit!");
  } else {
  if (randomMat[r][c] == 1) {
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

// --- Win sequence --- //
void winSequence() {
  Serial.println("All targets found! You win!");
}



