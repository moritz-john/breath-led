#ifndef XIAO_BATTERY_H
#define XIAO_BATTERY_H

#include <Arduino.h>

#define BATTERY_READ_ENABLE_PIN 19 // High = Enable Reading / Low = Power Save
#define BATTERY_ADC_PIN 29         // Analog input, voltage divider ratio 1:1 (470K/470K)

class XiaoBattery {
public:
  XiaoBattery() {}

  void begin() {
    pinMode(BATTERY_READ_ENABLE_PIN, OUTPUT);
    analogReadResolution(12);
  }

  float GetBatteryVoltage() {
    // Turn ON the voltage divider circuit
    digitalWrite(BATTERY_READ_ENABLE_PIN, HIGH);

    // Dummy read to wake the ADC; the first real read after enabling
    // tends to come out falsely low without this
    analogRead(BATTERY_ADC_PIN);

    delay(10); // let the divider settle

    uint16_t rawValue = analogRead(BATTERY_ADC_PIN);

    // Turn OFF the voltage divider circuit to save power
    digitalWrite(BATTERY_READ_ENABLE_PIN, LOW);

    // rawValue is the divided voltage on a 12-bit scale (0-4095) over
    // the 3.3V ADC reference; x2 undoes the 1:1 divider to get battery voltage
    const float conversion_factor = 3.3f / 4096.0f;
    float voltage = rawValue * conversion_factor * 2.0f;

    return voltage;
  }
};

#endif // XIAO_BATTERY_H