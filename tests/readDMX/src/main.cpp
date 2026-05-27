/*
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
*/

#include <Arduino.h>
#include <FastLED.h>

#define NUM_LEDS 1
CRGB leds[NUM_LEDS];

void setup() { 
  FastLED.addLeds<WS2812, 2>(leds, NUM_LEDS); 
}

void loop() {
  leds[0] = CRGB::Red; FastLED.show(); delay(500);
  leds[1] = CRGB::Blue; FastLED.show(); delay(500);
}