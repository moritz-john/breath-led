#include "xiao_battery.h"
#include <Adafruit_NeoPixel.h>
#include <OneButton.h>

// --- Pin & Hardware Config ---
#define BUTTON_START D8
#define BUTTON_SELECT D5
#define LED_PIN D9
#define NUM_LEDS 9

// Load user configuration (exercises, colors, sleep settings)
#include "config.h"

const int NUM_EXERCISE = sizeof(exercises) / sizeof(exercises[0]);

// TODO: evaluate battery savings
const int FRAME_INTERVAL = 50; // ~ 20 FPS (1000ms / 50)

// CENTER finds the symmetric middle of the LED "strip" for measuring distances
const float CENTER = (NUM_LEDS - 1) / 2.0f;
// How far the brightness ring needs to grow to fully light the edge LEDs.
// Edges are CENTER away from the middle, plus one extra step so they end up fully lit (not just reached) by the time the ring finishes expanding.
const float MAX_RADIUS = CENTER + 1.0f;

// --- Hardware & State ---
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
XiaoBattery battery;

OneButton buttonStart;
OneButton buttonSelect;

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

void onSelectClick() {
  if (running) return;

  currentExercise = (currentExercise + 1) % NUM_EXERCISE;
  showIndicator();
  lastActivityTime = millis();
}

void onSelectLongPressStart() {
  if (running) return;

  // Get a single voltage reading, scale ×100, and cast to int (e.g. 3.85V → 385)
  int voltage = (int)(battery.GetBatteryVoltage() * 100);
  // Clamp input before mapping to guarantee map() stays within expected bounds
  voltage = constrain(voltage, BATTERY_MIN, BATTERY_MAX);
  // Convert 3.20V–4.20V (320–420) to 1–NUM_LEDS LEDs
  batteryLedCount = map(voltage, BATTERY_MIN, BATTERY_MAX, 1, NUM_LEDS);

  lastActivityTime = millis();
}

void duringSelectLongPress() {
  if (running) return;

  // Continuously render the battery indicator computed in onSelectLongPressStart
  strip.clear();
  for (int i = 0; i < batteryLedCount; i++)
    strip.setPixelColor(i, COLOR_BATTERY_INDICATOR[0], COLOR_BATTERY_INDICATOR[1], COLOR_BATTERY_INDICATOR[2]);
  strip.show();
}

void onSelectLongPressStop() {
  if (running) return;

  showIndicator();
  lastActivityTime = millis();
}

// --- Setup ---
void setup() {
  buttonStart.setup(BUTTON_START, INPUT_PULLUP, true);
  buttonStart.attachClick(onStartClick);

  buttonSelect.setup(BUTTON_SELECT, INPUT_PULLUP, true);
  buttonSelect.attachClick(onSelectClick);
  buttonSelect.attachLongPressStart(onSelectLongPressStart);
  buttonSelect.attachDuringLongPress(duringSelectLongPress);
  buttonSelect.attachLongPressStop(onSelectLongPressStop);

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
  buttonSelect.tick();

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

    if (currentPhase == 0) { // INHALE: brightness ring expands outward from the center LED

      // Maps progress to a continuous radius growing outward from the center LED (0.0 -> MAX_RADIUS).
      float radius = progress * MAX_RADIUS;

      for (int i = 0; i < NUM_LEDS; i++) {

        // For NUM_LEDS = 9:
        // LED Index:  0  1  2  3  4 (center)  5  6  7  8
        // Distance:   4  3  2  1      0       1  2  3  4
        float distance = abs(i - CENTER);

        // Calculate brightness for this specific LED (i) alone, by comparing its own distance to the current radius
        // < 0.0 : radius hasn't reached this LED yet (Off)
        // 0 - 1 : radius is currently passing this LED's distance (Partial brightness)
        // > 1.0 : radius has grown past this LED already (Fully lit)
        //
        // Example (applied across the whole strip): at radius 1.5, the center LED (distance 0)
        // is fully lit, the LEDs one step out (distance 1) are 50% lit, and LEDs farther away (distance 2+) are still off.
        float intensity = constrain(radius - distance, 0.0f, 1.0f);

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

    } else if (currentPhase == 2) { // EXHALE: brightness ring shrinks from the edges back to the center

      // Same continuous radius logic as INHALE, but the progress value is inverted (1.0 -> 0.0).
      // The ring shrinks from MAX_RADIUS down to 0.
      float radius = (1.0f - progress) * MAX_RADIUS;

      for (int i = 0; i < NUM_LEDS; i++) {

        // How far this LED is from the center LED (symmetric on both sides)
        float distance = abs(i - CENTER);

        // Calculate brightness for this specific LED (i) as the radius shrinks
        // < 0.0 : radius has already shrunk past this LED (Off)
        // 0 - 1 : radius is currently receding through this LED's distance (Fading out based on the fraction)
        // > 1.0 : radius hasn't shrunk past this LED yet (Still fully lit)
        //
        // Example (applied across the whole strip): at radius 2.8, the center LED (distance 0)
        // and the LEDs one step out (distance 1) are still fully lit, the LEDs two steps out
        // (distance 2) are fading (80% lit), and LEDs farther away (distance 3+) are already off.
        float intensity = constrain(radius - distance, 0.0f, 1.0f);

        strip.setPixelColor(i,
                            // round() so low channel values like {0,0,1} still light even below intensity 1.0;
                            // without it, values just below 1.0 truncate to 0 instead.
                            // e.g. with {0,0,1}, an edge LED at EXHALE start could go straight to off.
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
// │       ├─ Advance phase (skipping 0s phases); if this completes a full cycle, increment repeats
// │       ├─ If whole exercise finished (repeat count reached): stop running and return early
// │       └─ Otherwise: update startTime
// │
// └─ [2] RENDERING PHASE (capped to FRAME_INTERVAL)
//     └─ If FRAME_INTERVAL elapsed:
//         ├─ Compute progress (0.0→1.0)
//         ├─ Inhale / Exhale / Hold In / Hold Out: render this phase's LED pattern (see inline comments)
//         └─ strip.show() after all LEDs are set pushes the new colors at once