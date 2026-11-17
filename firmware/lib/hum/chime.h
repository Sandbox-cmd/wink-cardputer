/* Wink's little sounds: one family of short, playful console sounds in C major, made by the
 * same kind of synth as the hum (a few table-sine harmonics with a share of 25% pulse-wave
 * colour, the hum's fixed 1650 Hz "case" resonance, quick glides and tiny arpeggios).
 * Written 17 November 2026; sound/REPORT2.md says what each one is meant to be.
 *
 * Usage, from the audio task, at 16 kHz mono:
 *     ChimeSynth c;  chime_start(&c, CHIME_NOTE);
 *     int16_t buf[512]; int got;
 *     do { got = chime_render(&c, buf, 512); play(buf, got); } while (got == 512);
 *
 * No heap, no stdio, no globals with state, float32 only. sizeof(ChimeSynth) is under 400 bytes.
 *
 * Names. firmware/src/voice.h already has a C++ `enum Chime : uint8_t { CH_BOOT, ... }`,
 * so this header never declares `Chime` or any CH_* name in C++: there the state is
 * `ChimeSynth` and the sounds are CHIME_*, numbered exactly as the enum is, so
 * `chime_start(&c, (int)CH_NOTE)` is right. In plain C (the native harness) `Chime` is also
 * a name for the state, as asked: `void chime_start(Chime *c, int which)`.
 */
#ifndef CHIME_H
#define CHIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHIME_RATE  16000
#define CHIME_HARM  8            /* harmonics per voice, those under 3.8 kHz sound */

/* the same order as voice.h's enum Chime */
enum {
  CHIME_BOOT, CHIME_LINK_UP, CHIME_LINK_DOWN, CHIME_NOTE, CHIME_ERROR, CHIME_PTT_ON,
  CHIME_PTT_OFF, CHIME_TICK, CHIME_TUNE, CHIME_WIN, CHIME_LOSE, CHIME_COUNT
};

typedef struct ChimeNote ChimeNote;      /* one note of a sound; the tables live in chime.c */

typedef struct {                         /* one of the two voices: a note sounding or ringing out */
  const ChimeNote *n;
  uint32_t start, stop;                  /* samples: the note's first, and where its release begins */
  float    amp, ka_on, ka_off, inv_len, f0;
  float    tilt;                         /* the tilt kpow was last made for */
  float    ph[CHIME_HARM], g[CHIME_HARM], kpow[CHIME_HARM];
  uint8_t  nh, on;
} ChimeVoice;

typedef struct ChimeSynth {
  const ChimeNote *notes;
  uint8_t  count, next, done;
  uint32_t pos, end;                     /* samples rendered; the sound's planned end */
  uint32_t rng;
  float    nz_lp, nz_hp, lp, vph;
  ChimeVoice v[2];
} ChimeSynth;

#ifndef __cplusplus
typedef ChimeSynth Chime;                /* the name asked for, in C only (see above) */
#endif

/* begin one of the sounds (CHIME_*); an unknown number gives silence */
void chime_start(ChimeSynth *c, int which);
/* write up to n samples; returns how many; fewer than n (possibly 0) means finished */
int  chime_render(ChimeSynth *c, int16_t *out, int n);
/* the sound's length in samples, without rendering it */
uint32_t chime_length(int which);

#ifdef __cplusplus
}
#endif

#endif /* CHIME_H */
