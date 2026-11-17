/* Wink's own voice: a closed-mouth hum that keeps the tune and rhythm of a sentence.
 *
 * A port of sound/sketch/hum.py (version 2 of 17 November 2026; version 1, which made the
 * first approved WAVs, is kept in sound/port/v1/) to C99 for the ESP32-S3. The recipe is sound/VOICE.md section 3; where the two
 * disagree this file follows hum.py, and sound/port/REPORT.md lists each place.
 *
 * Usage, from one audio task:
 *     Hum h;  hum_begin(&h, "It is quiet in here.", HUM_CALM);
 *     int16_t buf[512];
 *     while (hum_render(&h, buf, 512) == 512) { play(buf, 512); show(hum_shown(&h)); }
 *     (play the short last block too)
 *
 * No heap, no stdio, no globals with state: every instance carries all of its state in
 * its Hum, so two instances never meet. Float32 only. The planner is lazy: it plans one
 * phrase at a time and hands the renderer one event ahead of the one sounding, so there is
 * no event list to overflow, whatever the sentence length.
 */
#ifndef HUM_H
#define HUM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUM_RATE      16000
#define HUM_MAX_TEXT  240          /* bytes of text kept; longer text is cut at a character boundary */
#define HUM_HARM      12           /* harmonics of f0, those below 3.8 kHz sound */

/* VOICE.md 3.5: "For replies longer than three sentences, the hum stops at the end of the
 * third, and the rest appears silently on the same clock." hum.py has no such stop, and
 * hum.py is what was heard, so the default is 0: every sentence sounds. Build with
 * -DHUM_SOUNDED_SENTENCES=3 for VOICE.md's stop (later pulses keep their timing and
 * reveal, at level 0). */
#ifndef HUM_SOUNDED_SENTENCES
#define HUM_SOUNDED_SENTENCES 0
#endif

/* moods, in this order (0..10) */
enum {
  HUM_CALM, HUM_HAPPY, HUM_DELIGHTED, HUM_CURIOUS, HUM_THINKING, HUM_SLEEPY,
  HUM_SURPRISED, HUM_SAD, HUM_CHEEKY, HUM_TENDER, HUM_ALERT, HUM_MOODS
};

/* One pulse: one syllable, or one step of a transition gesture. Times are fractional ms
 * from the start of the reply, as in hum.py. Only two of these live in a Hum. */
typedef struct {
  float    t_ms, ms;           /* start and length */
  float    p0, p1;             /* pitch at start and end, semitones above C4 */
  float    scoop;              /* st below p0 to start from; rises over the first 25% */
  float    amp;                /* level of the pulse */
  float    fc, fc1;            /* brightness centre at start and end, Hz */
  float    open, open1;        /* mouth openness at start and end (0.25 for the hum) */
  float    breath;             /* noise mix */
  float    console;            /* v2: share of pulse-wave colour in the harmonics, 0..1 */
  float    floor;              /* level (x amp) held after the pulse until the next one */
  float    vib, vib_hz;        /* vibrato depth (st) and rate */
  float    attack, release;    /* ms */
  float    ka_on, ka_off;      /* level slew coefficients during and after the pulse */
  float    inv_ms;             /* 1 / ms */
  uint32_t start;              /* first sample: the first i with i/16 >= t_ms */
  int16_t  reveal_to;          /* text bytes to show once this pulse starts, -1 for none */
  uint8_t  hard;               /* hard onset: a 6 ms click and a fast attack */
  uint8_t  word_start;         /* first pulse of a word: the pitch jumps to it */
} HumEvent;

typedef struct {               /* the word being hummed, as the planner reads it */
  float   wobble, dlen;        /* the fixed per-word pitch nudge and length factor */
  int16_t a, b;                /* its bytes in the text */
  uint8_t n;                   /* pulses, 1..4 */
  int8_t  stress_at;           /* -1 (a function word), 0 or 1 */
  uint8_t func;
  char    v[4];                /* each syllable's vowel letter */
  uint8_t hard[4];             /* each syllable's onset is hard */
} HumWord;

typedef struct Hum Hum;
struct Hum {
  char     text[HUM_MAX_TEXT + 1];
  int16_t  len;
  uint8_t  mood;
  uint8_t  mode;               /* 0 a reply, 1 the opening gesture, 2 the closing gesture */

  /* planner: where it is in the text, and the per-reply rules */
  float    t;                  /* planner clock, ms */
  float    end_ms;             /* the planned end: after the last gap */
  int16_t  scan;               /* where the next phrase is looked for */
  int16_t  ph_end;             /* the phrase's words end before this byte (its mark) */
  int16_t  mark_end;           /* the byte after the phrase's mark */
  int16_t  n_syl, nuclear, k;  /* pulses in the phrase, its main stress, the next one */
  HumWord  word;
  uint8_t  si;                 /* the next syllable in the word */
  char     mark;               /* , ; : . ? !  or 't' for the trail ... */
  uint8_t  in_phrase, q_wh, chain_wh, prev_comma, last_phrase;
  uint8_t  sentence, first_of_sentence, leapt;
  uint8_t  flick_pending;       /* 1 cheeky's flick, 2 delighted's chirp (v2) */
  uint8_t  flick_silent;
  uint8_t  gi;                 /* gesture step */
  uint8_t  chirped, beat;      /* v2: one chirp per reply; stressed beats so far in the phrase */
  float    flick_t, flick_amp;

  /* the pulse sounding and the next one */
  HumEvent cur, nxt;
  uint8_t  have_cur, have_nxt;

  /* renderer */
  uint8_t  finished, hushed, nh;
  int16_t  shown;
  uint32_t pos;                /* samples rendered */
  uint32_t rng;                /* xorshift32 */
  float    amp, st, jit, vph;
  float    nz_lp, nz_hp, lp_out;
  float    x_anchor, f_anchor; /* f0 = f_anchor * 2^((x - x_anchor)/12), linearised */
  float    level;
  float    kpow_open;
  float    g[HUM_HARM], ph[HUM_HARM], kpow[HUM_HARM];
};

/* plan a whole reply (one or more sentences); text up to 240 bytes, copied */
void  hum_begin(Hum *h, const char *text, int mood);
/* write up to n samples; returns how many; fewer than n (possibly 0) means finished */
int   hum_render(Hum *h, int16_t *out, int n);
/* how many bytes of the text should be visible now */
int   hum_shown(const Hum *h);
/* 0..1: the hum's level (its envelope, 1.0 = a main stress in calm), peak of the last block */
float hum_level(const Hum *h);
/* the two transition gestures (VOICE.md section 4); each replaces what was planned */
void  hum_open(Hum *h);
void  hum_close(Hum *h);
/* hush: the level falls with tau 4 ms, no later pulse sounds, all the text is shown */
void  hum_hush(Hum *h);

/* for the test harness: plan text the way hum_begin does and copy out up to max events,
 * without rendering. Returns how many were planned (all of them, even past max). */
int   hum_debug_plan(Hum *h, const char *text, int mood, HumEvent *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* HUM_H */
