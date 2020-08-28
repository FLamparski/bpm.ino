#ifndef _UTILS_H_
#define _UTILS_H_

const uint8_t MIDI_PULSE_MSG = 0xF8;
const uint8_t MIDI_KEEPALIVE_MSG = 0xFE;

#define CREATE_TICK_HANDLER(name, pin_id) inline void name() { \
        static uint8_t state = LOW; \
        digitalWrite(pin_id, state); \
        state = flipPinState(state); }

inline uint8_t flipPinState(uint8_t pinState) {
  return pinState == HIGH ? LOW : HIGH;
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

#endif
