#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#define ADAFRUIT_RMT_CHANNEL_MAX 4
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#ifdef __AVR__
 #include <avr/power.h>
#endif

#define LED_COUNT 100

// Bus one pins
#define I2C_SDA0 16
#define I2C_SCL0 15
// Bus two pins
#define I2C_SDA1 18
#define I2C_SCL1 17

#define EMPTY 0
#define boardSize 10
#define maxShips 5
#define maxShipSize 4
#define mcpBaseAddress 0x20

#define orca    2
#define sub     3
#define AOPS    6
#define frigate 4
#define JSS     5

#define MAX_FIREWORKS 5
#define MAX_PARTICLES 25

#define PANEL_WIDTH 10
#define PANEL_HEIGHT 10

Adafruit_MCP23X17 mcp0[8];
Adafruit_MCP23X17 mcp1[8];

TwoWire I2C_0 = TwoWire(0);
TwoWire I2C_1 = TwoWire(1);

#define Ping_Sound               0
#define Whistle_Sound            1
#define Bloop_Sound              2
#define Hit_Sound                3
#define Miss_Sound               4
#define Sunk_Sound               5
#define Victory_Sound            6
#define Volume_Sound             7
#define Brightness_Sound         8
#define Halifax_Sound            9
#define AOPS_Sound               10
#define Orca_Sound               11
#define JSS_Sound                12
#define Sub_Sound                13
#define Fleet_Destroyed_Sound    14
#define Deploy_Sound             15
#define P1_Deployed_Sound        16
#define P2_Deployed_Sound        17
#define Set_Coordinates_Sound    18
#define Single_Player_Sound      19
#define Whilhelm_Sound           20
#define Coordinates_Locked_Sound 21

// -------- PLAYER 1 PINS --------
#define P1_GREEN_BTN  42
#define P1_RED_BTN    41
#define P1_GREEN_LED  40
#define P1_RED_LED    39
#define P1_POT_ROW     6
#define P1_POT_COL     7
#define P1_LED_PIN1    4
#define P1_LED_PIN2    8

// -------- PLAYER 2 PINS --------
#define P2_GREEN_BTN  38
#define P2_RED_BTN    37
#define P2_GREEN_LED  36
#define P2_RED_LED    35
#define P2_POT_ROW     9
#define P2_POT_COL    10
#define P2_LED_PIN1    1
#define P2_LED_PIN2    2

// ===== Pins (ESP32-S3 DevKitC-1) =====
static const int I2S_BCLK = 12;
static const int I2S_LRC  = 13;
static const int I2S_DOUT = 14;
static const int POT_PIN  =  5;

// ===== I2S state =====
static uint32_t g_rate       = 0;
static bool     i2s_installed = false;

// ===== Permanent ping buffer =====
static uint8_t*  pingBuf        = nullptr;
static uint32_t  pingBufLen     = 0;
static uint32_t  pingSampleRate = 0;

// ===== Temporary per-sound buffer =====
static uint8_t*  tempBuf        = nullptr;
static uint32_t  tempBufLen     = 0;
static uint32_t  tempSampleRate = 0;

TaskHandle_t audioTask;
// TaskHandle_t hitTask;
// TaskHandle_t missTask;
volatile int audioFile = -1;
volatile bool readDone = true;

enum Player    { PLAYER_1 = 0, PLAYER_2 = 1 };
enum GameState { WAITING_FOR_AIM, STALL, WAITING_FOR_CONFIRM, GAME_OVER };

Player    activePlayer = PLAYER_1;
GameState gameState    = WAITING_FOR_AIM;

Player otherPlayer(Player p) { return (p == PLAYER_1) ? PLAYER_2 : PLAYER_1; }

const uint8_t gpioPinArray[boardSize][boardSize] = {
  {0,1,2,3,4,5,6,8,9,10},
  {5,4,3,2,1,0,14,13,12,11},
  {6,8,9,10,11,12,13,14,0,1},
  {12,11,10,9,8,6,5,4,3,2},
  {13,14,0,1,2,3,4,5,6,8},
  {3,2,1,0,14,13,12,11,10,9},
  {4,5,6,8,9,10,11,12,13,14},
  {10,9,8,6,5,4,3,2,1,0},
  {11,12,13,14,0,1,2,3,4,5},
  {1,0,14,13,12,11,10,9,8,6}
};

const uint8_t gpioDeviceArray[boardSize][boardSize] = {
  {0,0,0,0,0,0,0,0,0,0}, //A
  {1,1,1,1,1,1,0,0,0,0}, //B
  {1,1,1,1,1,1,1,1,2,2}, //C
  {2,2,2,2,2,2,2,2,2,2}, //D
  {2,2,3,3,3,3,3,3,3,3}, //E
  {4,4,4,4,3,3,3,3,3,3}, //F
  {4,4,4,4,4,4,4,4,4,4}, //G
  {5,5,5,5,5,5,5,5,5,5}, //H
  {5,5,5,5,6,6,6,6,6,6}, //I
  {7,7,6,6,6,6,6,6,6,6}, //J
};

struct Board {
  int  ships[10][10];
  bool found[10][10];
  int  remaining = 0;
};
Board boards[2];

bool detectShipPositions(Board &b, Adafruit_MCP23X17 mcpDevice[], uint8_t player);
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
  int  inputRow;
  int  inputCol;
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

static const char* kPlaylist[] = {
  "/audio/airport-radar-ping-1582.wav",       // 0  Ping
  "/audio/pipes.wav",                          // 1  Whistle
  "/audio/liquid-bubble-3000.wav",             // 2  Bloop
  "/audio/hit.wav",                            // 3  Hit
  "/audio/miss.wav",                           // 4  Miss
  "/audio/sunk.wav",                           // 5  Sunk
  "/audio/victory.wav",                        // 6  Victory
  "/audio/set_volume.wav",                     // 7  Volume
  "/audio/set_brightness.wav",                 // 8  Brightness
  "/audio/ysa_halifax_class_frigate.wav",      // 9  Halifax
  "/audio/ysa_hary_dewolf_class_A-ops.wav",    // 10 AOPS
  "/audio/ysa_orca_class_patro_vessel.wav",    // 11 Orca
  "/audio/ysa_protector_class_JSS.wav",        // 12 JSS
  "/audio/ysa_victoria_class_subarine.wav",    // 13 Sub
  "/audio/enemy_fleet_destroyed.wav",          // 14 Fleet Destroyed
  "/audio/fleet_commanders_deploy_your_fleet.wav", // 15 Deploy
  "/audio/fleet_one_deployed.wav",             // 16 P1 Deployed
  "/audio/fleet_two_deployed.wav",             // 17 P2 Deployed
  "/audio/set_coordinance.wav",                // 18 Set Coordinates
  "/audio/single_player_mode.wav",             // 19 Single Player
  "/audio/Wilhelm_scream.wav",                 // 20 Wilhelm
  "/audio/corordinance_locked_in_fire_when_ready.wav" // 21 Coordinates Locked
};
static const int kPlaylistLen = 22;

struct WavInfo;
static bool parseWav(File &f, WavInfo &w);

struct WavInfo {
  uint32_t sampleRate    = 0;
  uint16_t numChannels   = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset    = 0;
  uint32_t dataSize      = 0;
};

struct Particle {
  float x;
  float y;
  float vx;
  float vy;
  uint8_t life;
  uint32_t color;
};

struct Firework {
  bool active;
  bool exploded;

  float x;
  float y;
  float vy;
  int explodeY;

  Particle particles[MAX_PARTICLES];
};

Firework fireworks[MAX_FIREWORKS];

// -------- Forward declarations --------
void refreshAimColors(PlayerHW &pl);
void refreshOceanColors(PlayerHW &pl);
void saveColors(PlayerHW &pl);
void aiming(PlayerHW &pl);
void blinkIndicatorR(PlayerHW &pl);
void blinkIndicatorG(PlayerHW &pl);
void hitLightUp(PlayerHW &pl, int r, int c);
void missLightUp(PlayerHW &pl, int r, int c);
bool commitShot(PlayerHW &pl);
void preaim(PlayerHW &pl);
void updateRipple();
void startRipple(PlayerHW &pl, int r, int c);
void updateOcean();
void startOcean();
void stripShow();
void endTurn();
void printShipPositions();
void setVolume();
void setBrightness();
void initilizeGPIOPins();
int  indexConvert(int r, int c);
int  getPosition(int positionPin);
bool buttonPressed(int pin);
static void   setupI2S(uint32_t sampleRate);
static bool   parseWav(File &f, WavInfo &w);
static uint32_t readLE32(File &f);
static uint16_t readLE16(File &f);
void playFromRam(const uint8_t* buf, uint32_t len);
static bool loadFileToRam(const char* path, uint8_t*& outBuf, uint32_t& outLen, uint32_t& outRate);

// User inputs / animation globals
int   preInputRow  = -1;
int   preInputCol  = -1;
bool  aimingActive = false;
unsigned long lastAimUpdate = 0;
int   aimStep = 0, aimMax = 0, aimRow = 0, aimCol = 0;
uint8_t brightness = 0;
float   volume     = 0.75;
volatile bool    stripUpdate  = false;
int     ripplePlayer = -1;
int     sensorValue  = 0;

bool          oceanActive     = false;
unsigned long lastOceanUpdate = 0;
float         oceanTime       = 0.0f;
int           storm           = 0;

bool          rippleActive     = false;
unsigned long lastRippleUpdate = 0;
int           rippleRow = 0, rippleCol = 0;
float         rippleRadius = 0.0f, rippleStrength = 1.0f;
bool hitShot = false;
bool forceOcean = false;
int check = 0;
bool shipSunk = true;
unsigned long gameOverUpdate = false;

// ========================================================
//  TASK CODE
// ========================================================

// void HitTaskCode(void *parameter) {
//   PlayerHW &pl = players[activePlayer];
//   vTaskDelay(3300);
//   hitLightUp(pl, pl.inputRow, pl.inputCol);
//   vTaskDelete(NULL);
// }

// void MissTaskCode(void *parameter) {
//   PlayerHW &pl = players[activePlayer];
//   vTaskDelay(3300);
//   missLightUp(pl, pl.inputRow, pl.inputCol);
//   vTaskDelete(NULL);
// }

void AudioTaskCode(void *parameter) {
  for (;;) {
    if (audioFile >= 0 && audioFile < kPlaylistLen) {
      int toPlay = audioFile;
      audioFile  = -1;
      if (toPlay == Ping_Sound) {
        // Play directly from permanent RAM — no flash read at all
        if (pingBuf && pingBufLen > 0) {
          setupI2S(pingSampleRate);
          playFromRam(pingBuf, pingBufLen);
        }
      } else {
        // Load into temp buffer, play, then free immediately
        if (loadFileToRam(kPlaylist[toPlay], tempBuf, tempBufLen, tempSampleRate)) {
          setupI2S(tempSampleRate);
          playFromRam(tempBuf, tempBufLen);
          free(tempBuf);
          tempBuf    = nullptr;
          tempBufLen = 0;
        }
        if (toPlay == 5) {
          int willhielm = random(68,71);
          Serial.print("Wilhielm: ");
          Serial.println(willhielm);
          if(willhielm == 69){
            if (loadFileToRam(kPlaylist[20], tempBuf, tempBufLen, tempSampleRate)) {
              setupI2S(tempSampleRate);
              playFromRam(tempBuf, tempBufLen);
              free(tempBuf);
              tempBuf    = nullptr;
              tempBufLen = 0;
            }
          }
          if(toPlay == Volume_Sound){
            volume = 1.0f - (analogRead(P1_POT_COL) / 4095.0f);
          }
        }
      }
    }
    vTaskDelay(1);
  }
}

// ========================================================
//  SETUP
// ========================================================

void setup() {
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif

  Serial.begin(115200);

  I2C_0.begin(I2C_SDA0, I2C_SCL0, 400000);
  I2C_1.begin(I2C_SDA1, I2C_SCL1, 400000);

  for (uint8_t i = 0; i < 8; i++) {
    uint8_t address = mcpBaseAddress + i;
    if (!mcp0[i].begin_I2C(address, &I2C_0)) {
      Serial.printf("Failed to initialize MCP0 at 0x%02X\n", address);
      while (1);
    }
  }
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t address = mcpBaseAddress + i;
    if (!mcp1[i].begin_I2C(address, &I2C_1)) {
      Serial.printf("Failed to initialize MCP1 at 0x%02X\n", address);
      while (1);
    }
  }

  for (int p = 0; p < 2; p++) {
    players[p].strip1.begin();
    players[p].strip2.begin();
    for (int i = 0; i < LED_COUNT; i++) players[p].strip1.setPixelColor(i, 0, 0, 255);
    for (int i = 0; i < LED_COUNT; i++) players[p].strip2.setPixelColor(i, 5, 75, 5);
    players[p].strip1.setBrightness(255);
    players[p].strip2.setBrightness(255);
    players[p].strip1.show();
    players[p].strip2.show();

    pinMode(players[p].greenBtn, INPUT_PULLUP);
    pinMode(players[p].redBtn,   INPUT_PULLUP);
    pinMode(players[p].greenLED, OUTPUT);
    pinMode(players[p].redLED,   OUTPUT);
    digitalWrite(players[p].greenLED, LOW);
    digitalWrite(players[p].redLED,   LOW);
  }

  xTaskCreatePinnedToCore(
    AudioTaskCode, "audioTask",
    10000, NULL, 0, &audioTask, 0
  );

  initilizeGPIOPins();
  LittleFS.begin();

  // Preload ping permanently into RAM — this is the only file kept in RAM at all times
  if (loadFileToRam(kPlaylist[Ping_Sound], pingBuf, pingBufLen, pingSampleRate)) {
    Serial.printf("Ping preloaded: %u bytes\n", pingBufLen);
  } else {
    Serial.println("Ping preload failed!");
  }

  randomSeed(analogRead(A0));

  audioFile = Volume_Sound;
  setVolume();

  audioFile = Brightness_Sound;
  setBrightness();

  startOcean();
  audioFile = Deploy_Sound;
  delay(100);

  bool d1 = false;
  bool d2 = false;

  while(!d1 || !d2){
    if(!d1) {
      d1 = detectShipPositions(boards[0], mcp0, 1);
      if(d1) {
        audioFile = P1_Deployed_Sound;
        for (int i = 0; i < LED_COUNT; i++) players[1].strip1.setPixelColor(i, 0, 0, 255);
      }
    }
    if(!d2) {
      d2 = detectShipPositions(boards[1], mcp1, 0);
      if(d2){
        audioFile = P2_Deployed_Sound;
        for (int i = 0; i < LED_COUNT; i++) players[0].strip1.setPixelColor(i, 0, 0, 255);
      }
    }
  }
  
  saveColors(players[0]);
  saveColors(players[1]);

  if (audioFile == P2_Deployed_Sound) {
    delay(2500);
  }

  printShipPositions();
  audioFile = Whistle_Sound;
  digitalWrite(P1_RED_LED,   LOW);
  digitalWrite(P1_GREEN_LED, LOW);

  Serial.println("Starting Game");
  stripUpdate = true;
  delay(3500);
}

// ========================================================
//  LOOP
// ========================================================

void loop() {
  PlayerHW &pl = players[activePlayer];

  if (gameState != STALL) {
    pl.inputRow = getPosition(pl.potRowPin);
    pl.inputCol = getPosition(pl.potColPin);
  }

  bool green = buttonPressed(pl.greenBtn);
  bool red   = buttonPressed(pl.redBtn);

  switch (gameState) {

    case WAITING_FOR_AIM:
      blinkIndicatorG(pl);
      if (green) {
        gameState = WAITING_FOR_CONFIRM;
        digitalWrite(pl.greenLED, HIGH);
      }
      if (pl.inputRow != preInputRow || pl.inputCol != preInputCol) {
        refreshAimColors(pl);
      }
      preaim(pl);
      break;

    case WAITING_FOR_CONFIRM:
      aiming(pl);
      blinkIndicatorR(pl);
      if (red) {
        refreshAimColors(pl);
        pl.strip2.show();
        bool hit = commitShot(pl);
        aimingActive = false;

        if (!hit) {
          digitalWrite(pl.redLED,   LOW);
          digitalWrite(pl.greenLED, LOW);
          gameState = STALL;
        } else if (gameState != GAME_OVER) {
          gameState = STALL;
          digitalWrite(pl.redLED,   LOW);
          digitalWrite(pl.greenLED, LOW);
        }
      }
      if (pl.inputRow != preInputRow || pl.inputCol != preInputCol) {
        gameState    = WAITING_FOR_AIM;
        aimingActive = false;
        refreshAimColors(pl);
      }
      break;

    case STALL:
      if (millis() - lastAimUpdate >= 3350) {
        lastAimUpdate = millis();
        if(hitShot){
          hitLightUp(pl, pl.inputRow, pl.inputCol);
        } else {
          missLightUp(pl, pl.inputRow, pl.inputCol);
        }
        forceOcean = true;
        updateOcean();
        stripShow();
        if (hitShot && shipSunk) {
          switch (check) {
            case orca:    audioFile = Orca_Sound;    break;
            case sub:     audioFile = Sub_Sound;     break;
            case AOPS:    audioFile = AOPS_Sound;    break;
            case frigate: audioFile = Halifax_Sound; break;
            case JSS:     audioFile = JSS_Sound;     break;
          }
        }
        if (boards[0].remaining == 0 || boards[1].remaining == 0) {
          Serial.println("All targets found! You win!");
          gameState = GAME_OVER;
          gameOverUpdate = millis();
        }
      }
      break;

    case GAME_OVER:
      if (millis() - gameOverUpdate >= 4000 && millis() - gameOverUpdate <= 4100) {
        audioFile = Fleet_Destroyed_Sound;
      }
      if (millis() - gameOverUpdate >= 6000 && millis() - gameOverUpdate <= 11000) {
        audioFile = Victory_Sound;
      }
      if (millis() - gameOverUpdate >= 4000) {
        gameOverSkull();
        gameOverFireworks();
      }
      break;
  }

  preInputRow = pl.inputRow;
  preInputCol = pl.inputCol;
  updateOcean();
  updateRipple();

  if (stripUpdate && readDone) {
    stripShow();
    stripUpdate = false;
  }
}

// ========================================================
//  LED HELPERS
// ========================================================

void stripShow() {
  players[0].strip1.show();
  players[0].strip2.show();
  players[1].strip1.show();
  players[1].strip2.show();
}

void saveColors(PlayerHW &pl) {
  for (int i = 0; i < LED_COUNT; i++) pl.previousColors1[i] = pl.strip1.getPixelColor(i);
  for (int i = 0; i < LED_COUNT; i++) pl.previousColors2[i] = pl.strip2.getPixelColor(i);
}

void refreshAimColors(PlayerHW &pl) {
  for (int i = 0; i < LED_COUNT; i++) pl.strip2.setPixelColor(i, pl.previousColors2[i]);
  //stripUpdate = true;
}

void refreshOceanColors(PlayerHW &pl) {
  for (int i = 0; i < LED_COUNT; i++) pl.strip1.setPixelColor(i, pl.previousColors1[i]);
  //stripUpdate = true;
}

void preaim(PlayerHW &pl) {
  if( millis() - lastAimUpdate >= 1000){
  pl.strip2.setPixelColor(indexConvert(pl.inputRow, 0), 255, 255, 0);
  pl.strip2.setPixelColor(indexConvert(0, pl.inputCol), 255, 255, 0);
  stripUpdate = true;
  }
}

void aiming(PlayerHW &pl) {
  if (!aimingActive) {
    aimingActive  = true;
    aimRow        = pl.inputRow;
    aimCol        = pl.inputCol;
    aimStep       = 0;
    aimMax        = max(max(9 - aimCol, aimCol), max(9 - aimRow, aimRow));
    lastAimUpdate = millis();
    refreshAimColors(pl);
    return;
  }

  if (millis() - lastAimUpdate >= 80) {
    lastAimUpdate = millis();
    refreshAimColors(pl);

    pl.strip2.setPixelColor(indexConvert(pl.inputRow, pl.inputCol), 255, 255, 0);

    int i = aimMax - aimStep;
    if (i <= (9 - aimCol)) pl.strip2.setPixelColor(indexConvert(aimRow, aimCol + i), 255, 255, 0);
    if (i <= aimCol)        pl.strip2.setPixelColor(indexConvert(aimRow, aimCol - i), 255, 255, 0);
    if (i <= aimRow)        pl.strip2.setPixelColor(indexConvert(aimRow - i, aimCol), 255, 255, 0);
    if (i <= (9 - aimRow))  pl.strip2.setPixelColor(indexConvert(aimRow + i, aimCol), 255, 255, 0);

    aimStep++;

    if (aimStep > aimMax) {
      refreshAimColors(pl);
      aimingActive = false;
      audioFile    = Ping_Sound;  // plays from RAM — no flash read
    }
    stripUpdate = true;
  }
}

void hitLightUp(PlayerHW &pl, int r, int c) {
  refreshOceanColors(pl);
  pl.strip1.setPixelColor(indexConvert(r, c), 255, 0, 0);
  pl.strip2.setPixelColor(indexConvert(r, c), 255, 0, 0);
  saveColors(pl);
  startRipple(pl, r, c);
  stripUpdate = true;
  gameState   = WAITING_FOR_AIM;
}

void missLightUp(PlayerHW &pl, int r, int c) {
  refreshOceanColors(pl);
  pl.strip1.setPixelColor(indexConvert(r, c), 127, 127, 127);
  pl.strip2.setPixelColor(indexConvert(r, c), 127, 127, 127);
  saveColors(pl);
  startRipple(pl, r, c);
  stripUpdate = true;
  endTurn();
}

int indexConvert(int r, int c) {
  return (r % 2 == 0) ? (r * 10 + c) : (r * 10 + (9 - c));
}

// ========================================================
//  OCEAN ANIMATION
// ========================================================

void startOcean() {
  oceanActive     = true;
  oceanTime       = 0.0f;
  lastOceanUpdate = millis();
}

void updateOcean() {
  if (!oceanActive) return;
  if(forceOcean == true) {
    forceOcean = false;
  } else {
    if (millis() - lastOceanUpdate < 100) return;
  }
  
  lastOceanUpdate = millis();

  refreshOceanColors(players[0]);
  refreshOceanColors(players[1]);

  for (int p = 0; p < 2; p++) {
    for (int r = 0; r < 10; r++) {
      for (int c = 0; c < 10; c++) {
        int      idx  = indexConvert(r, c);
        uint32_t base = players[p].previousColors1[idx];

        uint8_t br = (base >> 16) & 0xFF;
        uint8_t bg = (base >> 8)  & 0xFF;
        uint8_t bb =  base        & 0xFF;

        if (br != 0 || bg != 0 || bb == 0) continue; // only animate blue tiles

        float swell1  = sin(oceanTime * 0.6f + r * 0.7f + c * 0.3f);
        float swell2  = sin(oceanTime * 0.4f + r * 0.2f - c * 0.8f);
        float wave1   = sin(oceanTime * 1.3f + r * 1.2f);
        float wave2   = sin(oceanTime * 1.1f + c * 1.4f);
        float ripple1 = sin(oceanTime * 2.4f + (r + c) * 1.8f);
        float ripple2 = sin(oceanTime * 2.8f + (r - c) * 1.5f);

        float rp1w = (storm == 0) ? 0.03f : 0.06f;
        float rp2w = (storm == 0) ? 0.02f : 0.05f;

        float mix = swell1 * 0.35f + swell2 * 0.25f
                  + wave1  * 0.20f + wave2  * 0.15f
                  + ripple1 * rp1w + ripple2 * rp2w;

        float level = (mix + 1.2f) * 0.45f;
        if (level < 0) level = 0;
        if (level > 1) level = 1;

        int blue = (int)(level * 170) + 40;
        int foam = 0;
        if (level > 0.82f) {
          foam = (int)((level - 0.82f) * 700);
          if (foam > 100) foam = 100;
        }
        foam += random(-5, 6);
        if (foam < 0) foam = 0;

        players[p].strip1.setPixelColor(idx, foam, foam, min(255, blue + foam));
      }
    }
  }

  oceanTime  += 0.25f;
  stripUpdate = true;
}

// ========================================================
//  RIPPLE ANIMATION
// ========================================================

void startRipple(PlayerHW &pl, int r, int c) {
  rippleActive     = true;
  rippleRow        = r;
  rippleCol        = c;
  ripplePlayer     = activePlayer;
  rippleRadius     = 0.0f;
  rippleStrength   = 1.0f;
  lastRippleUpdate = millis();
}

void updateRipple() {
  if (!rippleActive) return;
  if (millis() - lastRippleUpdate < 25) return;
  lastRippleUpdate = millis();

  for (int r = 0; r < 10; r++) {
    for (int c = 0; c < 10; c++) {
      int      idx = indexConvert(r, c);
      uint32_t cur = players[ripplePlayer].strip1.getPixelColor(idx);

      uint8_t cr = (cur >> 16) & 0xFF;
      uint8_t cg = (cur >> 8)  & 0xFF;
      uint8_t cb =  cur        & 0xFF;

      float dist = sqrt((r - rippleRow) * (r - rippleRow) +
                        (c - rippleCol) * (c - rippleCol));
      float diff = abs(dist - rippleRadius);

      if (diff < 0.7f) {
        float fade = (1.0f - diff / 0.7f) * rippleStrength;
        if (fade < 0) fade = 0;
        int glow = (int)(fade * 180);
        cr = min(255, (int)cr + glow / 2);
        cg = min(255, (int)cg + glow / 2);
        cb = min(255, (int)cb + glow);
        players[ripplePlayer].strip1.setPixelColor(idx, cr, cg, cb);
      }
    }
  }

  stripUpdate     = true;
  rippleRadius   += 0.4f;
  rippleStrength *= 0.93f;

  if (rippleStrength < 0.05f || rippleRadius > 15.0f) {
    rippleActive = false;
  }
}

// ========================================================
//  INDICATOR LEDs
// ========================================================

void blinkIndicatorR(PlayerHW &pl) {
  static unsigned long rPrev  = 0;
  static bool          rState = false;
  if (millis() - rPrev > 100) {
    rPrev  = millis();
    rState = !rState;
    digitalWrite(pl.redLED, rState);
  }
}

void blinkIndicatorG(PlayerHW &pl) {
  static unsigned long gPrev  = 0;
  static bool          gState = false;
  if (millis() - gPrev > 100) {
    gPrev  = millis();
    gState = !gState;
    digitalWrite(pl.greenLED, gState);
  }
}

// ========================================================
//  GAME LOGIC
// ========================================================

void endTurn() {
  aimingActive = false;
  players[activePlayer].inputRow = -1;
  players[activePlayer].inputCol = -1;
  activePlayer = otherPlayer(activePlayer);
  gameState    = WAITING_FOR_AIM;
}

bool commitShot(PlayerHW &pl) {
  if (pl.inputRow < 0 || pl.inputRow > 9 || pl.inputCol < 0 || pl.inputCol > 9)
    return false;

  Board &enemy = boards[otherPlayer(activePlayer)];

  if (enemy.found[pl.inputRow][pl.inputCol]) {
    endTurn();
    return false;
  }

  //refreshOceanColors(pl);
  refreshAimColors(pl);

  int cell = enemy.ships[pl.inputRow][pl.inputCol];
  if (cell != 0 && cell != 1) {
    enemy.found[pl.inputRow][pl.inputCol] = true;
    check = cell;
    Serial.printf("check: %d\n", check);
    enemy.ships[pl.inputRow][pl.inputCol] = 1;

    shipSunk = true;
    for (uint8_t y = 0; y < boardSize && shipSunk; y++)
      for (uint8_t x = 0; x < boardSize && shipSunk; x++)
        if (enemy.ships[y][x] == check) shipSunk = false;

    audioFile = shipSunk ? Sunk_Sound : Hit_Sound;
    delay(1);
    hitShot = true;

    enemy.remaining--;
    Serial.println(enemy.remaining);
    return true;
  }
  
  hitShot = false;
  audioFile = Miss_Sound;
  return false;
}

// ========================================================
//  INPUT
// ========================================================

int getPosition(int positionPin) {
  int positionValue   = -1;
  int positionAverage = 0;

  while (positionValue != positionAverage) {
    positionAverage = 0;
    for (int i = 0; i < 5; i++) {
      int sv = analogRead(positionPin);
      int pv;
      if      (sv <  210) pv = 9;
      else if (sv <  635) pv = 8;
      else if (sv < 1070) pv = 7;
      else if (sv < 1510) pv = 6;
      else if (sv < 1960) pv = 5;
      else if (sv < 2400) pv = 4;
      else if (sv < 2860) pv = 3;
      else if (sv < 3380) pv = 2;
      else if (sv < 4050) pv = 1;
      else                pv = 0;
      positionValue    = pv;
      positionAverage += pv;
    }
    positionAverage = (positionAverage % 5 == 0) ? positionAverage / 5 : -1;
  }
  return positionValue;
}

bool buttonPressed(int pin) {
  static unsigned long lastTime[100]  = {};
  static bool          lastState[100] = {};
  bool reading  = !digitalRead(pin);
  unsigned long now = millis();
  if (reading != lastState[pin] && now - lastTime[pin] > 100) {
    lastTime[pin]  = now;
    lastState[pin] = reading;
    return reading;
  }
  return false;
}

// ========================================================
//  VOLUME & BRIGHTNESS
// ========================================================

void setVolume() {
  audioFile = Volume_Sound;
  int volPosition = getPosition(P1_POT_COL);
  unsigned long volumeRepeat = millis();
  while (digitalRead(P1_RED_BTN) == HIGH) {
    blinkIndicatorR(players[0]);
    if(millis() - volumeRepeat >= 5000){
      volume = 0.75;
      audioFile = Volume_Sound;
      volumeRepeat = millis();
    }
    if (getPosition(P1_POT_COL) != volPosition) {
      audioFile   = Bloop_Sound;
      volPosition = getPosition(P1_POT_COL);
      volume = 1.0f - (analogRead(P1_POT_COL) / 4095.0f);
      volumeRepeat = millis();
    }
  }
  volume = 1.0f - (analogRead(P1_POT_COL) / 4095.0f);
  digitalWrite(P1_RED_LED, HIGH);
}

void setBrightness() {
  audioFile = Brightness_Sound;
  const uint8_t levels[10] = {0, 25, 55, 80, 105, 130, 155, 180, 205, 255};
  unsigned long brightnessRepeat = millis();
  while (digitalRead(P1_GREEN_BTN) == HIGH) {
    if(millis() - brightnessRepeat >= 5000){
        audioFile = Brightness_Sound;
        brightnessRepeat = millis();
    }
    blinkIndicatorG(players[0]);
    brightness = levels[getPosition(P1_POT_ROW)];
    for (int p = 0; p < 2; p++) {
      players[p].strip1.clear();
      players[p].strip2.clear();
      for (int i = 0; i < LED_COUNT; i++) players[p].strip1.setPixelColor(i, 0, 0, 255);
      for (int i = 0; i < LED_COUNT; i++) players[p].strip2.setPixelColor(i, 5, 75, 5);
      players[p].strip1.setBrightness(brightness);
      players[p].strip2.setBrightness(brightness);
      players[p].strip1.show();
      players[p].strip2.show();
    }
  }
  digitalWrite(P1_GREEN_LED, HIGH);
}

// ========================================================
//  SHIP DETECTION
// ========================================================
bool detectShipPositions(Board &b, Adafruit_MCP23X17 mcpDevice[], uint8_t player) {
  b.remaining = 0;
  // bool    specialThree  = false;
  bool    finalCheck = false;
  uint8_t check;
  uint8_t orcaCheck = 0;
  uint8_t frigateCheck = 0;
  uint8_t subCheck = 0;
  uint8_t AOPSCheck = 0;
  uint8_t JSSCheck = 0;

  if(b.ships[0][0] == 0) {
    players[player].strip1.setPixelColor(0, 0, 0, 255);
  }

  for (uint8_t row = 0; row < boardSize; row++) {
    for (uint8_t column = 0; column < boardSize; column++) {

      uint8_t gpioPinActive = gpioPinArray[row][column];
      uint8_t activeDevice  = gpioDeviceArray[row][column];

      mcpDevice[activeDevice].pinMode(gpioPinActive, OUTPUT);
      mcpDevice[activeDevice].digitalWrite(gpioPinActive, LOW);

      // Horizontal scan
      for (uint8_t i = 1; i <= maxShipSize && (column + i) < boardSize; i++) {
        uint8_t device = gpioDeviceArray[row][column + i];
        uint8_t pin    = gpioPinArray[row][column + i];
        //mcpDevice[device].pinMode(pin, INPUT_PULLUP);
        if (mcpDevice[device].digitalRead(pin) == LOW && b.ships[row][column + i] == EMPTY) {
          int shipID = (i + 1);
          if (i == 2) {
            uint8_t np = gpioPinArray[row][column + 1];
            uint8_t nd = gpioDeviceArray[row][column + 1];
            //mcpDevice[nd].pinMode(np, INPUT_PULLUP);
            if (mcpDevice[nd].digitalRead(np) == LOW) { 
              shipID = 6; 
            }
          }
          // int shipID = specialThree ? 6 : (i + 1); 
          for (uint8_t x = column; x <= (column + i); x++) {
            b.ships[row][x] = shipID;
            players[player].strip1.setPixelColor(indexConvert(row, x), 255, 0, 0);
          }
          // specialThree = false;
        }
        if(b.ships[row][column + i] == 0) {
            players[player].strip1.setPixelColor(indexConvert(row, column + i), 0, 0, 255);
        }
      }     
      //players[player].strip1.show();

      // Vertical scan
      for (uint8_t i = 1; i <= maxShipSize && (row + i) < boardSize; i++) {
        uint8_t device = gpioDeviceArray[row + i][column];
        uint8_t pin    = gpioPinArray[row + i][column];
        //mcpDevice[device].pinMode(pin, INPUT_PULLUP);
        if (mcpDevice[device].digitalRead(pin) == LOW && b.ships[row + i][column] == EMPTY) {
          int shipID = (i + 1);
          if (i == 2) {
            uint8_t np = gpioPinArray[row + 1][column];
            uint8_t nd = gpioDeviceArray[row + 1][column];
            //mcpDevice[nd].pinMode(np, INPUT_PULLUP);
            if (mcpDevice[nd].digitalRead(np) == LOW) { 
              shipID = 6; 
            }
          }
          for (uint8_t y = row; y <= (row + i); y++) {
            b.ships[y][column] = shipID;
            players[player].strip1.setPixelColor(indexConvert(y, column), 255, 0, 0);
          }
        }
        if(b.ships[row + i][column] == 0) {
          players[player].strip1.setPixelColor(indexConvert(row + i, column), 0, 0, 255);
        }
      }
      mcpDevice[activeDevice].pinMode(gpioPinActive, INPUT_PULLUP);
    }
  }
  players[player].strip1.show();
  if (!finalCheck) {
    finalCheck = true;
    //numberOfShips = 0;
    for (uint8_t y = 0; y < boardSize; y++) {
      for (uint8_t x = 0; x < boardSize; x++) {
        check = b.ships[y][x];
        switch (check){
          case 2:
            orcaCheck++;
            break;
          
          case 3:
            subCheck++;
            break;

          case 6:
            AOPSCheck++;
            break;

          case 4:
            frigateCheck++;
            break;
          
          case 5:
            JSSCheck++;
            break;

          default:
          break;
        }
      }
    }
    if(orcaCheck != 2 || subCheck != 3 || AOPSCheck != 3 || frigateCheck != 4 || JSSCheck != 5){
      finalCheck = false;
      orcaCheck = 0;
      frigateCheck = 0;
      subCheck = 0;
      AOPSCheck = 0;
      JSSCheck = 0;
      for (uint8_t y = 0; y < boardSize; y++) {
        for (uint8_t x = 0; x < boardSize; x++) {
          b.ships[y][x] = 0;
          //players[player].strip1.setPixelColor(indexConvert(y, x), 0, 0, 255);
        }
      }
    }
  }
  //players[player].strip1.show();
  b.remaining = 2;
  return finalCheck;
}

// void detectShipPositions(Board &b, Adafruit_MCP23X17 mcpDevice[], uint8_t player) {
//   b.remaining = 0;
//   uint8_t numberOfShips = 0;
//   bool    specialThree  = false;
//   bool    finalCheck    = false;
//   uint8_t check;
//   uint8_t orcaCheck = 0;
//   uint8_t frigateCheck = 0;
//   uint8_t subCheck = 0;
//   uint8_t AOPSCheck = 0;
//   uint8_t JSSCheck = 0;


//   while (numberOfShips < 5) {
//     printShipPositions();
//     for (uint8_t row = 0; row < boardSize; row++) {
//       for (uint8_t column = 0; column < boardSize; column++) {

//         uint8_t gpioPinActive = gpioPinArray[row][column];
//         uint8_t activeDevice  = gpioDeviceArray[row][column];

//         mcpDevice[activeDevice].pinMode(gpioPinActive, OUTPUT);
//         mcpDevice[activeDevice].digitalWrite(gpioPinActive, LOW);

//         // Horizontal scan
//         for (uint8_t i = 1; i <= maxShipSize && (column + i) < boardSize; i++) {
//           uint8_t device = gpioDeviceArray[row][column + i];
//           uint8_t pin    = gpioPinArray[row][column + i];
//           mcpDevice[device].pinMode(pin, INPUT_PULLUP);
//           if (mcpDevice[device].digitalRead(pin) == LOW && b.ships[row][column + i] == EMPTY) {
//             numberOfShips++;
//             if (i == 2) {
//               uint8_t np = gpioPinArray[row][column + 1];
//               uint8_t nd = gpioDeviceArray[row][column + 1];
//               mcpDevice[nd].pinMode(np, INPUT_PULLUP);
//               if (mcpDevice[nd].digitalRead(np) == LOW) { specialThree = true; numberOfShips--; }
//             }
//             int shipID = specialThree ? 6 : (i + 1);
//             for (uint8_t x = column; x <= (column + i); x++) {
//               players[player].strip1.setPixelColor(indexConvert(row, x), 255, 0, 0);
//               players[player].strip1.show();
//               b.ships[row][x] = shipID;
//             }
//             specialThree = false;
//           }
//         }

//         // Vertical scan
//         for (uint8_t i = 1; i <= maxShipSize && (row + i) < boardSize; i++) {
//           uint8_t device = gpioDeviceArray[row + i][column];
//           uint8_t pin    = gpioPinArray[row + i][column];
//           mcpDevice[device].pinMode(pin, INPUT_PULLUP);
//           if (mcpDevice[device].digitalRead(pin) == LOW && b.ships[row + i][column] == EMPTY) {
//             numberOfShips++;
//             if (i == 2) {
//               uint8_t np = gpioPinArray[row + 1][column];
//               uint8_t nd = gpioDeviceArray[row + 1][column];
//               mcpDevice[nd].pinMode(np, INPUT_PULLUP);
//               if (mcpDevice[nd].digitalRead(np) == LOW) { specialThree = true; numberOfShips--; }
//             }
//             int shipID = specialThree ? 6 : (i + 1);
//             for (uint8_t y = row; y <= (row + i); y++) {
//               players[player].strip1.setPixelColor(indexConvert(y, column), 255, 0, 0);
//               players[player].strip1.show();
//               b.ships[y][column] = shipID;
//             }
//             specialThree = false;
//           }
//         }

//         mcpDevice[activeDevice].pinMode(gpioPinActive, INPUT_PULLUP);
//       }
//     }
//     if (numberOfShips >= 5 && !finalCheck) {
//       finalCheck = true;
//       //numberOfShips = 0;
//       for (uint8_t y = 0; y < boardSize; y++) {
//         for (uint8_t x = 0; x < boardSize; x++) {
//           check = b.ships[y][x];
//           switch (check){
//             case 2:
//               orcaCheck++;
//               break;
            
//             case 3:
//               subCheck++;
//               break;

//             case 6:
//               AOPSCheck++;
//               break;

//             case 4:
//               frigateCheck++;
//               break;
            
//             case 5:
//               JSSCheck++;
//               break;

//             default:
//             break;
//           }
//         }
//       }
//       if(orcaCheck != 2 || subCheck != 3 || AOPSCheck != 3 || frigateCheck != 4 || JSSCheck != 5){
//         finalCheck = false;
//         numberOfShips = 0;
//         orcaCheck = 0;
//         frigateCheck = 0;
//         subCheck = 0;
//         AOPSCheck = 0;
//         JSSCheck = 0;
//         for (uint8_t y = 0; y < boardSize; y++) {
//           for (uint8_t x = 0; x < boardSize; x++) {
//             b.ships[y][x] = 0;
//           }
//         }
//       }
//     }
//   }
//   b.remaining = 2;
// }

void initrandomMatrix(Board &b) {
  memset(b.ships, 0, sizeof(b.ships));
  memset(b.found, 0, sizeof(b.found));
  b.remaining = 0;
  struct Block { int len; };
  Block blocks[] = {{5}, {4}, {3}, {3}, {2}};
  for (auto &blk : blocks) {
    bool placed = false;
    while (!placed) {
      bool horiz = random(2);
      int  line  = random(10);
      int  start = random(0, 10 - blk.len);
      bool ok    = true;
      for (int i = 0; i < blk.len && ok; i++) {
        int r = horiz ? line : start + i;
        int c = horiz ? start + i : line;
        if (b.ships[r][c]) ok = false;
      }
      if (!ok) continue;
      for (int i = 0; i < blk.len; i++) {
        int r = horiz ? line : start + i;
        int c = horiz ? start + i : line;
        b.ships[r][c] = 1;
      }
      b.remaining += blk.len;
      placed = true;
    }
  }
}

void printShipPositions() {
  for (int i = 0; i < 2; i++) {
    Serial.println();
    for (uint8_t y = 0; y < 10; y++) {
      for (uint8_t x = 0; x < 10; x++) {
        Serial.print(boards[i].ships[y][x]);
        Serial.print(", ");
      }
      Serial.println();
    }
  }
}

void initilizeGPIOPins() {
  for (uint8_t i = 0; i < 7; i++)
    for (uint8_t j = 0; j < 14; j++) mcp0[i].pinMode(j, INPUT_PULLUP);
  for (uint8_t i = 0; i < 7; i++)
    for (uint8_t j = 0; j < 14; j++) mcp1[i].pinMode(j, INPUT_PULLUP);
}

// ========================================================
//  AUDIO
// ========================================================

static uint32_t readLE32(File &f) {
  uint8_t b[4];
  if (f.read(b, 4) != 4) return 0;
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t readLE16(File &f) {
  uint8_t b[2];
  if (f.read(b, 2) != 2) return 0;
  return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static bool parseWav(File &f, WavInfo &w) {
  w = WavInfo{};
  f.seek(0);
  uint8_t id[4];
  if (f.read(id, 4) != 4 || memcmp(id, "RIFF", 4) != 0) return false;
  readLE32(f);
  if (f.read(id, 4) != 4 || memcmp(id, "WAVE", 4) != 0) return false;

  bool gotFmt = false, gotData = false;
  while (f.available()) {
    if (f.read(id, 4) != 4) break;
    uint32_t size = readLE32(f);
    if (memcmp(id, "fmt ", 4) == 0) {
      uint16_t audioFormat = readLE16(f);
      w.numChannels        = readLE16(f);
      w.sampleRate         = readLE32(f);
      readLE32(f); readLE16(f);
      w.bitsPerSample      = readLE16(f);
      if (size > 16) f.seek(f.position() + (size - 16));
      if (audioFormat != 1 || w.bitsPerSample != 16 || w.numChannels != 1) return false;
      gotFmt = true;
    } else if (memcmp(id, "data", 4) == 0) {
      w.dataOffset = f.position();
      w.dataSize   = size;
      gotData      = true;
      break;
    } else {
      f.seek(f.position() + size);
    }
    if (size & 1) f.seek(f.position() + 1);
  }
  return gotFmt && gotData;
}

// Load a WAV file's PCM data entirely into a malloc'd buffer.
// Caller is responsible for free()'ing outBuf when done.
static bool loadFileToRam(const char* path, uint8_t*& outBuf, uint32_t& outLen, uint32_t& outRate) {
  readDone = false;
  vTaskDelay(10);
  File f = LittleFS.open(path, "r");
  if (!f) { Serial.printf("Missing: %s\n", path); return false; }

  WavInfo w;
  if (!parseWav(f, w)) {
    Serial.printf("Bad WAV: %s\n", path);
    f.close();
    return false;
  }

  outBuf = (uint8_t*)malloc(w.dataSize);
  if (!outBuf) {
    Serial.printf("malloc failed (%u bytes) for %s\n", w.dataSize, path);
    f.close();
    return false;
  }

  f.seek(w.dataOffset);
  size_t got = f.read(outBuf, w.dataSize);
  readDone = true;
  f.close();

  if (got != w.dataSize) {
    Serial.printf("Short read for %s\n", path);
    free(outBuf);
    outBuf = nullptr;
    return false;
  }

  outLen  = w.dataSize;
  outRate = w.sampleRate;
  return true;
}

static void setupI2S(uint32_t sampleRate) {
  if (i2s_installed && g_rate == sampleRate) return;

  if (i2s_installed) {
    i2s_driver_uninstall(I2S_NUM_0);
    i2s_installed = false;
  }

  g_rate = sampleRate;

  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = sampleRate;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count        = 8;
  cfg.dma_buf_len          = 256;
  cfg.use_apll             = false;
  cfg.tx_desc_auto_clear   = true;

  i2s_pin_config_t pins = {};
  pins.bck_io_num      = I2S_BCLK;
  pins.ws_io_num       = I2S_LRC;
  pins.data_out_num    = I2S_DOUT;
  pins.data_in_num     = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_installed = true;
}

// Play PCM data entirely from a RAM buffer — no flash reads during playback
void playFromRam(const uint8_t* buf, uint32_t len) {
  const uint8_t* p    = buf;
  uint32_t       left = len;
  int16_t        out[512];

  while (left) {
    int n = (left > 512) ? 512 : (int)left;
    left -= n;

    int frames = 0;
    for (int i = 0; i + 1 < n && frames < 256; i += 2) {
      int16_t s = (int16_t)(p[i] | (p[i + 1] << 8));
      int32_t v = (int32_t)(s * volume);
      v = constrain(v, -32768, 32767);
      out[frames * 2]     = (int16_t)v;
      out[frames * 2 + 1] = (int16_t)v;
      frames++;
    }
    p += n;

    size_t written;
    i2s_write(I2S_NUM_0, out, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    taskYIELD();
  }
}

void gameOverSkull() {

  static int16_t offset = -PANEL_WIDTH;
  static uint32_t lastUpdate = 0;
  static bool jawOpen = false;
  static uint8_t jawTimer = 0;

  if (millis() - lastUpdate < 80) return;  // slower scroll (was 60)
  lastUpdate = millis();
  refreshAimColors(players[otherPlayer(activePlayer)]);

// toggle jaw more frequently
  jawTimer++;
  if (jawTimer > 2) {   // was 8
    jawTimer = 0;
    jawOpen = !jawOpen;
  }

  const uint8_t skullTop[6][10] = {
  {0,0,1,1,1,1,1,1,1,0},
  {0,1,1,0,1,1,1,0,1,1},
  {0,1,1,0,2,1,2,0,1,1},
  {0,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,0,1,1,1,1},
  {0,0,1,1,1,1,1,1,1,0}
  };

  const uint8_t jawClosed[4][10] = {
  {0,0,1,0,1,0,1,0,1,0},
  {0,0,0,1,0,1,0,1,0,0},
  {0,0,0,1,1,1,1,1,0,0},
  {0,0,0,0,0,0,0,0,0,0}
  };

  const uint8_t jawOpenData[4][10] = {
  {0,0,1,0,1,0,1,0,1,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,1,0,1,0,1,0,0},
  {0,0,0,1,1,1,1,1,0,0}
  };

    // draw skull top
  for(int y = 0; y < 6; y++){
    for(int x = 0; x < 10; x++){

      if(!skullTop[y][x]) continue;

      int drawX = x + offset;
      if(drawX < 0 || drawX >= PANEL_WIDTH) continue;

      uint32_t color = players[otherPlayer(activePlayer)].strip2.Color(255,255,255);

      if(skullTop[y][x] == 2){
      color = players[otherPlayer(activePlayer)].strip2.Color(255, 0, 0);
      }
      // row = y, column = drawX
      players[otherPlayer(activePlayer)].strip2.setPixelColor(indexConvert(y, drawX), color);
    }
  }

  // draw jaw
  for(int y = 0; y < 4; y++){
    for(int x = 0; x < 10; x++){

      uint8_t val = jawOpen ? jawOpenData[y][x] : jawClosed[y][x];
      if(!val) continue;

      int drawX = x + offset;
      int drawY = y + 6;

      if(drawX < 0 || drawX >= PANEL_WIDTH) continue;

      // row = drawY, column = drawX
      players[otherPlayer(activePlayer)].strip2.setPixelColor(indexConvert(drawY, drawX), players[otherPlayer(activePlayer)].strip2.Color(255,255,255));
    }
  }

  offset++;
  if(offset > PANEL_WIDTH)
    offset = -10;

}

void gameOverFireworks() {

  static uint32_t lastUpdate = 0;
  if(millis() - lastUpdate < 50) return;
  lastUpdate = millis();

  refreshAimColors(players[activePlayer]);

  static uint32_t lastLaunch = 0;

  for(int i=0;i<MAX_FIREWORKS;i++){

    Firework &f = fireworks[i];

    if(!f.active){

      if(millis() - lastLaunch < 600) continue;

      if(random(100) < 4){
        f.active = true;
        f.exploded = false;

        f.x = random(PANEL_WIDTH);
        f.y = PANEL_HEIGHT - 1;
        f.vy = -0.6;
        f.explodeY = random(2, PANEL_HEIGHT / 2);

        lastLaunch = millis();
      }

      continue;
    }

    if(!f.exploded){

      players[activePlayer].strip2.setPixelColor(
        indexConvert((int)f.y,(int)f.x),
        players[activePlayer].strip2.Color(255,180,80)
      );

      f.y += f.vy;

      if(f.y <= f.explodeY){

        f.exploded = true;

        // Choose one palette color for this explosion
        uint8_t colorChoice = random(5);
        uint32_t explodeColor;

        switch(colorChoice){
          case 0: explodeColor = players[activePlayer].strip2.Color(255,0,0); break;   // red
          case 1: explodeColor = players[activePlayer].strip2.Color(0,0,255); break;   // blue
          case 2: explodeColor = players[activePlayer].strip2.Color(255,255,0); break; // yellow
          case 3: explodeColor = players[activePlayer].strip2.Color(255,120,0); break; // orange
          case 4: explodeColor = players[activePlayer].strip2.Color(255,20,147); break;// pink
        }

        for(int p=0;p<MAX_PARTICLES;p++){

          float angle = random(360) * 0.01745;
          float speed = random(10,40) / 40.0;

          f.particles[p] = {
            f.x,
            f.y,
            cos(angle) * speed,
            sin(angle) * speed,
            random(12,20),  // lifespan
            explodeColor
          };
        }
      }

    } else {

      int alive = 0;

      for(int p=0;p<MAX_PARTICLES;p++){

        Particle &pt = f.particles[p];

        if(pt.life == 0) continue;

        alive++;

        pt.x += pt.vx;
        pt.y += pt.vy;

        pt.vy += 0.03; // gravity

        pt.life--;

        if(random(100) < 25) continue; // sparkle flicker

        int px = (int)pt.x;
        int py = (int)pt.y;

        if(px >= 0 && px < PANEL_WIDTH && py >= 0 && py < PANEL_HEIGHT){

          // fade calculation
          uint8_t fade = pt.life * 12;

          uint32_t color = players[activePlayer].strip2.Color(
            ((pt.color >> 16) & 255) * fade / 255,
            ((pt.color >> 8) & 255) * fade / 255,
            ((pt.color & 255) * fade / 255)
          );

          uint32_t current = players[activePlayer].strip2.getPixelColor(indexConvert(py, px));

          uint8_t g = (current >> 8) & 0xFF;
          uint8_t r = (current >> 16) & 0xFF;
          uint8_t b = current & 0xFF;

          // don't overwrite green background
          if(!(g > 0 && r == 0 && b == 0)){
            players[activePlayer].strip2.setPixelColor(
              indexConvert(py, px),
              color
            );
          }
        }
      }

      if(alive == 0)
        f.active = false;
    }
  }
}
