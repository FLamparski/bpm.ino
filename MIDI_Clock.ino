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
#include "config.h"
#include "utils.h"


// Resources
SoftwareSerial midiOut(MIDI_IN_PIN, MIDI_OUT_PIN);
Encoder encoder(ENC_A_PIN, ENC_B_PIN);
Adafruit_SSD1306 display(128, 32);

// ==== Global state ====
volatile uint8_t tickCount = 0;

uint16_t bpm = DEFAULT_BPM;
uint16_t encValue = bpm * 4;

uint8_t prevPlayIconValue = 0;
uint8_t playIconValue = 0;

bool isPaused = false;

bool shouldUpdateDisplay = false;
// ==== End global state ====

// The analogue pulse tickers create a square wave where the rising edge is
// at 2x BPM, 1x BPM, BPM / 2, and BPM / 4. The MIDI pulse ticker sends a
// MIDI realtime clock message every 1/24th of a beat.
CREATE_TICK_HANDLER(timesTwoTickHandler, CLOCK_2X_PIN);
CREATE_TICK_HANDLER(timesOneTickHandler, CLOCK_1X_PIN);
CREATE_TICK_HANDLER(halfTickHandler, CLOCK_HALF_PIN);
CREATE_TICK_HANDLER(quarterTickHandler, CLOCK_QUARTER_PIN);

ISR(TIMER1_COMPA_vect, ISR_BLOCK) {
  tickCount++;
  midiOut.write(MIDI_PULSE_MSG);

  // Ticks are every 1/24th of a beat
  // tickCount / 12 = 1/2 of a beat
  // tickCount / 24 = 1 beat
  // tickCount / 48 = 2 beats
  // tickCount / 96 = 4 beats
  // each needs to be halved to get the right duty cycle when flipping pins
  if (tickCount % 48 == 0) {
    quarterTickHandler();
    tickCount = 0;
  } 
  if (tickCount % 24 == 0) {
    halfTickHandler();
  } 
  if (tickCount % 12 == 0) {
    playIconValue = playIconValue ? 0 : 1;
    timesOneTickHandler();
  } 
  if (tickCount % 6 == 0) {
    timesTwoTickHandler();
  }
}

// The MIDI idle ticker sends the keepalive message every 270ms. This will
// only be active if the clock is *not* running, otherwise other MIDI
// compliant devices may decide to stop cooperating.
void midiKeepaliveHandler() {
  midiOut.write(MIDI_KEEPALIVE_MSG);
  midiOut.flush();
}

Ticker tickerMidiKeepalive(midiKeepaliveHandler, 270, 0, MILLIS);

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

void tickTask(void);
void encoderTask(void);
void buttonTask(void);
void displayTask(void);
void loop() {
  tickerMidiKeepalive.update();

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
        tickerMidiKeepalive.start();
      } else {
        startTimer1();
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
  if (ms - msCounter >= 16 && shouldUpdateDisplay) {
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
