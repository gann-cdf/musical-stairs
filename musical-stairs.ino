/*
 * Musical Stairs
 *
 * The core assumptions of this code mostly have to do with the sequence in
 * which the XSHUT pins are wired together:
 *
 *   1. All of the XSHUT pins are contiguous on the Arduino, starting at
 *      XSHUT_OFFSET.
 *   2. The pins are ordered L1, R1, L2, R2, ... , L10, R10
 *
 * At the moment, we are triggering a note to be played if
 * REQ_CONSECUTIVE_BREAKS consecutive readings are broken (i.e. less than
 * UNBROKEN_RANGE) following an unbroken reading.
 *
 * We have also set a very short SENSOR_TIMEOUT since we will have to poll
 * every sensor every time, and waiting for a single sensor to timeout will
 * impinge on the responsiveness of the stairs overall.
 *
 * @author Seth Battis <sbattis@gannacademy.org>
 * @author Zachary Sherman <19zsherman@gannacademy.org>
 * @author Ilana Jacobs <19ijacobs@gannacademy.org>
 * @author Eli Cole <19ecole@gannacademy.org>
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <MIDI.h>

const bool LOGGING = false; // show output logs in Serial Monitor

const long SENSOR_TIMEOUT = 27; // milliseconds
const int XSHUT_OFFSET = 22; // initial XSHUT pin (must be contiguous)
const int REQ_CONSECUTIVE_BREAKS = 3; // number of consecutive broken readings to count as broken
const int DELAY_BEFORE_PLAY_AGAIN = 10; // loop iterations (scanning every sensor)
const int UNBROKEN_RANGE = 1100; // minimum "unbroken" distance for a sensor

const long SERIAL_BAUD_RATE = 115200; // baud
const int MIDI_CHANNEL = 4; // doesn't matter -- laptop listens to all channels
const int MIDI_VELOCITY = 100; // how hard the note is struck [0..128)

const int STAIRS = 6; // number of stairs currently connected
const int SIDES = 2; // one sensor on each side of each stair
  // just reminders about how we understand the side indices
  const int LEFT = 0;
  const int RIGHT = 1;

// some sensors are still too flaky to use -- ignore them
const bool IGNORE_SENSOR[10][2] = {
  {false, false}, // stair 1 (L1, R1)
  {false, false},
  {false, false},
  {false, false},
  {false, false}, // stair 5 (L5, R5)
  {false, false},
  { true, false},
  { true, false},
  {false, true },
  {false, false}, // stair 10 (L10, R10)
};

// global variables
MIDI_CREATE_DEFAULT_INSTANCE(); // initializes MIDI
VL53L0X SENSOR[STAIRS][SIDES];
bool HISTORY[STAIRS][SIDES][REQ_CONSECUTIVE_BREAKS]; // history runs from most recent (0) to least recent (REQ_CONSECUTIVE_BREAKS - 1)
int STATE[STAIRS]; // store the loop counter for the most recent triggering of this step

/**
 * Display optional Serial Monitor logging messages
 * @param String message The message to display
 * @param int indent (Optional) How far message should be indented
 */
void logging(String message, int indent = 0) {
  if (LOGGING) {
    for (int i = 0; i < indent; i++) {
      Serial.print("  ");
    }
    Serial.println(message);
  }
}

/**
 * A visual divider
 * @return String The divider to display
 */
String divider() {
  return "------------------------------------------------------------------------";
}

/**
 * A human-readable name for a sensor
 * @param int stair Stair numer [0..STAIRS)
 * @param int side Side [0..SIDES) or {LEFT, RIGHT}
 * @return String Human-readable sensor name
 */
String sensorName(int stair, int side) {
  return "stair " + (String) (stair + 1) + ", " + (side == LEFT ? "left" : "right") + " side";
}

/**
 * Calculate the I^2C address a sensor based on its position on the stairs
 * @param int stair Stair number [0..STAIRS)
 * @param int side Which side of the stair [0..SIDES) or {LEFT, RIGHT}
 * @return int I^2C address of sensor
 */
int sensorId(int stair, int side) {
  return (stair * 2) + side;
}

/**
 * Calculate XSHUT pin bsaed on sensor ID
 * @param int sensorId I^2C address of the sensor
 * @return int XSHUT pin number
 */
int xshut(int sensorId) {
  return sensorId + XSHUT_OFFSET;
}

/**
 * Calculate MIDI pitch (C major scale) based on the stair
 * @param int stair Stair number [0..STAIRS)
 */
int note(int stair) {
  if (stair < 3) {
    return 60 + (stair * 2);
  } else if (stair < 7) {
    return 59 + (stair * 2);
  } else {
    return 58 + stair * 2;
  }
}

/**
 * Determine if a sensor's beam has been broken
 * @precondition HISTORY contains whether or not the REQ_CONSECUTIVE_BREAKS
 *    previous readings were breaks
 * @postcondition HISTORY has been updated to include this most recent reading
 *    at index 0 and other readings have cycled back in history
 * @param int stair Stair number [0..STAIRS)
 * @param int side Side [0..SIDES) or {LEFT, RIGHT}
 * @param int measurement The current measurement from that sensor
 * @return bool True if the beam is freshly broken by this reading, false
 *    otherwise
 */
bool broken(int stair, int side, int measurement) {
 // is it a break in the beam?
  bool now = measurement < UNBROKEN_RANGE && (measurement != 0 || !SENSOR[stair][side].timeoutOccurred());
  bool result = now;
  if (result) {
    logging("Activity at " + sensorName(stair, side) + " 1" + (HISTORY[stair][side][0] ? "1" : "0")  + (HISTORY[stair][side][1] ? "1" : "0")  + (HISTORY[stair][side][2] ? "1" : "0"));

    // have there been enough previous broken measurements?
    for (int past = 0; past < REQ_CONSECUTIVE_BREAKS - 1 && result; past++) {
      result = result && HISTORY[stair][side][past];
    }

    // was there an unbroken measurement before those breaks?
    result = result && !HISTORY[stair][side][REQ_CONSECUTIVE_BREAKS - 1];
  }

  // add this current break information to the sensor history
  for (int i = REQ_CONSECUTIVE_BREAKS - 1; i > 0; i--) {
    HISTORY[stair][side][i] = HISTORY[stair][side][i-1];
  }
  HISTORY[stair][side][0] = now;

  return result;
}

/**
 * Play a note (if we're not logging to Serial Monitor)
 * TODO We could use multiple serial channels to allow MIDI and Serial Monitor
 *    logging
 * @param int stair Stair number [0..STAIRS)
 */
void playNote(int stair) {
  if (!LOGGING) {
    MIDI.sendNoteOn(note(stair), MIDI_VELOCITY, MIDI_CHANNEL);
  } else {
    logging("Playing note for stair " + (String) stair);
  }
}

/**
 * Play the ready tone for a given stair
 * @param int stair Stair number [0..STAIRS)
 */
void readyTone(int stair) {
  playNote(stair);
  delay(250);
}

/**
 * Initialize a sensor
 * @param int stair Stair number [0..STAIRS)
 * @param int side Side number [0..SIDES) or {LEFT, RIGHT}
 */
void initializeSensor(int stair, int side) {
  logging("Sensor " + (String) sensorId(stair, side) + " initializing");
  pinMode(xshut(sensorId(stair, side)), INPUT);
  delay(10);
  logging("Activated XSHUT " + (String) xshut(sensorId(stair, side)), 1);
  SENSOR[stair][side].init();
  logging("Sensor " + (String) sensorId(stair, side) + " at " + sensorName(stair, side) + " initialized", 1);
  SENSOR[stair][side].setAddress((uint8_t) sensorId(stair, side));
  logging("Address set to " + (String) SENSOR[stair][side].getAddress(), 1);
  SENSOR[stair][side].setTimeout(SENSOR_TIMEOUT);
  logging("Timeout set to " + (String) SENSOR[stair][side].getTimeout(), 1);
  for (int i = 0; i < REQ_CONSECUTIVE_BREAKS; i++) {
    HISTORY[stair][side][i] = false;
  }
  logging("Reset sensor history", 1);
}

/**
 * Initialize sensors and interfaces
 */
void setup() {
  // prepare MIDI
  MIDI.begin(MIDI_CHANNEL); // must be before Serial.begin()

  // prepare for Serial Monitor logging output
  Serial.begin(SERIAL_BAUD_RATE);
  logging("");
  logging(divider());
  logging("MIDI interface initialized");
  logging("Serial interface initialized");

  // reset all sensors
  for (int stair = 0; stair < STAIRS; stair++) {
    for (int side = 0; side < SIDES; side++) {
      pinMode(xshut(sensorId(stair, side)), OUTPUT);
      digitalWrite(xshut(sensorId(stair, side)), LOW);
      logging("Reset XSHUT " + (String) xshut(sensorId(stair, side)));
    }
  }
  delay(10);

  // set sensor I^2C addresses
  Wire.begin(); // must be after XSHUT reset
  logging("I^2C interface initialized");
  for (int stair = 0; stair < STAIRS; stair++) {
    for (int side = 0; side < SIDES; side++) {
      initializeSensor(stair, side);
    }
    STATE[stair] = -1 * REQ_CONSECUTIVE_BREAKS;
    logging("Reset stair state");
    readyTone(stair);
  }
  logging(divider());
}

/**
 * Poll sensors and send MIDI instructions
 */
bool FIRST_RUN = true;
long counter = 0;
void loop() {
  if (FIRST_RUN) {
    logging("Starting polling loop");
    FIRST_RUN = false;
  }
  for (int stair = 0; stair < STAIRS; stair++) {
    for (int side = 0; side < SIDES; side++) {
      if (!IGNORE_SENSOR[stair][side] &&
          broken(stair, side, SENSOR[stair][side].readRangeSingleMillimeters()) &&
          STATE[stair] < counter - DELAY_BEFORE_PLAY_AGAIN) {
        STATE[stair] = counter;
        playNote(stair);
        logging("Play note for " + sensorName(stair, side));
      }
    }
  }
  counter++;
  if (counter >= 2147483647) {
    counter = 0;
  }
}
