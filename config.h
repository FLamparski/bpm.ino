#ifndef _CONFIG_H_
#define _CONFIG_H_

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

#endif
