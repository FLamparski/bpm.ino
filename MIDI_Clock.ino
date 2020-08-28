/*
  Firmware for Beat Producing Machine MIDI clock generators
  Copyright (C) 2020 Filip Wieland

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <EEPROM.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <Encoder.h>
#include "Ticker.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "icon_play_filled.h"
#include "icon_play_empty.h"
#include "icon_pause.h"

#define EEPROM_BPM_ADDR 0x0F
#define DEFAULT_BPM 120
#define MIN_BPM 30
#define MAX_BPM 250

// In board v1, the encoder is connected to plain GPIO pins (w/o external interrupt capability)
// This could mean the encoder response is a bit less quick than desired. This anticipates
// a board v2 where the MIDI serial and encoder are swapped around so that the encoder is
// connected to interrupt-enabled pins.
#define BOARD_REV_1

#if defined(BOARD_REV_1)
  #define ENC_A_PIN 8
  #define ENC_B_PIN 9
  #define MIDI_OUT_PIN 3
#elif defined(BOARD_REV_2)
  #define ENC_A_PIN 2
  #define ENC_B_PIN 3
  #define MIDI_OUT_PIN 9
#else
  #error Please select board revision BOARD_REV_1 or BOARD_REV_2
#endif

#define MIDI_IN_PIN (MIDI_OUT_PIN - 1)
#define BUTTON_PIN 10
#define CLOCK_2X_PIN 4
#define CLOCK_1X_PIN 5
#define CLOCK_HALF_PIN 6
#define CLOCK_QUARTER_PIN 7

#define CREATE_TICK_HANDLER(name, pin_id) void name() { \
        static uint8_t state = LOW; \
        digitalWrite(pin_id, state); \
        state = flipPinState(state); }

SoftwareSerial midiOut(MIDI_IN_PIN, MIDI_OUT_PIN);

Encoder encoder(ENC_A_PIN, ENC_B_PIN);

Adafruit_SSD1306 display(128, 32);

// Global state
uint16_t bpm = DEFAULT_BPM;
uint16_t encValue = bpm * 4;
uint8_t playIconValue = 0;
bool isPaused = false;
uint8_t prevPlayIconValue = 0;
bool shouldUpdateDisplay = false;

inline uint32_t bpmToUs(uint32_t bpm);
void midiTickHandler(void);
void midiKeepaliveHandler(void);
void timesTwoTickHandler(void);
void timesOneTickHandler(void);
void halfTickHandler(void);
void quarterTickHandler(void);


// The MIDI idle ticker sends the keepalive message every 270ms. This will
// only be active if the clock is *not* running, otherwise other MIDI
// compliant devices may decide to stop cooperating.
Ticker tickerMidiKeepalive(midiKeepaliveHandler, 270, 0, MILLIS);

// The analogue pulse tickers create a square wave where the rising edge is
// at 2x BPM, 1x BPM, BPM / 2, and BPM / 4
CREATE_TICK_HANDLER(timesTwoTickHandler, CLOCK_2X_PIN);
Ticker timesTwoTicker(timesTwoTickHandler, bpmToUs(bpm) / 4, 0, MICROS_MICROS);

void timesOneTickHandler(void) {
  static uint8_t state = LOW;
  digitalWrite(CLOCK_1X_PIN, state);
  state = flipPinState(state);
  playIconValue = playIconValue == 1 ? 0 : 1;
}
Ticker timesOneTicker(timesOneTickHandler, bpmToUs(bpm) / 2, 0, MICROS_MICROS);

CREATE_TICK_HANDLER(halfTickHandler, CLOCK_HALF_PIN);
Ticker halfTicker(halfTickHandler, bpmToUs(bpm), 0, MICROS);

CREATE_TICK_HANDLER(quarterTickHandler, CLOCK_QUARTER_PIN);
Ticker quarterTicker(quarterTickHandler, bpmToUs(bpm) * 2, 0, MICROS_MICROS);

const uint8_t MIDI_PULSE_MSG = 0xF8;
ISR(TIMER1_COMPA_vect, ISR_BLOCK) {
  midiOut.write(MIDI_PULSE_MSG);
}

const uint8_t MIDI_KEEPALIVE_MSG = 0xFE;
void midiKeepaliveHandler() {
  midiOut.write(MIDI_KEEPALIVE_MSG);
  midiOut.flush();
}

void setupTimer1(void) {
  noInterrupts();
  // Enable Timer1
  PRR &= ~(1<<PRTIM1);

  // Set Timer1 to count up to the compare value and then reset
  TCCR1A &= ~((1<<WGM10) | (1<<WGM11));
  TCCR1B |= (1<<WGM12);
  TCCR1B &= ~(1<<WGM13);

  // Set Timer1 interrupts for compare value reached
  TIMSK1 |= (1<<OCIE1A);
  interrupts();
}

void setTimer1Bpm(uint8_t bpm) {
  // Calculate the compare value to set
  float bpmMidiTickFrequency = ((float) bpm) / 60.0f * 24.0f;

  // compareValue = clockSpeed / prescaler / bpmMidiTickFrequency
  uint16_t compareValue = 16000000 / 64 / bpmMidiTickFrequency;
  
  // Load the compare register with the compare value
  OCR1A = compareValue;
}

void startTimer1(void) {
  noInterrupts();
  // Start the timer (prescaler set to 64)
  TCNT1 = 0;
  TCCR1B |= (1<<CS11) | (1<<CS10);
  TCCR1B &= ~(1<<CS12);
  interrupts();
}

void stopTimer1(void) {
  noInterrupts();
  // Stop the timer
  TCCR1B &= ~((1<<CS12) | (1<<CS11) | (1<<CS10));
  interrupts();
}

void updateDisplay(void);
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(CLOCK_2X_PIN, OUTPUT);
  pinMode(CLOCK_1X_PIN, OUTPUT);
  pinMode(CLOCK_HALF_PIN, OUTPUT);
  pinMode(CLOCK_QUARTER_PIN, OUTPUT);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setRotation(2);
  display.display();

  encoder.write(DEFAULT_BPM * 4);

  timesTwoTicker.start();
  timesOneTicker.start();
  halfTicker.start();
  quarterTicker.start();

  uint8_t eepromBpm = EEPROM.read(EEPROM_BPM_ADDR);
  if (eepromBpm >= MIN_BPM && eepromBpm <= MAX_BPM) {
    bpm = eepromBpm;
  }

  midiOut.begin(31250);

  updateDisplay();

  setupTimer1();
  setTimer1Bpm(bpm);
  startTimer1();
}

void encoderTask(void);
void buttonTask(void);
void displayTask(void);
void loop() {
  tickerMidiKeepalive.update();
  timesTwoTicker.update();
  timesOneTicker.update();
  halfTicker.update();
  quarterTicker.update();

  encoderTask();
  buttonTask();
  displayTask();
}

void buttonTask(void) {
  static uint8_t oldButtonState = HIGH;

  auto buttonState = digitalRead(BUTTON_PIN);
  if (oldButtonState != buttonState) {
    oldButtonState = buttonState;
    if (buttonState == LOW) {
      isPaused = isPaused ? false : true;
      shouldUpdateDisplay = true;
      if (isPaused) {
        // Updating the EEPROM is very slow - 3.3ms - which is
        // unacceptable if a MIDI device is running. So this
        // is really the only place we can persist the selected BPM.
        // EEPROM.update(EEPROM_BPM_ADDR, bpm);
        stopTimer1();
        timesTwoTicker.stop();
        timesOneTicker.stop();
        halfTicker.stop();
        quarterTicker.stop();
        tickerMidiKeepalive.start();
      } else {
        startTimer1();
        timesTwoTicker.start();
        timesOneTicker.start();
        halfTicker.start();
        quarterTicker.start();
        tickerMidiKeepalive.stop();
      }
    }
  }
}

void encoderTask(void) {
  auto newEncValue = encoder.read();
  auto newBpm = newEncValue / 4;
  if (newBpm >= MIN_BPM && newBpm <= MAX_BPM) {
    if (newBpm != bpm) {
      bpm = newBpm;
      setTimer1Bpm(bpm);
      timesTwoTicker.interval(bpmToUs(bpm) / 4);
      timesOneTicker.interval(bpmToUs(bpm) / 2);
      halfTicker.interval(bpmToUs(bpm));
      quarterTicker.interval(bpmToUs(bpm) * 2);

      encValue = bpm * 4;
      encoder.write(encValue);
      shouldUpdateDisplay = true;
    }
  }
  else {
    encoder.write(bpm * 4);
  }

  if (playIconValue != prevPlayIconValue) {
    shouldUpdateDisplay = true;
    prevPlayIconValue = playIconValue;
  }
}

void displayTask(void) {
  static auto msCounter = millis();
  auto ms = millis();
  if (ms - msCounter >= 30 && shouldUpdateDisplay) {
    updateDisplay();
    shouldUpdateDisplay = false;
    msCounter = ms;
  }
}

void updateDisplay(void) {
  auto textWidth = 5 * 4 * (bpm >= 100 ? 4 : 3);
  display.clearDisplay();

  // draw icon
  uint8_t* bmp = nullptr;
  if (isPaused) {
    bmp = ICON_PAUSE_data;
  } else if (playIconValue) {
    bmp = ICON_PLAY_FILLED_data;
  } else {
    bmp = ICON_PLAY_EMPTY_data;
  }
  display.drawBitmap(0, 0, bmp, 32, 32, SSD1306_WHITE);

  // draw current bpm
  display.setCursor(128 - textWidth, 0);
  display.setTextSize(4);
  display.setTextColor(WHITE);
  display.print(bpm, DEC);
  display.display();
}

// Gets the beat period in microseconds
inline uint32_t bpmToUs(uint32_t bpm) {
  return 60000 / bpm * 1000;
}

inline uint8_t flipPinState(uint8_t pinState) {
  return pinState == HIGH ? LOW : HIGH;
}
