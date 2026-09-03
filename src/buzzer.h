#pragma once

#ifndef BUZZER_H
#define BUZZER_H

#define BUZZER_GPIO 19

// Notes waiting to be played. Holds more than the 64 a single Beep message
// can carry, so a melody streamed in several appended messages does not
// lose its tail while the first part is still playing.
#define BEEP_QUEUE_SIZE 128

#include <Arduino.h>

// One step of a melody, in the terms the hardware works in: a square wave
// of `frequency` Hz held for `duration_ms`. Frequency 0 is a rest.
struct BeepNote {
  uint16_t frequency;
  uint16_t duration_ms;
};

void beep_init(void);

// Play `count` notes back to back. Without `append` this replaces whatever
// is sounding right now; with it the notes are queued behind it and play
// gaplessly. A count of 0 stops playback and drops the queue, as does
// beep_stop(). Notes past the queue capacity are dropped.
void beep_play(const BeepNote *notes, size_t count, bool append);
void beep_stop(void);

// Advances playback to the next note when the current one is up. Called
// from loop(); playback is polled rather than driven from a timer IRQ
// because core0 already turns over well inside a millisecond and nothing
// here is worth a shared-state hazard with the interrupt handler.
void beep_loop(void);

#endif // BUZZER_H
