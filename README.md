# Beat Producing Machine

A MIDI and square wave clock generator for synths, sequencers, eurorack, etc.

**Features:**

* MIDI clock out **without** realtime start/stop messages (perfect for Volcas or stuff that you want
  to sync but not trigger sequencer start/stop on) on the DIN socket
* 0-5V 50% duty cycle trigger signal at 1x, 2x, 1/2, and 1/4 of set BPM on 3.5mm jack sockets
  which take both Eurorack patch cables and stereo aux cables
* Run/stop toggle - in stop mode a MIDI keepalive will be sent every 250ms to avoid everything
  going quiet
* Has an OLED screen to show the BPM which is only updated while you mess with the knob
* The play icon in run mode will pulse with the BPM at 50% duty cycle
* BPM ranges from 30 to 250

**Planned features:**

* Store the BPM value in EEPROM on pause and recall it on startup

Runs on an Arduino Nano. I'll probably start selling kits when I get both the firmware and hardware
right - probably in October or so.