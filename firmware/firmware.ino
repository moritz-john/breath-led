#include "xiao_battery.h"
#include <Adafruit_NeoPixel.h>
#include <OneButton.h>

// --- Pin & Hardware Config ---
#define BUTTON_START D8
#define BUTTON_MODE D5
#define LED_PIN D9
#define NUM_LEDS 9

// Load user configuration (exercises, colors, sleep settings)
#include "config.h"

const int NUM_EXERCISE = sizeof(exercises) / sizeof(exercises[0]);

// TODO: evaluate battery savings
const int FRAME_INTERVAL = 50; // ~ 20 FPS (1000ms / 50)

// --- Hardware & State ---
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
XiaoBattery battery;

OneButton buttonStart;
OneButton buttonMode;

bool running                   = false;
int currentExercise            = 0;
int currentPhase               = 0;
int currentRepeat              = 0;
int batteryLedCount            = 0;
unsigned long startTime        = 0;
unsigned long lastActivityTime = 0;
unsigned long lastFrameTime    = 0;

// --- Helper functions ---
void showIndicator() {
  strip.clear();
  strip.setPixelColor(exercises[currentExercise].ledIndex, COLOR_EXERCISE_INDICATOR[0], COLOR_EXERCISE_INDICATOR[1], COLOR_EXERCISE_INDICATOR[2]);
  strip.show();
}

void goToSleep() {
  strip.clear();
  strip.show();
  delay(50);
}

// --- Button callback functions ---
void onStartClick() {
  running = !running;

  // Reset state variables regardless of start/stop for clean next exercise
  currentPhase      = 0;
  currentRepeat     = 0;
  unsigned long now = millis();
  startTime         = now;
  lastActivityTime  = now;
  lastFrameTime     = now;

  if (!running) showIndicator();
}

void onModeClick() {
  if (running) return;

  currentExercise = (currentExercise + 1) % NUM_EXERCISE;
  showIndicator();
  lastActivityTime = millis();
}

void onModeLongPressStart() {
  if (running) return;

  // Get a single voltage reading, scale ×100, and cast to int (e.g. 3.85V → 385)
  int voltage = (int)(battery.GetBatteryVoltage() * 100);
  // Clamp input before mapping to guarantee map() stays within expected bounds
  voltage = constrain(voltage, BATTERY_MIN, BATTERY_MAX);
  // Convert 3.20V–4.20V (320–420) to 1–NUM_LEDS LEDs
  batteryLedCount = map(voltage, BATTERY_MIN, BATTERY_MAX, 1, NUM_LEDS);

  lastActivityTime = millis();
}

void duringModeLongPress() {
  if (running) return;

  // Continuously render the battery indicator computed in onModeLongPressStart
  strip.clear();
  for (int i = 0; i < batteryLedCount; i++)
    strip.setPixelColor(i, COLOR_BATTERY_INDICATOR[0], COLOR_BATTERY_INDICATOR[1], COLOR_BATTERY_INDICATOR[2]);
  strip.show();
}

void onModeLongPressStop() {
  if (running) return;

  showIndicator();
  lastActivityTime = millis();
}

// --- Setup ---
void setup() {
  buttonStart.setup(BUTTON_START, INPUT_PULLUP, true);
  buttonStart.attachClick(onStartClick);

  buttonMode.setup(BUTTON_MODE, INPUT_PULLUP, true);
  buttonMode.attachClick(onModeClick);
  buttonMode.attachLongPressStart(onModeLongPressStart);
  buttonMode.attachDuringLongPress(duringModeLongPress);
  buttonMode.attachLongPressStop(onModeLongPressStop);

  battery.begin();

  strip.begin();
  strip.show();

  showIndicator();
  lastActivityTime = millis();
}

// --- Loop ---
void loop() {
  unsigned long now = millis();

  buttonStart.tick();
  buttonMode.tick();

  if (!running) {
    if (now - lastActivityTime > SLEEP_TIMEOUT)
      goToSleep();
    return; // Not running - skip all breathing logic below
  }

  // 1) Logic Phase: Always runs
  // Get the duration of the current phase in milliseconds to match millis()
  unsigned long duration = exercises[currentExercise].phases[currentPhase] * 1000L;

  // Check whether the current breath phase duration has elapsed
  if (now - startTime >= duration) {

    // Advance at least once, then keep skipping any zero-duration phases (e.g. HOLD_IN = 0 seconds)
    do {
      currentPhase = (currentPhase + 1) % 4; // % 4 wraps 3→0 to keep cycling
    } while (exercises[currentExercise].phases[currentPhase] == 0);

    // Wrapped back to Inhale (phase 0) → one full breath cycle just finished
    if (currentPhase == 0) {
      currentRepeat++;

      // Stop the exercise once all configured repeats are complete
      if (currentRepeat >= exercises[currentExercise].repeats) {
        running          = false;
        lastActivityTime = millis(); // Reset sleep timer
        showIndicator();
        return;
      }
    }

    // Runs once per breathing phase transition
    startTime += duration; // exact phase length, not "= now"; prevents drift from late ticks
    // Exit now: rendering below still has the OLD duration (e.g. 4000ms for INHALE) even though currentPhase just became HOLD_IN. 
    // Next loop() call recomputes duration (e.g. 1000ms) to match, then renders.
    return;
  }

  // 2) Rendering Phase: Capped to FRAME_INTERVAL
  if (now - lastFrameTime >= FRAME_INTERVAL) {
    lastFrameTime = now;

    // Normalized 0.0→1.0 progress through the current breath phase, used for LED animation below
    // Example: 4s Inhale (duration = 4000ms):
    //   now = 1000ms → progress = 0.25 (25% done)
    //   now = 1300ms → progress = 0.325 (32.5% done)
    float progress = (float)(now - startTime) / duration;

    strip.clear();

    if (currentPhase == 0) { // INHALE: wave fills left to right

      // Maps progress to a continuous position on the strip (0.0 -> NUM_LEDS).
      // The fractional part of this number creates the smooth transition.
      // Example: At 4.5, LED 4 is 50% lit. At exactly 4.0, LEDs 0-3 are fully lit and 4+ are completely off.
      float ledProgress = progress * NUM_LEDS;

      for (int i = 0; i < NUM_LEDS; i++) {

        // Calculate brightness for this specific LED (i):
        // < 0.0 : wave hasn't reached it yet (Off)
        // 0 - 1 : wave is currently passing over it (Partial brightness)
        // > 1.0 : wave has fully passed it (Fully lit)
        float intensity = constrain(ledProgress - i, 0.0f, 1.0f);

        strip.setPixelColor(i,
                            round(COLOR_INHALE[0] * intensity),
                            round(COLOR_INHALE[1] * intensity),
                            round(COLOR_INHALE[2] * intensity));
      }

    } else if (currentPhase == 1) { // HOLD_IN: even-indexed LEDs lit
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i % 2 == 0)
          strip.setPixelColor(i, COLOR_HOLD[0], COLOR_HOLD[1], COLOR_HOLD[2]);
      }

    } else if (currentPhase == 2) { // EXHALE: wave drains right to left

      // Same continuous position logic as INHALE, but the progress value is inverted (1.0 -> 0.0).
      // The wave shrinks from NUM_LEDS to 0, so the fractional part determines how an LED fades out.
      // Example: At 4.5, LEDs 5+ are already off, LED 4 is fading (50%), and 0-3 remain lit.
      float ledProgress = (1.0f - progress) * NUM_LEDS;

      for (int i = 0; i < NUM_LEDS; i++) {

        // Calculate brightness for this specific LED (i) as the wave recedes:
        // < 0.0 : wave has already drained past it (Off)
        // 0 - 1 : wave is currently draining over it (Fading out based on the fraction)
        // > 1.0 : wave hasn't drained past it yet (Still fully lit)
        float intensity = constrain(ledProgress - i, 0.0f, 1.0f);

        strip.setPixelColor(i,
                            // round() so low channel values like {0,0,1} still light even below intensity 1.0;
                            // without it, values just below 1.0 truncate to 0 instead.
                            // e.g. with {0,0,1}, LED i=8 at EXHALE start could go straight to off.
                            round(COLOR_EXHALE[0] * intensity),
                            round(COLOR_EXHALE[1] * intensity),
                            round(COLOR_EXHALE[2] * intensity));
      }

    } else { // (currentPhase == 3) HOLD_OUT : odd-indexed LEDs lit
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i % 2 != 0)
          strip.setPixelColor(i, COLOR_HOLD[0], COLOR_HOLD[1], COLOR_HOLD[2]);
      }
    }

    strip.show();
  }
}

// loop() tick:
// │
// ├─ [1] LOGIC PHASE (uncapped - runs every tick for timing precision)
// │   ├─ Compute phase duration and check if elapsed
// │   └─ If elapsed:
// │       ├─ Advance phase (skipping 0s phases), increment repeats if cycle complete
// │       ├─ If finished (all repeats done): stop running and return early (skip startTime update)
// │       └─ Otherwise: update startTime
// │
// └─ [2] RENDERING PHASE (capped to FRAME_INTERVAL)
//     └─ If FRAME_INTERVAL elapsed:
//         ├─ Compute progress (0.0→1.0)
//         ├─ Execute phase-specific rendering, each branch looping LED i (0 to NUM_LEDS):
//         │   ├─ Inhale: map progress UP to ledProgress (0→NUM_LEDS), calculate intensity, set color (see inline comments)
//         │   ├─ Exhale: map progress DOWN to ledProgress (NUM_LEDS→0), calculate intensity, set color (see inline comments)
//         │   └─ Hold In/Out: even (i%2==0) / odd (i%2!=0) LEDs at full COLOR_HOLD
//         └─ strip.show() after all LEDs are set pushes the new colors at once