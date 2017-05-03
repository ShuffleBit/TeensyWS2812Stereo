#include <Arduino.h>

#include <FastLED.h>
#include <MSGEQ7.h>

FASTLED_USING_NAMESPACE

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

// Light and sound (frequency and position w/in stereo field) reactive WS2811
// Patrick Ward, 2017:

// Hardware:
// Paul Stoffgren's excellent Teensy 3.1 - see pjrc.com. ARM Cortex M4
// WS2812B strip on pin 6 (w/ 4uf cap VDD-Gnd)
// Cadmium photocell and 10-turn 201 pot on A0 for ambient light
// 2 MSGEQ7 chips to sample frequency of stereo audio, 22K on inputs

// TODO:
// Op amp on inputs
// ESP8266 control (or move to ESP32)

//////////// Defs
// WS2812B:
#define AMBIENT       A0
#define WSDATA_PIN    6 ///////////////

// MSGEQ7:
#define DEAF          255 // Max analogRead value on EQLEFT/EQRight
#define STROBE        3  //////////////
#define RESET         4  //////////////
#define EQLEFT        A1 //////////////
#define EQRIGHT       A2 //////////////
#define THRESHOLD     35 // noise floor (MSGEQ7 is very noisy)
#define SMOOTH        1  // N sample smoothing

// FastLED:
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS            9 //////////////////
#define LEDS_PER_CHANNEL    5 /////////// center LED - gotta pick yer poison on even NUM_LEDS
#define BRIGHTNESS          20
#define FRAMES_PER_SEC      60
#define FADERATE            180

#define MAXIDLE             9000
#define DEBUG               false

CRGB leds[NUM_LEDS];  // led array
CMSGEQ7<0, RESET, STROBE, EQLEFT, EQRIGHT> MSGEQ7;

void setup() {
  if (DEBUG) Serial.begin(38400);
  delay(2000); // 3 second delay for power supply settling

  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE, WSDATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  // set initial brightness
  FastLED.setBrightness(BRIGHTNESS);
  MSGEQ7.begin();
  delay(1000);
  MSGEQ7.reset();
}

///////////// Globs
uint8_t gHue = 0;     // rotating "base color" used by many of the patterns
uint8_t gSLBin[SMOOTH][7], gSRBin[SMOOTH][7]; // left/right eq gSample data
uint8_t gLBin[7], gRBin[7]; //left / right data, averaged over SMOOTH gSamples
int gSample = 0;       // which gSample
int gBand;             // the current gBand being processed
uint32_t gIdle = 10000; // how many MSGEQ7 reads were zilch in the 1K-4K gBands? Begin with vizzies
float gScale = (float)LEDS_PER_CHANNEL / DEAF; // math *might* be expensive
int gPeak = DEAF; // track gPeak volume

void loop()
{
  // if no sound detected for MAXIDLE iterations, make a rainbow
  if (gIdle >= MAXIDLE) rainbow();
  else stereoPosition();

  // send the 'leds' array out to the actual LED strip
  FastLED.show();
  // insert a delay to keep the framerate modest
  FastLED.delay(1000 / FRAMES_PER_SEC);

  // do some periodic updates
  EVERY_N_MILLISECONDS( 20 ) {
    gHue++;  // cycle base color of rainbow
    if (gPeak > 100) gPeak--;
  }
  EVERY_N_MILLISECONDS( 500 ) {
    checkAmbient();  // Adapt to ambient light
    Serial.print("gLBin: ");
    for (int i = 0; i < 7; i++) {
      Serial.print(gLBin[i]);
      Serial.print(" ");
    }
    Serial.println("");
    Serial.print("gRBin: ");
    for (int i = 0; i < 7; i++) {
      Serial.print(gRBin[i]);
      Serial.print(" ");
    }
    Serial.println("");
    Serial.println("");
    MSGEQ7.reset(); // prevent out-of-sync
  }
  getMSGEQ7();
  //blur1d(leds, NUM_LEDS, 255); // Replaced by fadeToBlackBy() below
}

void getMSGEQ7()
{
  if (MSGEQ7.read()) {
    mapNoise(true);
    for (gBand = 0; gBand < 7; gBand++) {
      gSLBin[gSample][gBand] = MSGEQ7.get(gBand, 0);
      if (gSLBin[gSample][gBand] > THRESHOLD) gSLBin[gSample][gBand] -= THRESHOLD;
      else gSLBin[gSample][gBand] = 0;
      gSRBin[gSample][gBand] = MSGEQ7.get(gBand, 1);
      if (gSRBin[gSample][gBand] > THRESHOLD) gSRBin[gSample][gBand] -= THRESHOLD;
      else gSRBin[gSample][gBand] = 0;
      if (gSLBin[gSample][gBand] > THRESHOLD || gSRBin[gSample][gBand] > THRESHOLD) gIdle = 0;
      else if (gIdle <= MAXIDLE) gIdle++;
    }
    if (++gSample >= SMOOTH) gSample = 0;
  }
}

void magnitudeDither(int center, uint8_t magnitude, int loudBand)
{
  // paints the stereo field
  // standard 60's-era color organ red=bass, green=mid, blue=treble
  int half_stretch = (((NUM_LEDS / 2) * (DEAF / magnitude)) / 4); // up to 1/4 the LEDs
  if (half_stretch > LEDS_PER_CHANNEL) half_stretch = LEDS_PER_CHANNEL;
  int mindex = ((center - half_stretch) >= 0) ? (center - half_stretch) : 0;
  int maxdex = ((center + half_stretch) <= NUM_LEDS) ? (center + half_stretch) : NUM_LEDS;

  for (int i = mindex; i < center; i++) {
    // scale it off-center (left)
    int d = ((center - i) > 0) ? (center - i) : 1;
    if (d < 2) d = 2;

    // Make the dominant band brighter to vizualize tempo better
    if (loudBand != gBand) magnitude *= 0.66;
    switch (gBand) {
      case 0:       // 63Hz
        leds[i].red += magnitude / d;
        break;
      case 1:       // 160Hz
        leds[i].red += magnitude / (d + 1);
        leds[i].green += magnitude / d;
        break;
      case 2:       // 400Hz
        leds[i].red += magnitude / d;
        leds[i].green += magnitude / d;
        break;
      case 3:       // 1,000Hz
        leds[i].green += magnitude / d;
        break;
      case 4:       // 2,500Hz
        leds[i].green += magnitude / d;
        leds[i].blue += (magnitude/2) / d;
        break;
      case 5:       // 6,250Hz
        leds[i].green += magnitude / d;
        leds[i].blue += magnitude / d;
        break;
      case 6:       // 16,000Hz
        leds[i].blue += magnitude / d;
        break;
    }
  }

  // band center
  switch (gBand) {
    case 0:
      leds[center].red += magnitude;
      break;
    case 1:
      leds[center].red += magnitude;
      leds[center].green += magnitude / 2;
      break;
    case 2:
      leds[center].red += magnitude;
      leds[center].green += magnitude;
      break;
    case 3:
      leds[center].green += magnitude;
      break;
    case 4:
      leds[center].green += magnitude;
      leds[center].blue += (magnitude/2);
      break;
    case 5:
      leds[center].green += magnitude;
      leds[center].blue += magnitude;
      break;
    case 6:
      leds[center].blue += magnitude;
      break;
  }

  for (int i = (center+1); i < maxdex; i++) {
    // scale it off-center (right)
    int d = ((i - center) > 0) ? (i - center) : 1;
    if (d < 2) d = 2;

    switch (gBand) {
      case 0:
        leds[i].red += magnitude / d;
        break;
      case 1:
        leds[i].red += magnitude / (d + 1);
        leds[i].green += magnitude / d;
        break;
      case 2:
        leds[i].red += magnitude / d;
        leds[i].green += magnitude / d;
        break;
      case 3:
        leds[i].green += magnitude / d;
        break;
      case 4:
        leds[i].green += magnitude / d;
        leds[i].blue += magnitude / (d+1);
        break;
      case 5:
        leds[i].green += magnitude / d;
        leds[i].blue += magnitude / d;
        break;
      case 6:
        leds[i].blue += magnitude / d;
        break;
    }
  }
}

//  returns center (leds[] index) of stereo spread in current frequency gBand
int gBandCenter()
{
  if (gLBin[gBand] == gRBin[gBand]) return (NUM_LEDS / 2);
  else if (gLBin[gBand] == 0) return (NUM_LEDS - 1);
  else if (gRBin[gBand] == 0) return (0);
  else {
    // O--O--O--O--O--O--O--O--O (9 LED strand)
    // lMax        ^           rMax
    //
    // lefty-ness increases left from center
    int sleft = -(int)((float)gLBin[gBand] * gScale) - 1;

    // righty-ness increases right from center
    int sright = LEDS_PER_CHANNEL + (int)((float)gRBin[gBand] * gScale);

    return (sleft + sright);
  }
}

// one possible visualization: stereo position of frequency groups
void stereoPosition()
{
  uint8_t center;
  uint8_t magnitude;
  uint32_t left, right;
  int volMax, loudBand;

  volMax = loudBand = 0;
  // find the dominant gBand
  for (gBand = 0; gBand < 7; gBand++) {
    left = right = 0;
    for (int i = 0; i < SMOOTH; i++) {
      left += gSLBin[i][gBand];
      right += gSRBin[i][gBand];
    }
    gLBin[gBand] = (uint8_t)(left / SMOOTH);
    gRBin[gBand] = (uint8_t)(right / SMOOTH);
    magnitude = (int)((gLBin[gBand] + gRBin[gBand]) / 2);
    if (magnitude > volMax) {
      volMax = magnitude;
      loudBand = gBand;
    }
  }

  fadeToBlackBy(leds, NUM_LEDS, FADERATE); // it's better if it's fast
  
  for (gBand = 0; gBand < 7; gBand++) {
    // sN sample smooth
    left = right = 0;
    for (int i = 0; i < SMOOTH; i++) {
      left += gSLBin[i][gBand];
      right += gSRBin[i][gBand];
    }
    gLBin[gBand] = (uint8_t)(left / SMOOTH);
    gRBin[gBand] = (uint8_t)(right / SMOOTH);
    // find the "center" of the band
    center = gBandCenter();
    // find the relative "volume" of the band
    magnitude = (int)((gLBin[gBand] + gRBin[gBand]) / 2);
    if (magnitude < THRESHOLD) {
      ++gIdle;
    }
    // blur the band around the center based on volume
    magnitudeDither(center, magnitude, loudBand);
  }
}

void checkAmbient() { // Read the Cds light level
  int val = analogRead(AMBIENT);
  if (val > 96) val = 96;
  else if (val < 5) val = 5;
  FastLED.setBrightness(val);

  // auto gain
  gScale = (float)LEDS_PER_CHANNEL / (float)gPeak;
}

void rainbow()
{
  // FastLED's built-in rainbow
  fill_rainbow( leds, NUM_LEDS, gHue, 7);
}
