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

// --- Helpers ---
void showIndicator() {
  strip.clear();
  strip.setPixelColor(exercises[currentExercise].ledIndex, COLOR_EXERCISE_INDICATOR[0], COLOR_EXERCISE_INDICATOR[1], COLOR_EXERCISE_INDICATOR[2]);
  strip.show();
}

void showBatteryLevel() {
  // Voltage is a float (e.g. 3.85V); scaled ×100 and cast to int (e.g. 385) for use with map()
  int voltage = (int)(battery.GetBatteryVoltage() * 100);

  // Convert 3.20V–4.20V to 1–NUM_LEDS LEDs; extrapolates if input is out of range
  int numLedsLit = map(voltage, BATTERY_MIN, BATTERY_MAX, 1, NUM_LEDS);
  // Clamp result - map() extrapolation can push output below 1 or above NUM_LEDS
  numLedsLit = constrain(numLedsLit, 1, NUM_LEDS);

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
  currentPhase = 0;
  currentRepeat = 0;
  startTime = millis();
  lastActivityTime = millis();
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
    return;
  }

  // Phase timing
  // unsigned long duration = exercises[currentExercise].phases[currentPhase] *
  // 1000L; float progress = (float)(now - startTime) / duration;

  // if (progress >= 1.0f) // Phase has finished
  // {
  //   do
  //   {
  //     currentPhase = (currentPhase + 1) % 4;
  //   } while (exercises[currentExercise].phases[currentPhase] == 0);

  //   if (currentPhase == 0)
  //   {
  //     currentRepeat++;
  //     if (currentRepeat >= exercises[currentExercise].repeats)
  //     {
  //       running = false;
  //       lastActivityTime = millis();
  //       showIndicator();
  //       return;
  //     }
  //   }
  //   startTime = now;
  //   return;
  // }

  // Phase timing
  unsigned long duration = exercises[currentExercise].phases[currentPhase] * 1000L; // convert to milliseconds to match millis()
  // Normalized 0.0→1.0 progress through the current breath phase, used for LED animation below
  float progress = (float)(now - startTime) / duration;

  if (progress >=
      1.0f) // Phase is complete; caught on the next loop() tick after expiry
  {
    // Advance at least once, then keep skipping any zero-duration phases (e.g. HOLD_IN = 0 seconds)
    do {
      currentPhase = (currentPhase + 1) % 4; // % 4 wraps 3→0 to keep cycling
    } while (exercises[currentExercise].phases[currentPhase] == 0);

    // Wrapped back to Inhale → one full breath cycle just finished
    if (currentPhase == 0) {
      currentRepeat++;
      if (currentRepeat >=
          exercises[currentExercise].repeats) // All cycles done
      {
        running = false;
        lastActivityTime = millis(); // Reset sleep timer
        showIndicator();
        return;
      }
    }
    startTime = now; // Start measuring the new phase from this exact tick
    return;          // Skip rendering — let next loop() call draw the fresh phase
  }

  // Rendering
  strip.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t r = 0, g = 0, b = 0;
    if (currentPhase == 0) { // INHALE
      float intensity = constrain(progress * NUM_LEDS - i, 0.0f, 1.0f);
      r = COLOR_INHALE[0] * intensity;
      g = COLOR_INHALE[1] * intensity;
      b = COLOR_INHALE[2] * intensity;
    } else if (currentPhase == 2) { // EXHALE
      float intensity = constrain((1.0f - progress) * NUM_LEDS - i, 0.0f, 1.0f);
      r = COLOR_EXHALE[0] * intensity;
      g = COLOR_EXHALE[1] * intensity;
      b = COLOR_EXHALE[2] * intensity;
    } else { // HOLD_IN & HOLD_OUT
      if (i % 2 != 0) {
        r = COLOR_HOLD[0];
        g = COLOR_HOLD[1];
        b = COLOR_HOLD[2];
      }
    }
    strip.setPixelColor(i, r, g, b);
  }
  strip.show();
}