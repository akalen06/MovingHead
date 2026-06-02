
//Working DMX receive code
#include <Arduino.h>
#include <dmxInput.h>

DmxInput dmx;

const uint dmxPin = 1;          // GP1 = RX_CH0 op Waveshare kanaal 0
const uint startChannel = 1;    // DMX adres 1
const uint numChannels = 10;

uint8_t buffer[numChannels];

void setup() {
  Serial.begin(250000);
  delay(2000);

  dmx.begin(dmxPin, startChannel, numChannels);
  Serial.println("DMX receiver gestart");
}

void loop() {
  dmx.read(buffer);

  Serial.print("CH1="); Serial.print(buffer[1]);
  Serial.print(" CH2="); Serial.print(buffer[2]);
  Serial.print(" CH3="); Serial.print(buffer[3]);
  Serial.print(" CH4="); Serial.print(buffer[4]);
  Serial.print(" CH5="); Serial.print(buffer[5]);
  Serial.print(" CH6="); Serial.print(buffer[6]);
  Serial.print(" CH7="); Serial.print(buffer[7]);
  Serial.print(" CH8="); Serial.print(buffer[8]);
  Serial.print(" CH9="); Serial.print(buffer[9]);
  Serial.print("\n");
  
}


/*
//This code contains a working LED driver with the FastLED lib

#include <Arduino.h>
#include <FastLED.h>

#define LED_PIN 2
#define NUM_SQUARES 9

const uint16_t ledsPerSquare[NUM_SQUARES] = {
  33, 32, 33,
  33, 32, 32,
  30, 30, 30
};

#define NUM_LEDS 324

CRGB leds[NUM_LEDS];

void setSquareColor(int square, CRGB color) {
  if (square < 0 || square >= NUM_SQUARES) return;

  int start = 0;
  for (int i = 0; i < square; i++) {
    start += ledsPerSquare[i];
  }

  int end = start + ledsPerSquare[square];

  for (int i = start; i < end; i++) {
    leds[i] = color;
  }
}

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(100);
  FastLED.clear();
  FastLED.show();
}

void loop() {
  FastLED.clear();

  setSquareColor(0, CRGB::Red);
  setSquareColor(1, CRGB::Green);
  setSquareColor(2, CRGB::Blue);
  setSquareColor(3, CRGB::Yellow);
  setSquareColor(4, CRGB::Purple);
  setSquareColor(5, CRGB::Cyan);
  setSquareColor(6, CRGB::Orange);
  setSquareColor(7, CRGB::White);
  setSquareColor(8, CRGB::Pink);

  FastLED.show();
  delay(500);
}
*/


//This code aims to combine
/*
#define LED_PIN 2
#define NUM_LEDS 324
#define NUM_SQUARES 9
#define LEDS_PER_SQUARE 36

DmxInput dmx;
const uint dmxPin = 1;        // GP1 = RX_CH0
const uint startChannel = 1;  // DMX adres 1
const uint numChannels = 9;   // 9 vakken

uint8_t buffer[numChannels];
CRGB leds[NUM_LEDS];

CRGB dmxValueToColor(uint8_t value) {
  if (value <= 10) return CRGB::White;
  else if (value <= 21) return CRGB::Red;
  else if (value <= 32) return CRGB::Orange;
  else if (value <= 43) return CRGB::Yellow;
  else if (value <= 54) return CRGB::Green;
  else if (value <= 65) return CRGB::Blue;
  else if (value <= 76) return CRGB::Cyan;
  else if (value <= 87) return CRGB::Purple;
  else return CRGB::Black;
}

void setSquareColor(int square, CRGB color) {
  int start = square * LEDS_PER_SQUARE;
  int end = start + LEDS_PER_SQUARE;
  for (int i = start; i < end; i++) {
    leds[i] = color;
  }
}
*/

/*
//Combined code #2
#include <Arduino.h>
#include <dmxInput.h>

DmxInput dmx;

const uint dmxPin = 1;          // GP1 = RX_CH0 op Waveshare kanaal 0
const uint startChannel = 1;    // DMX adres 1
const uint numChannels = 10;

uint8_t buffer[numChannels];

void setup() {
  Serial.begin(250000);
  delay(2000);

  dmx.begin(dmxPin, startChannel, numChannels);
  Serial.println("DMX receiver gestart");
}

void loop() {
  dmx.read(buffer); //Buffer is blocking

  Serial.print("CH1="); Serial.print(buffer[1]);
  Serial.print(" CH2="); Serial.print(buffer[2]);
  Serial.print(" CH3="); Serial.print(buffer[3]);
  Serial.print(" CH4="); Serial.print(buffer[4]);
  Serial.print(" CH5="); Serial.print(buffer[5]);
  Serial.print(" CH6="); Serial.print(buffer[6]);
  Serial.print(" CH7="); Serial.print(buffer[7]);
  Serial.print(" CH8="); Serial.print(buffer[8]);
  Serial.print(" CH9="); Serial.print(buffer[9]);
  Serial.print("\n");
  
}
*/