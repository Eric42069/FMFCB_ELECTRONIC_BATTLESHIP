#include <Wire.h>
#include <Adafruit_MCP23X17.h>

Adafruit_MCP23X17 mcp[8];
#define mcpBaseAddress 0x20

#define I2C_SDA 11
#define I2C_SCL 10 //SCK

byte shipPositionsArray[10][10] = {
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0}
};

const byte gpioPinArray[10][10] = {
  {0,1,2,3,4,5,6,8,9,10},
  {11,12,13,14,0,1,2,3,4,5},
  {6,8,9,10,11,12,13,14,0,1},
  {2,3,4,5,6,8,9,10,11,12},
  {13,14,0,1,2,3,4,5,6,8},
  {9,10,11,12,13,14,0,1,2,3},
  {4,5,6,8,9,10,11,12,13,14},
  {0,1,2,3,4,5,6,8,9,10},
  {11,12,13,14,0,1,2,3,4,5},
  {6,8,9,10,11,12,13,14,0,1}
};

const byte gpioDeviceArray[10][10]{
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,2,2},
  {2,2,2,2,2,2,2,2,2,2},
  {2,2,3,3,3,3,3,3,3,3},
  {3,3,3,3,3,3,4,4,4,4},
  {4,4,4,4,4,4,4,4,4,4},
  {5,5,5,5,5,5,5,5,5,5},
  {5,5,5,5,6,6,6,6,6,6},
  {6,6,6,6,6,6,6,6,7,7},
};

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL); // Set the I2C pins

  for(byte i; i < 8; i++){
    byte address = mcpBaseAddress + i;

    if(!mcp[i].begin_I2C(address)){
      Serial.print("Failed to initialize MCP at 0x");
      Serial.println(address, HEX);
      while(1);
    }
  }

  Wire.setClock(400000); //Sets the frequency of the I2C interface

  initilizeGPIOPins();
  detectShipPositions();
}

void loop(){
  Serial.println("Entered main loop");
  printShipPositions();
  Serial.println();
  delay(5000);
}

//Functions
void detectShipPositions(){
  byte numberOfShips = 0;
  byte gpioPinActive = 0; //This is the pin that is set LOW.
  byte gpioPinShort = 0;
  
  while(numberOfShips < 5){
    for(byte row = 0; row < 10; row++){
      for(byte column = 0; column < 10; column++){
        gpioPinActive = gpioPinArray[row][column];
        mcp[gpioDeviceArray[row][column]].pinMode(gpioPinActive, OUTPUT);
        mcp[gpioDeviceArray[row][column]].digitalWrite(gpioPinActive, LOW);
        for(byte i = 1; i < 5 && (column + i) < 10; i++){
          mcp[gpioDeviceArray[row][column + i]].pinMode(gpioPinArray[row][column + i], INPUT_PULLUP);
          gpioPinShort = mcp[gpioDeviceArray[row][column + i]].digitalRead(gpioPinArray[row][column + i]);
          if(gpioPinShort == 0 && shipPositionsArray[row][column + i] != 1){
            numberOfShips++;
            for(byte x = column; x <= (column + i); x++){
              shipPositionsArray[row][x] = 1;
            }
          }          
        }
        for(byte i = 1; i < 5 && (row + i) < 10; i++){
          mcp[gpioDeviceArray[row + i][column]].pinMode(gpioPinArray[row + i][column], INPUT_PULLUP);
          gpioPinShort = mcp[gpioDeviceArray[row + i][column]].digitalRead(gpioPinArray[row + i][column]);
          if(gpioPinShort == 0 && shipPositionsArray[row + i][column] != 1){
            numberOfShips++;
            for(byte y = row; y <= (row + i); y++){
              shipPositionsArray[y][column] = 1;
            }
          }
        }
        mcp[gpioDeviceArray[row][column]].digitalWrite(gpioPinActive, HIGH);
      }
    }
  }
}

void printShipPositions(){
  for(byte y = 0; y < 10; y++){
    for(byte x = 0; x < 10; x++){
      Serial.print(shipPositionsArray[y][x]);
      Serial.print(", ");
      if(x == 9){
        Serial.println();
      }
    }
  }
}

void initilizeGPIOPins(){
  byte gpioPin = 0;
  byte gpioDevice = 0;
  delay(2000);
  for(byte y = 0; y < 10; y++){
    for(byte x = 0; x < 10; x++){
      mcp[gpioDevice].pinMode(gpioPinArray[y][x], INPUT_PULLUP);
      gpioPin = gpioPinArray[y][x];
      Serial.print("Initialized: ");
      Serial.print(gpioPin);
      Serial.print(" on chip: ");
      Serial.println(gpioDevice);
      if(gpioPin == 14 && gpioDevice < 7){
        gpioDevice++;
      }
    }
  }
}
