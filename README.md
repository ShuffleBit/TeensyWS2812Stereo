# TeensyWS2812Stereo
This Teensyduino sketch produces a visualization of stereo position and volume, for 7 audio frequencies, in very close to real time. Teensy, WS2812B, MSGEQ7

Ever wanted an old 60's color organ to display where in the stereo field that trumpet is? Well, now you can.

This sketch assumes a 9-LED WS2812B strip, two MSGEQ7 chips in the standard configuration, and a cadmium photocell to automatically adapt to ambient light. Strip length is more about the power to drive them than what a Teensy can do.

Make sure you redefine how many LEDs your string has, and where the center LED is, what pins your gear is hooked to, and you're all set.

Uses the FastLED 3.1 library (I used 3.1.2, briefly, for blur1d(), but scrapped that), and the MSGEQ7 library.
