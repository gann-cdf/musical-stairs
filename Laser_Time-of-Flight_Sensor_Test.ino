#include <Wire.h>
#include <VL53L0X.h>

const int NUM_SENSORS = 5;
const long SENSOR_TIMEOUT = 25;
const long MAX_TESTS = 1000;
const long MAX_DURATION = 300000;
const int XSHUT_OFFSET = 2; // first XSHUT pin

VL53L0X sensors[NUM_SENSORS];

int count = 0;
int errors[NUM_SENSORS];
int timeouts[NUM_SENSORS];

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println();
  divider();
  Serial.println("Serial interface initialized");

  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(xshut(i), OUTPUT);
    digitalWrite(xshut(i), LOW);
    Serial.println("Reset XSHUT of sensor " + (String) i);
  }
  delay(10);
  Wire.begin();
  Serial.println("Wire interface initialized");
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.println("Sensor " + (String) i + " initializing...");
    pinMode(xshut(i), INPUT);
    delay(10);
    Serial.println("  XSHUT on pin " + (String) xshut(i) + " activated");
    sensors[i].init(true);
    Serial.println("  Sensor " + (String) i + " initialized");
    Serial.println("  Initial reading of " + (String) sensors[i].readRangeSingleMillimeters() + "mm");
    sensors[i].setAddress((uint8_t) xshut(i));
    Serial.println("  Address set to " + (String) sensors[i].getAddress());
    sensors[i].setTimeout(SENSOR_TIMEOUT);
    Serial.println("  Timeout set to " + (String) sensors[i].getTimeout());
    errors[i] = 0;
    timeouts[i] = 0;
  }
  divider();
  Serial.println("Beginning " + (String) MAX_TESTS + " tests over a max of " + (String) MAX_DURATION + " milliseconds");
  divider();

  digitalWrite(LED_BUILTIN, HIGH);
  long start = millis();
  while (millis() < start + MAX_DURATION && count < MAX_TESTS) {
    Serial.print("Test " + (String) (++count) + ": ");
    for (int i = 0; i < NUM_SENSORS; i++) {
      int measurement = sensors[i].readRangeSingleMillimeters();
      Serial.print("[Sensor " + (String) i + ": ");
      if (measurement > 0) Serial.print((String) measurement + "mm");
      if (sensors[i].timeoutOccurred()) {
        Serial.print("TIMEOUT");
        timeouts[i]++;
      } else if (measurement <= 0) {
        Serial.print ((String) measurement + "mm ERROR");
        errors[i]++;
      }
      Serial.print("] ");
    }
    Serial.println();
  }
  long end = millis();
  divider();
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println((String) count + " tests in " + (String) (end - start) + " milliseconds");
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.println("  Sensor " + (String) i + ":");
    Serial.println("    " + (String) (count - errors[i] - timeouts[i]) + " successful measurements");
    Serial.println("    " + (String) errors[i] + " erroneous measurements");
    Serial.println("    " + (String) timeouts[i] + " timeouts");
  }
  divider(); 
}

int xshut(int i) {
  return i + XSHUT_OFFSET;
}

void divider() {
  Serial.println("----------------------------------------------------------------------");
}

void loop() {
}
