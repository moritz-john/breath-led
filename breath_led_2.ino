#include <Adafruit_NeoPixel.h>
#include <OneButton.h>
#include <xiaobattery.h>

// --- Pin & Hardware Config ---
#define BUTTON_START 1
#define BUTTON_MODE 0
#define LED_PIN 10
#define NUM_LEDS 8
#define SLEEP_TIMEOUT 10000 // 10 seconds

// --- User LED Color Settings ---
// clang-format off
const uint8_t MAX_BRIGHTNESS              = 20; // 1-255
const uint8_t COLOR_INHALE[3]             = {0, 255, 0};
const uint8_t COLOR_EXHALE[3]             = {0, 0, 255};
const uint8_t COLOR_HOLD[3]               = {255, 180, 0};
const uint8_t COLOR_BATTERY_INDICATOR[3]  = {200, 200, 200};
const uint8_t COLOR_EXERCISE_INDICATOR[3] = {20, 20, 20};
// clang-format on

// --- Battery Settings ---
const int BATTERY_MIN = 320; // 3.20V - empty
const int BATTERY_MAX = 420; // 4.20V - full

// --- Exercise Setup ---
struct Exercise {
  const char *name; // Exercise name
  int phases[4];    // {Inhale, Hold_In, Exhale, Hold_Out} in seconds
  int repeats;      // Number of breath cycles
  int ledIndex;     // Indicator LED when selecting this exercise
};

// { "name", {Inhale, Hold_In, Exhale, Hold_Out}, repeats, LedIndex }
Exercise exercises[] = {
    {"4-7-8", {4, 7, 8, 0}, 8, 0},
    {"Box", {4, 4, 4, 4}, 20, 3},
    {"5:5", {5, 0, 5, 0}, 20, 7},
};

const int NUM_EXERCISE = sizeof(exercises) / sizeof(exercises[0]);

// TODO: evaluate battery savings
const int FRAME_INTERVAL = 50; // ~ 20 FPS (1000ms / 50)

// --- Hardware & State ---
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
Xiao battery;

OneButton buttonStart; // Configured in setup()
OneButton buttonMode;  // Configured in setup()

int currentExercise = 0;
int currentPhase = 0;
int currentRepeat = 0;
bool running = false;
unsigned long startTime = 0;
unsigned long lastActivityTime = 0;
unsigned long lastFrameTime = 0; // Frame limiter - tracks last LED update time

// --- Helpers ---
void showIndicator() {
  strip.clear();
  strip.setPixelColor(exercises[currentExercise].ledIndex, COLOR_EXERCISE_INDICATOR[0], COLOR_EXERCISE_INDICATOR[1], COLOR_EXERCISE_INDICATOR[2]);
  strip.show();
}

void showBatteryLevel() {
  // Voltage is a float (e.g. 3.85V); scaled ×100 and cast to int (e.g. 385) for use with map()
  int voltage = (int)(battery.GetBatteryVoltage() * 100);
  // Clamp input before mapping to guarantee map() stays within expected bounds
  voltage = constrain(voltage, BATTERY_MIN, BATTERY_MAX);
  // Convert 3.20V–4.20V to 1–NUM_LEDS LEDs
  int numLedsLit = map(voltage, BATTERY_MIN, BATTERY_MAX, 1, NUM_LEDS);

  strip.clear();
  for (int i = 0; i < numLedsLit; i++)
    strip.setPixelColor(i, COLOR_BATTERY_INDICATOR[0], COLOR_BATTERY_INDICATOR[1], COLOR_BATTERY_INDICATOR[2]);
  strip.show();
}

void goToSleep() {
  strip.clear();
  strip.show();
  delay(50);
  pinMode(BUTTON_START, INPUT_PULLUP_SENSE);
  pinMode(BUTTON_MODE, INPUT_PULLUP_SENSE);
  sd_power_system_off();
}

// --- Button Callbacks ---
void onStartClick() {
  running = !running;
  // Reset state variables regardless of start/stop for clean next exercise
  currentPhase = 0;
  currentRepeat = 0;
  startTime = millis();
  lastActivityTime = millis();
  lastFrameTime = millis(); // Initialize frame timer on exercise start
  if (!running)
    showIndicator();
}

void onModeClick() {
  if (running)
    return;
  currentExercise = (currentExercise + 1) % NUM_EXERCISE;
  showIndicator();
  lastActivityTime = millis();
}

void onModeLongPressStart() {
  if (running)
    return;
  showBatteryLevel();
  lastActivityTime = millis();
}

void onModeLongPressStop() {
  if (running)
    return;
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
  buttonMode.attachLongPressStop(onModeLongPressStop);

  strip.begin();
  strip.setBrightness(MAX_BRIGHTNESS);
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
        running = false;
        lastActivityTime = millis(); // Reset sleep timer
        showIndicator();
        return;
      }
    }

    startTime += duration; // Advance by exactly one phase duration
    return;                // Skip rendering here — let the next loop() tick draw the fresh phase
  }

  // 2) Rendering Phase: Capped to FRAME_INTERVAL
  if (now - lastFrameTime >= FRAME_INTERVAL) {
    lastFrameTime = now;

    // Normalized 0.0→1.0 progress through the current breath phase, used for LED animation below; only computed if still active
    float progress = (float)(now - startTime) / duration;

    strip.clear();

    if (currentPhase == 0) { // INHALE - wave fills left to right
      float ledProgress = progress * NUM_LEDS;
      // loop through each LED and compute intensity based on distance from ledProgress
      for (int i = 0; i < NUM_LEDS; i++) { 
        // Clamps negatives (LED not reached yet) to 0 (off); caps >1 to 1 so multiplying by 255 stays in uint8_t bounds; 0–1 is between min and max brightness
        float intensity = constrain(ledProgress - i, 0.0f, 1.0f);
        strip.setPixelColor(i, COLOR_INHALE[0] * intensity, COLOR_INHALE[1] * intensity, COLOR_INHALE[2] * intensity);
      }
    } else if (currentPhase == 2) { // EXHALE - wave drains right to left
      float ledProgress = (1.0f - progress) * NUM_LEDS;
      for (int i = 0; i < NUM_LEDS; i++) {
        // Mirrors inhale but reversed; same clamping logic applies
        float intensity = constrain(ledProgress - i, 0.0f, 1.0f);
        strip.setPixelColor(i, COLOR_EXHALE[0] * intensity, COLOR_EXHALE[1] * intensity, COLOR_EXHALE[2] * intensity);
      }
    } else { // HOLD_IN & HOLD_OUT
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
// │   ├─ Compute phase duration
// │   └─ Phase elapsed?
// │       ├─ Advance phase (skip zero-duration phases)
// │       ├─ Full cycle done? → increment repeat, check if finished
// │       └─ startTime += duration → return (skip render this tick)
// │
// └─ [2] RENDERING PHASE (capped to FRAME_INTERVAL - saves power)
//     └─ Frame interval elapsed?
//         ├─ Compute progress (0.0→1.0)
//         ├─ Inhale:  fill LEDs 0→N with COLOR_INHALE
//         ├─ Exhale:  fill LEDs N→0 with COLOR_EXHALE
//         ├─ Hold:    odd LEDs lit with COLOR_HOLD
//         └─ strip.show()