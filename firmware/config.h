#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

struct Exercise {
  const char *name; // Exercise name
  int phases[4];    // {Inhale, Hold_In, Exhale, Hold_Out} time in seconds
  int repeats;      // Number of breath cycles
  int ledIndex;     // Indicator LED when selecting this exercise; starts counting at 0 for the first LED
};

// Exercicise Settings:
// Add, remove or edit breathing exercises here:
const Exercise exercises[] = {
    {.name = "4-7-8", .phases = {4, 7, 8, 0}, .repeats = 8,  .ledIndex = 0},
    {.name = "Box",   .phases = {4, 4, 4, 4}, .repeats = 20, .ledIndex = 4},
    {.name = "1:5",   .phases = {4, 0, 8, 0}, .repeats = 25, .ledIndex = 8},
};

// Color Settings:
// Edit the {Red, Green, Blue} values (0-255) to change colors
// Higher values -> brighter, with a smoother inhale/exhale fade
//            COLOR_TYPE[3]               = {1-255, 1-255, 1-255}
const uint8_t COLOR_INHALE[3]             = {0, 20, 0};
const uint8_t COLOR_EXHALE[3]             = {21, 14, 0};
const uint8_t COLOR_HOLD[3]               = {20, 0, 0};
const uint8_t COLOR_EXERCISE_INDICATOR[3] = {20, 20, 20};
const uint8_t COLOR_BATTERY_INDICATOR[3]  = {20, 20, 20};

// Sleep Settings:
// How long (in milliseconds) the device stays idle before turning its LEDs off to save power
const unsigned long SLEEP_TIMEOUT = 60000; // 60 seconds

// Battery Settings: you likely won't need to change this
const int BATTERY_MIN = 320; // 3.20V - empty
const int BATTERY_MAX = 420; // 4.20V - full

#endif // CONFIG_H
