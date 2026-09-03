#include "buzzer.h"
#include <hardware/clocks.h>
#include <hardware/pwm.h>

// The buzzer is passive: it needs a square wave at the pitch we want, not a
// level, so the pin is driven by the PWM block rather than analogWrite (one
// fixed carrier frequency) as it was when a beep had no pitch to carry.
static uint pwm_slice = 0;
static bool pwm_ready = false;

static BeepNote queue[BEEP_QUEUE_SIZE];
static size_t queue_head = 0; // next slot to write
static size_t queue_tail = 0; // next note to play
static size_t queue_count = 0;

static bool playing = false;
static uint32_t note_ends_at = 0;

static void tone_on(uint16_t frequency) {
  if (frequency == 0)
    return;

  gpio_set_function(BUZZER_GPIO, GPIO_FUNC_PWM);
  if (!pwm_ready) {
    pwm_slice = pwm_gpio_to_slice_num(BUZZER_GPIO);
    pwm_ready = true;
  }

  // Pick the smallest divider that keeps the counter inside its 16 bits, so
  // the pitch lands as close to the asked-for frequency as the hardware can
  uint32_t clock_hz = clock_get_hz(clk_sys);
  float divider = (float)clock_hz / ((float)frequency * 65536.0f);
  if (divider < 1.0f)
    divider = 1.0f;
  if (divider > 255.0f)
    divider = 255.0f; // hardware limit, reached below ~8 Hz
  uint32_t wrap = (uint32_t)((float)clock_hz / (divider * (float)frequency));
  if (wrap < 2)
    wrap = 2; // frequency out of reach, sound something rather than nothing
  if (wrap > 65536)
    wrap = 65536;

  pwm_set_clkdiv(pwm_slice, divider);
  pwm_set_wrap(pwm_slice, wrap - 1);
  pwm_set_gpio_level(BUZZER_GPIO, wrap / 2); // square wave
  pwm_set_enabled(pwm_slice, true);
}

static void tone_off(void) {
  if (pwm_ready)
    pwm_set_enabled(pwm_slice, false);
  // Hand the pin back to plain IO and park it low: a stopped PWM leaves the
  // output wherever the counter happened to be, and a buzzer held at high
  // draws current for no sound
  gpio_set_function(BUZZER_GPIO, GPIO_FUNC_SIO);
  gpio_set_dir(BUZZER_GPIO, GPIO_OUT);
  gpio_put(BUZZER_GPIO, 0);
}

static void queue_clear(void) {
  queue_head = queue_tail = queue_count = 0;
}

// Starts the next queued note, or ends playback when the queue has run out.
// Timing accumulates from the deadline of the note that just finished, not
// from now, so the tempo of a long melody does not creep with the polling
// interval.
static void play_next(void) {
  if (queue_count == 0) {
    tone_off();
    playing = false;
    return;
  }

  const BeepNote note = queue[queue_tail];
  queue_tail = (queue_tail + 1) % BEEP_QUEUE_SIZE;
  queue_count--;

  if (note.frequency == 0)
    tone_off(); // a rest: silence for its duration, then on to the next
  else
    tone_on(note.frequency);

  note_ends_at = (playing ? note_ends_at : millis()) + note.duration_ms;
  playing = true;
}

void beep_init(void) {
  pinMode(BUZZER_GPIO, OUTPUT);
  digitalWrite(BUZZER_GPIO, LOW);
  // The PWM slice is left alone until the first tone: claiming it here would
  // drive the pin before anything asked for a sound
}

void beep_stop(void) {
  queue_clear();
  tone_off();
  playing = false;
}

void beep_play(const BeepNote *notes, size_t count, bool append) {
  if (count == 0) {
    beep_stop(); // an empty melody is how the other side asks for silence
    return;
  }

  if (!append)
    beep_stop();

  for (size_t i = 0; i < count; i++) {
    if (notes[i].duration_ms == 0)
      continue; // a note nobody can hear, and it would stall the queue
    if (queue_count == BEEP_QUEUE_SIZE) {
      Serial.printf("Beep queue full, dropping %u notes\r\n",
                    (unsigned)(count - i));
      break;
    }
    queue[queue_head] = notes[i];
    queue_head = (queue_head + 1) % BEEP_QUEUE_SIZE;
    queue_count++;
  }

  if (!playing)
    play_next();
}

void beep_loop(void) {
  if (!playing)
    return;
  // Signed compare so the deadline keeps working across the millis() wrap
  while (playing && (int32_t)(millis() - note_ends_at) >= 0)
    play_next(); // loops: a note may be shorter than a pass through loop()
}
