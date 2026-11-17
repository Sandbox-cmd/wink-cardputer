/* Wink's own voice: a closed-mouth hum. See hum.h; the reference is sound/sketch/hum.py.
 *
 * Version 2 (17 November 2026), matching hum.py version 2: lifted a fourth (home F4), a
 * little quicker, stressed beats bounce and alternate (the lilt), wider contours on happy,
 * delighted and cheeky, a share of 25% pulse-wave colour in the harmonics (the console),
 * one chirp per delighted reply, and a soft pitch ceiling. Version 1 is kept whole in
 * sound/port/v1/hum.c.
 *
 * Two halves, as in hum.py:
 *   the planner turns text into pulses (one per syllable, a glided contour per word),
 *   the renderer turns pulses into 16 kHz samples (12 table-sine harmonics, one-pole slews).
 * The planner runs lazily inside hum_render: when a pulse starts, the next one is planned.
 * A phrase (the words up to a mark) is read once when its first pulse is planned, for its
 * pulse count, its main stress and whether it is the reply's last.
 *
 * Float32 only. Per sample: two slews, a target, 12 table sines, a noise step, a filter.
 * Per 32 samples: the harmonic gains and the jitter. expf only per pulse, per 32 samples
 * while the mouth opens or closes (a gesture), and when the pitch moves half a semitone
 * from the last anchor.
 */
#include "hum.h"
#include <math.h>
#include <string.h>

#define C4_HZ     261.63f
#define MASTER    0.38f              /* set against Samantha through the speaker band (v1 0.42) */
#define KP        2.836877527e-03f   /* pitch slew, tau 22 ms: 1 - exp(-1/352) */
#define KA_IDLE   6.230509377e-03f   /* level slew before any pulse, tau 10 ms */
#define KA_HUSH   1.550356299e-02f   /* hush, tau 4 ms */
#define LN2_12    5.776226505e-02f   /* ln 2 / 12 */
#define LN10_20   1.151292546e-01f   /* ln 10 / 20 */
#define INV_SR    (1.0f / 16000.0f)
#define FMAX      3800.0f
#define DONE_AMP  2e-4f              /* the level under which a finished reply stops */

/* sin(2 pi i / 256), i = 0..256 */
static const float SINT[257] = {
  0.0f, 2.454122852e-02f, 4.906767433e-02f, 7.356456360e-02f, 9.801714033e-02f, 1.224106752e-01f, 1.467304745e-01f, 1.709618888e-01f,
  1.950903220e-01f, 2.191012402e-01f, 2.429801799e-01f, 2.667127575e-01f, 2.902846773e-01f, 3.136817404e-01f, 3.368898534e-01f, 3.598950365e-01f,
  3.826834324e-01f, 4.052413140e-01f, 4.275550934e-01f, 4.496113297e-01f, 4.713967368e-01f, 4.928981922e-01f, 5.141027442e-01f, 5.349976199e-01f,
  5.555702330e-01f, 5.758081914e-01f, 5.956993045e-01f, 6.152315906e-01f, 6.343932842e-01f, 6.531728430e-01f, 6.715589548e-01f, 6.895405447e-01f,
  7.071067812e-01f, 7.242470830e-01f, 7.409511254e-01f, 7.572088465e-01f, 7.730104534e-01f, 7.883464276e-01f, 8.032075315e-01f, 8.175848132e-01f,
  8.314696123e-01f, 8.448535652e-01f, 8.577286100e-01f, 8.700869911e-01f, 8.819212643e-01f, 8.932243012e-01f, 9.039892931e-01f, 9.142097557e-01f,
  9.238795325e-01f, 9.329927988e-01f, 9.415440652e-01f, 9.495281806e-01f, 9.569403357e-01f, 9.637760658e-01f, 9.700312532e-01f, 9.757021300e-01f,
  9.807852804e-01f, 9.852776424e-01f, 9.891765100e-01f, 9.924795346e-01f, 9.951847267e-01f, 9.972904567e-01f, 9.987954562e-01f, 9.996988187e-01f,
  1.000000000e+00f, 9.996988187e-01f, 9.987954562e-01f, 9.972904567e-01f, 9.951847267e-01f, 9.924795346e-01f, 9.891765100e-01f, 9.852776424e-01f,
  9.807852804e-01f, 9.757021300e-01f, 9.700312532e-01f, 9.637760658e-01f, 9.569403357e-01f, 9.495281806e-01f, 9.415440652e-01f, 9.329927988e-01f,
  9.238795325e-01f, 9.142097557e-01f, 9.039892931e-01f, 8.932243012e-01f, 8.819212643e-01f, 8.700869911e-01f, 8.577286100e-01f, 8.448535652e-01f,
  8.314696123e-01f, 8.175848132e-01f, 8.032075315e-01f, 7.883464276e-01f, 7.730104534e-01f, 7.572088465e-01f, 7.409511254e-01f, 7.242470830e-01f,
  7.071067812e-01f, 6.895405447e-01f, 6.715589548e-01f, 6.531728430e-01f, 6.343932842e-01f, 6.152315906e-01f, 5.956993045e-01f, 5.758081914e-01f,
  5.555702330e-01f, 5.349976199e-01f, 5.141027442e-01f, 4.928981922e-01f, 4.713967368e-01f, 4.496113297e-01f, 4.275550934e-01f, 4.052413140e-01f,
  3.826834324e-01f, 3.598950365e-01f, 3.368898534e-01f, 3.136817404e-01f, 2.902846773e-01f, 2.667127575e-01f, 2.429801799e-01f, 2.191012402e-01f,
  1.950903220e-01f, 1.709618888e-01f, 1.467304745e-01f, 1.224106752e-01f, 9.801714033e-02f, 7.356456360e-02f, 4.906767433e-02f, 2.454122852e-02f,
  0.0f, -2.454122852e-02f, -4.906767433e-02f, -7.356456360e-02f, -9.801714033e-02f, -1.224106752e-01f, -1.467304745e-01f, -1.709618888e-01f,
  -1.950903220e-01f, -2.191012402e-01f, -2.429801799e-01f, -2.667127575e-01f, -2.902846773e-01f, -3.136817404e-01f, -3.368898534e-01f, -3.598950365e-01f,
  -3.826834324e-01f, -4.052413140e-01f, -4.275550934e-01f, -4.496113297e-01f, -4.713967368e-01f, -4.928981922e-01f, -5.141027442e-01f, -5.349976199e-01f,
  -5.555702330e-01f, -5.758081914e-01f, -5.956993045e-01f, -6.152315906e-01f, -6.343932842e-01f, -6.531728430e-01f, -6.715589548e-01f, -6.895405447e-01f,
  -7.071067812e-01f, -7.242470830e-01f, -7.409511254e-01f, -7.572088465e-01f, -7.730104534e-01f, -7.883464276e-01f, -8.032075315e-01f, -8.175848132e-01f,
  -8.314696123e-01f, -8.448535652e-01f, -8.577286100e-01f, -8.700869911e-01f, -8.819212643e-01f, -8.932243012e-01f, -9.039892931e-01f, -9.142097557e-01f,
  -9.238795325e-01f, -9.329927988e-01f, -9.415440652e-01f, -9.495281806e-01f, -9.569403357e-01f, -9.637760658e-01f, -9.700312532e-01f, -9.757021300e-01f,
  -9.807852804e-01f, -9.852776424e-01f, -9.891765100e-01f, -9.924795346e-01f, -9.951847267e-01f, -9.972904567e-01f, -9.987954562e-01f, -9.996988187e-01f,
  -1.000000000e+00f, -9.996988187e-01f, -9.987954562e-01f, -9.972904567e-01f, -9.951847267e-01f, -9.924795346e-01f, -9.891765100e-01f, -9.852776424e-01f,
  -9.807852804e-01f, -9.757021300e-01f, -9.700312532e-01f, -9.637760658e-01f, -9.569403357e-01f, -9.495281806e-01f, -9.415440652e-01f, -9.329927988e-01f,
  -9.238795325e-01f, -9.142097557e-01f, -9.039892931e-01f, -8.932243012e-01f, -8.819212643e-01f, -8.700869911e-01f, -8.577286100e-01f, -8.448535652e-01f,
  -8.314696123e-01f, -8.175848132e-01f, -8.032075315e-01f, -7.883464276e-01f, -7.730104534e-01f, -7.572088465e-01f, -7.409511254e-01f, -7.242470830e-01f,
  -7.071067812e-01f, -6.895405447e-01f, -6.715589548e-01f, -6.531728430e-01f, -6.343932842e-01f, -6.152315906e-01f, -5.956993045e-01f, -5.758081914e-01f,
  -5.555702330e-01f, -5.349976199e-01f, -5.141027442e-01f, -4.928981922e-01f, -4.713967368e-01f, -4.496113297e-01f, -4.275550934e-01f, -4.052413140e-01f,
  -3.826834324e-01f, -3.598950365e-01f, -3.368898534e-01f, -3.136817404e-01f, -2.902846773e-01f, -2.667127575e-01f, -2.429801799e-01f, -2.191012402e-01f,
  -1.950903220e-01f, -1.709618888e-01f, -1.467304745e-01f, -1.224106752e-01f, -9.801714033e-02f, -7.356456360e-02f, -4.906767433e-02f, -2.454122852e-02f,
  0.0f
};

/* ln k, k = 1..12 */
static const float LOGK[HUM_HARM] = {
  0.000000000e+00f, 6.931471806e-01f, 1.098612289e+00f, 1.386294361e+00f, 1.609437912e+00f, 1.791759469e+00f,
  1.945910149e+00f, 2.079441542e+00f, 2.197224577e+00f, 2.302585093e+00f, 2.397895273e+00f, 2.484906650e+00f
};

/* nine numbers each; the habit is the mood's index (v2 added lilt, lean and console) */
typedef struct { float top, range, tempo, bright, breath, gain_db, lilt, lean, console; } Mood;
static const Mood MOODS[HUM_MOODS] = {
  { 5.0f, 1.00f, 0.90f, 1.00f, 0.04f,  0.0f, 0.40f, 0.7f, 0.22f },   /* calm */
  { 6.5f, 1.60f, 0.83f, 1.10f, 0.03f,  0.5f, 0.60f, 0.8f, 0.28f },   /* happy: stressed pulses scoop from 1.5 st below */
  { 8.0f, 1.90f, 0.79f, 1.18f, 0.03f,  1.0f, 0.70f, 0.8f, 0.32f },   /* delighted: the last landing starts 4 st higher; one chirp */
  { 6.0f, 1.20f, 0.87f, 1.06f, 0.04f,  0.0f, 0.50f, 0.7f, 0.25f },   /* curious: statements land on D */
  { 4.5f, 0.45f, 1.04f, 0.92f, 0.04f, -1.0f, 0.20f, 0.6f, 0.20f },   /* thinking: each sentence's first pulse held 1.6x */
  { 2.0f, 0.50f, 1.40f, 0.80f, 0.12f, -3.0f, 0.10f, 0.5f, 0.12f },   /* sleepy: landings sink 0.7 st under home */
  { 8.0f, 1.40f, 0.81f, 1.14f, 0.03f,  1.0f, 0.50f, 0.7f, 0.28f },   /* surprised: a sentence's first stressed pulse leaps */
  { 3.0f, 0.60f, 1.25f, 0.85f, 0.07f, -3.0f, 0.00f, 0.5f, 0.10f },   /* sad: each word's end falls 0.7 st; no vibrato */
  { 5.5f, 1.80f, 0.86f, 1.10f, 0.04f,  0.0f, 0.60f, 0.8f, 0.30f },   /* cheeky: swing, scoops, the last landing flicks up */
  { 3.5f, 0.80f, 1.15f, 0.84f, 0.08f, -2.5f, 0.00f, 0.5f, 0.10f },   /* tender: legato */
  { 7.0f, 1.00f, 0.77f, 1.16f, 0.02f,  1.0f, 0.30f, 0.7f, 0.25f },   /* alert: staccato */
};

/* v2: everything sits a fourth higher. The names keep version 1's roles (G4 is the
 * continuation note); their values are lifted, so HOME sounds F4 and G4 sounds C5. */
#define LIFT  5.0f
#define HOME  (0.0f + LIFT)
#define D4    (2.0f + LIFT)
#define E4    (4.0f + LIFT)
#define G4    (7.0f + LIFT)
#define A4    (9.0f + LIFT)
#define C5   (12.0f + LIFT)
#define SMALL 1.12f                  /* the moving colour sits 12% higher */
#define KNEE  16.0f                  /* above E5 a pitch moves at half rate ... */
#define PMAX  19.0f                  /* ... and never past G5 */
#define GESTURE_CONSOLE 0.15f

static float cap(float p) {
  float q;
  if (p <= KNEE) return p;
  q = KNEE + (p - KNEE) * 0.5f;
  return q < PMAX ? q : PMAX;
}

/* the console colour: a 25% pulse wave's harmonics, |sin(pi k / 4)| / k */
static const float PULSE[HUM_HARM] = {
  7.071067812e-01f, 5.000000000e-01f, 2.357022604e-01f, 0.0f, 1.414213562e-01f, 1.666666667e-01f,
  1.010152545e-01f, 0.0f, 7.856742013e-02f, 1.000000000e-01f, 6.428243465e-02f, 0.0f
};

/* hum.py's FUNCTION set (VOICE.md's list plus "im") and WH set (plus "whom", "whose") */
static const char *const FUNCTION_WORDS[] = {
  "a", "an", "the", "to", "of", "and", "or", "but", "is", "it", "its", "in", "on", "at", "for",
  "as", "be", "are", "was", "were", "i", "you", "me", "my", "your", "we", "he", "she", "they",
  "them", "this", "that", "with", "so", "if", "not", "do", "does", "did", "am", "im", "by",
  "from", "up", "out", "just", 0
};
static const char *const WH_WORDS[] = {
  "what", "where", "who", "whom", "whose", "why", "how", "when", "which", 0
};
static const char *const PREFIXES[] = { "be", "de", "re", "un", "con", "com", "ex", "pre", "a", 0 };

/* the two transition gestures, from hum.py's gesture_open and gesture_close */
typedef struct { float t, ms, p0, p1, amp, fc, fc1, open, open1, attack, release, vib; } Gesture;
static const Gesture GESTURE_OPEN[2] = {
  {   0.0f, 200.0f, G4, G4, 0.70f,  700.0f,  700.0f, 0.10f, 0.10f, 25.0f,   0.0f, 0.00f },
  { 200.0f, 330.0f, G4, C5, 1.00f,  700.0f, 1750.0f, 0.10f, 1.00f, 12.0f, 110.0f, 0.12f },
};
static const Gesture GESTURE_CLOSE[1] = {
  {   0.0f, 420.0f, C5, G4, 0.85f, 1750.0f,  700.0f, 1.00f, 0.10f, 18.0f,  90.0f, 0.00f },
};
#define GESTURE_TAIL_MS 150.0f      /* render.py renders the opening 150 ms past its last pulse */
#define GESTURE_SEED    7u          /* render.py's seed for the gestures */

/* ------------------------------------------------------------------ small helpers */

static uint32_t h32(const char *s, int n) {          /* FNV-1a, as hum.py's h32 */
  uint32_t h = 2166136261u;
  int i;
  for (i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
  return h;
}

static uint32_t xs32(uint32_t *s) {                    /* xorshift32 */
  uint32_t x = *s;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  *s = x;
  return x;
}

static float rng_pm(uint32_t *s) {                     /* uniform -1..1 */
  return (float)xs32(s) * 0x1p-31f - 1.0f;
}

static float tsin(float ph) {                          /* ph in cycles, 0..1 */
  float x = ph * 256.0f;
  int i = (int)x;
  float f = x - (float)i;
  return SINT[i] + (SINT[i + 1] - SINT[i]) * f;
}

static int is_vowel(char c) {
  return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}

static int is_mark(char c) {
  return c == ',' || c == ';' || c == ':' || c == '.' || c == '?' || c == '!';
}

static int in_list(const char *const *list, const char *w) {
  int i;
  for (i = 0; list[i]; i++) if (strcmp(list[i], w) == 0) return 1;
  return 0;
}

/* Python's str.isalnum() or "'", for one code point. Exact for ASCII; for the rest, an
 * approximation that keeps the letters of the common scripts. */
static int is_word_cp(uint32_t c) {
  if (c < 0x80) return (c >= '0' && c <= '9') || ((c | 32u) >= 'a' && (c | 32u) <= 'z') || c == '\'';
  if (c == 0xAA || c == 0xB2 || c == 0xB3 || c == 0xB5 || c == 0xB9 || c == 0xBA || (c >= 0xBC && c <= 0xBE)) return 1;
  if (c >= 0xC0 && c <= 0x2C1) return c != 0xD7 && c != 0xF7;
  if (c >= 0x370 && c <= 0x3FF) return c != 0x375 && c != 0x37E && c != 0x384 && c != 0x385 && c != 0x387;
  if (c >= 0x400 && c <= 0x52F) return !(c >= 0x482 && c <= 0x489);
  if (c >= 0x5D0 && c <= 0x5EA) return 1;
  if ((c >= 0x620 && c <= 0x64A) || (c >= 0x660 && c <= 0x669) || (c >= 0x671 && c <= 0x6D3)) return 1;
  if (c >= 0x3041 && c <= 0x30FF) return c != 0x30FB;
  if (c >= 0x4E00 && c <= 0x9FFF) return 1;
  if (c >= 0xAC00 && c <= 0xD7A3) return 1;
  return 0;
}

/* is the character at byte i part of a word; *adv = its length in bytes */
static int word_at(const char *s, int i, int len, int *adv) {
  unsigned char c = (unsigned char)s[i];
  int L, k;
  uint32_t cp;
  *adv = 1;
  if (c < 0x80) return is_word_cp(c);
  if ((c & 0xE0) == 0xC0) { L = 2; cp = c & 0x1Fu; }
  else if ((c & 0xF0) == 0xE0) { L = 3; cp = c & 0x0Fu; }
  else if ((c & 0xF8) == 0xF0) { L = 4; cp = c & 0x07u; }
  else return 0;
  if (i + L > len) return 0;
  for (k = 1; k < L; k++) {
    unsigned char d = (unsigned char)s[i + k];
    if ((d & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (d & 0x3Fu);
  }
  *adv = L;
  return is_word_cp(cp);
}

/* the next word starting at or after byte i and before lim: [*a, *b) */
static int next_word(const char *s, int i, int lim, int len, int *a, int *b) {
  int adv;
  while (i < lim) {
    if (word_at(s, i, len, &adv)) break;
    i += adv;
  }
  if (i >= lim) return 0;
  *a = i;
  while (i < lim && word_at(s, i, len, &adv)) i += adv;
  *b = i;
  return 1;
}

static float vowel_fc(char v) {
  switch (v) {
    case 'a': return 950.0f;
    case 'o': return 860.0f;
    case 'u': return 820.0f;
    case 'e': return 1180.0f;
    case 'i': return 1420.0f;
    case 'y': return 1380.0f;
    default:  return 1000.0f;
  }
}

/* ------------------------------------------------------------------ the planner */

/* hum.py: lw = w.lower() without apostrophes; syllables(lw); stress; onsets; the word seed */
static void read_word(const char *s, int a, int b, HumWord *w) {
  char lw[HUM_MAX_TEXT + 1];
  int n = 0, i, alld, total = 0, last_start = -1, ng, k;
  int gpos[4];
  char gv[4];
  uint32_t seed;
  for (i = a; i < b; i++) {
    char c = s[i];
    if (c == '\'') continue;
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    lw[n++] = c;
  }
  lw[n] = 0;
  w->a = (int16_t)a; w->b = (int16_t)b;

  seed = h32(lw, n);
  if (!seed) seed = 1;
  w->wobble = rng_pm(&seed) * 0.4f;                  /* the same word always sits the same way */
  w->dlen = 1.0f + rng_pm(&seed) * 0.08f;

  alld = n > 0;
  for (i = 0; i < n; i++) if (lw[i] < '0' || lw[i] > '9') { alld = 0; break; }
  if (alld) {                                        /* a number: one pulse a digit, up to 4 */
    ng = n < 4 ? n : 4;
    for (k = 0; k < ng; k++) { gpos[k] = k; gv[k] = 'e'; }
  } else {
    i = 0;
    while (i < n) {                                  /* groups of vowels */
      if (is_vowel(lw[i])) {
        int j = i;
        while (j < n && is_vowel(lw[j])) j++;
        if (total < 4) { gpos[total] = i; gv[total] = lw[i]; }
        last_start = i;
        total++;
        i = j;
      } else {
        i++;
      }
    }
    /* a silent final e */
    if (total > 1 && lw[n - 1] == 'e' && !(n >= 2 && lw[n - 2] == 'l') && last_start == n - 1) total--;
    if (total == 0) { gpos[0] = 0; gv[0] = 'a'; total = 1; }   /* digits mixed in, initials */
    ng = total < 4 ? total : 4;
  }
  w->n = (uint8_t)ng;
  w->func = (uint8_t)(ng == 1 && in_list(FUNCTION_WORDS, lw));
  w->stress_at = 0;
  if (w->func) {
    w->stress_at = -1;
  } else if (ng == 2 && n > 4) {
    for (k = 0; PREFIXES[k]; k++) {
      size_t pl = strlen(PREFIXES[k]);
      if (strncmp(lw, PREFIXES[k], pl) == 0) { w->stress_at = 1; break; }
    }
  }
  for (k = 0; k < 4; k++) { w->v[k] = 'a'; w->hard[k] = 0; }
  for (k = 0; k < ng; k++) {
    char on = gpos[k] > 0 ? lw[gpos[k] - 1] : 0;   /* the letter before the vowel group */
    w->v[k] = gv[k];
    w->hard[k] = (uint8_t)(on && strchr("pbtdkgcqx", on) != 0);
  }
}

static int is_wh_word(const char *s, int a, int b) {   /* hum.py: the raw word, lowered */
  char lw[8];
  int n = b - a, i;
  if (n <= 0 || n > 5) return 0;
  for (i = 0; i < n; i++) {
    char c = s[a + i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    lw[i] = c;
  }
  lw[n] = 0;
  return in_list(WH_WORDS, lw);
}

/* read the next phrase: its words, its mark, its pulse count and main stress */
static int find_phrase(Hum *h) {
  const char *s = h->text;
  int len = h->len, i = h->scan, adv, start, j, last_end, a, b, base, wa, wb;
  char c = 0;
  HumWord w;

  while (i < len) {
    if (word_at(s, i, len, &adv)) break;
    i += adv;                                        /* marks with no words before them are dropped */
  }
  if (i >= len) return 0;
  start = i; j = i; last_end = i;
  while (j < len) {
    c = s[j];
    if (is_mark(c)) break;
    if (word_at(s, j, len, &adv)) last_end = j + adv;
    j += adv;
  }
  if (j >= len) {                                    /* the text's end counts as '.' */
    h->mark = '.'; h->ph_end = (int16_t)len; h->mark_end = (int16_t)last_end; h->scan = (int16_t)len;
  } else {
    h->mark = c; h->ph_end = (int16_t)j; h->mark_end = (int16_t)(j + 1);
    if (c == '.') {                                  /* '.' after '.' is one trail mark */
      int k = j + 1;
      while (k < len) {
        char d = s[k];
        if (d == '.') { h->mark = 't'; h->mark_end = (int16_t)(k + 1); k++; continue; }
        if (is_mark(d) || word_at(s, k, len, &adv)) break;
        k += adv;
      }
    }
    h->scan = h->mark_end;
  }
  /* the reply's last phrase: no word after its mark */
  h->last_phrase = 1;
  for (i = h->scan; i < len; i += adv) {
    if (word_at(s, i, len, &adv)) { h->last_phrase = 0; break; }
  }
  /* pulses and the main stress (the phrase's last stressed pulse) */
  h->n_syl = 0; h->nuclear = -1;
  base = 0; wa = -1; wb = -1;
  i = start;
  while (next_word(s, i, h->ph_end, len, &a, &b)) {
    if (wa < 0) { wa = a; wb = b; }
    read_word(s, a, b, &w);
    if (w.stress_at >= 0) h->nuclear = (int16_t)(base + w.stress_at);
    base += w.n;
    i = b;
  }
  h->n_syl = (int16_t)base;
  if (h->nuclear < 0) h->nuclear = (int16_t)(base - 1);
  /* a question that starts with a wh-word rises less; the start is found back through commas */
  if (!h->prev_comma) h->chain_wh = (uint8_t)is_wh_word(s, wa, wb);
  h->q_wh = (uint8_t)(h->mark == '?' && h->chain_wh);
  read_word(s, wa, wb, &h->word);
  h->si = 0; h->k = 0; h->in_phrase = 1; h->beat = 0;
  return 1;
}

static void finalize(HumEvent *e) {
  float x = e->t_ms * 16.0f, ta, tr;
  uint32_t st = (uint32_t)x;
  if ((float)st < x) st++;
  e->start = st;
  e->inv_ms = 1.0f / e->ms;
  ta = e->attack / 3.0f; if (ta < 0.5f) ta = 0.5f;
  tr = (e->release > 8.0f ? e->release : 8.0f) / 3.0f;
  e->ka_on = 1.0f - expf(-1.0f / (16.0f * ta));
  e->ka_off = 1.0f - expf(-1.0f / (16.0f * tr));
}

static int silent_sentence(const Hum *h) {
  return HUM_SOUNDED_SENTENCES > 0 && h->sentence >= HUM_SOUNDED_SENTENCES;
}

/* the next syllable of the phrase, and what comes after it */
static void make_syllable(Hum *h, HumEvent *e) {
  const Mood *m = &MOODS[h->mood];
  const int md = h->mood;
  HumWord *w = &h->word;
  const int si = h->si, k = h->k, n = h->n_syl;
  const int stressed = (si == w->stress_at), func = w->func;
  const int sent_drop = h->sentence < 3 ? h->sentence : 3;
  const char mark = h->mark;
  const int stop = (mark == '.' || mark == '!' || mark == '?' || mark == 't');
  const int last = (k == n - 1);
  const int word_start = (si == 0), word_end = (si == w->n - 1);
  float line_top = m->top + LIFT - 0.6f * (float)sent_drop;
  float frac = (float)k / (float)(n - 1 > 1 ? n - 1 : 1);
  float line = line_top - 2.0f * frac * m->range;      /* the phrase drifts down */
  float p, amp, ms, p0, p1;

  if (stressed) {                                      /* v2: higher, and alternating (the lilt) */
    p = line + (2.3f + ((h->beat % 2 == 0) ? m->lilt : -0.5f * m->lilt)) * m->range + w->wobble; amp = 0.86f;
    h->beat++;
  }
  else if (func) { p = line - 1.0f * m->range + w->wobble; amp = 0.58f; }
  else           { p = line - 0.9f * m->range + w->wobble; amp = 0.70f; }
  if (k == h->nuclear) { p = line + 3.0f * m->range + w->wobble; amp = 1.0f; }
  ms = 150.0f * m->tempo * w->dlen;
  if (stressed) ms *= 1.30f;
  else if (func) ms *= 0.72f;
  else ms *= 0.88f;
  if (last) ms *= stop ? 1.45f : 1.2f;
  p0 = p; p1 = p - m->lean;                            /* every syllable leans down */
  if (last) {                                          /* the landings: the key's anchors */
    if (mark == ',' || mark == ';' || mark == ':') {
      p0 = p; p1 = p < G4 ? G4 : p + 1.0f;
    } else if (mark == '?') {
      float tgt = h->q_wh ? E4 + 3.0f : A4;
      p0 = p < tgt - 4.0f ? p : tgt - 4.0f; p1 = tgt;
      ms *= 1.1f;
    } else if (mark == 't') {
      p0 = p; p1 = p - 1.5f;
      ms *= 1.3f;
    } else {
      float land = md == HUM_CURIOUS ? D4 : HOME;
      p0 = p > land + 3.0f ? p : land + 3.0f; p1 = land;
      if (mark == '!') { p0 += 1.0f; if (amp < 1.0f) amp = 1.0f; }
      if (md == HUM_DELIGHTED && h->last_phrase) p0 += 4.0f;
      if (md == HUM_SLEEPY) p1 = land - 0.7f;
    }
  }
  /* habits */
  if (md == HUM_SAD && word_end && !last) p1 -= 0.7f;
  if (md == HUM_ALERT) ms *= 0.72f;
  if (md == HUM_THINKING && h->first_of_sentence && k == 0) ms *= 1.6f;
  if (md == HUM_SURPRISED && !h->leapt && stressed) { p0 += 4.0f; p1 += 3.0f; h->leapt = 1; }
  if (md == HUM_CHEEKY) ms *= (k % 2 == 0) ? 1.18f : 0.80f;
  p0 = cap(p0); p1 = cap(p1);                          /* v2: the soft ceiling */
  e->scoop = 0.0f;
  if (stressed && md == HUM_HAPPY) e->scoop = -1.5f;
  if (stressed && md == HUM_CHEEKY) e->scoop = -2.0f;
  amp *= expf((m->gain_db - (float)sent_drop) * LN10_20);
  if (silent_sentence(h)) amp = 0.0f;

  e->t_ms = h->t; e->ms = ms; e->p0 = p0; e->p1 = p1; e->amp = amp;
  e->hard = (uint8_t)(w->hard[si] || md == HUM_ALERT);
  e->attack = (e->hard ? 5.0f : 22.0f) * ((md == HUM_TENDER || md == HUM_SLEEPY) ? 1.6f : 1.0f);
  e->fc = e->fc1 = vowel_fc(w->v[si]) * m->bright * SMALL;
  e->open = e->open1 = 0.25f;
  e->breath = m->breath;
  e->console = m->console;
  e->vib = md == HUM_SAD ? 0.0f : 0.10f;
  e->vib_hz = md == HUM_SLEEPY ? 3.8f : 5.2f;
  e->word_start = (uint8_t)word_start;
  e->reveal_to = (int16_t)(word_start ? w->b : -1);
  e->floor = 0.0f;
  h->first_of_sentence = 0;
  h->t += ms;

  if (last) {
    float gap;
    switch (mark) {
      case ',': gap = 180.0f; break;
      case ';': case ':': gap = 230.0f; break;
      case '!': gap = 320.0f; break;
      case '?': gap = 360.0f; break;
      case 't': gap = 420.0f; break;
      default:  gap = 340.0f; break;
    }
    e->reveal_to = h->mark_end;                        /* the mark shows with the last pulse */
    e->release = stop ? 60.0f : 40.0f;
    if (md == HUM_CHEEKY && (mark == '.' || mark == '!') && h->last_phrase) {
      h->flick_pending = 1;                            /* the fall-rise: a short light flick */
      h->flick_t = h->t + 25.0f;
      h->flick_silent = (uint8_t)silent_sentence(h);
      h->t += 120.0f;
    }
    if (md == HUM_DELIGHTED && (mark == '!' || mark == ',') && !h->chirped && !h->last_phrase) {
      h->flick_pending = 2;                            /* v2: the one chirp of a delighted reply */
      h->flick_t = h->t + 30.0f;
      h->flick_silent = (uint8_t)silent_sentence(h);
      h->flick_amp = 0.45f * expf((m->gain_db - (float)sent_drop) * LN10_20);
      h->chirped = 1;
      h->t += 105.0f;
    }
    h->t += gap * m->tempo;
    if (stop) { h->sentence++; h->first_of_sentence = 1; h->leapt = 0; }
    h->prev_comma = (uint8_t)(mark == ',');
    h->in_phrase = 0;
  } else if (word_end) {                               /* between words: a dip, still sounding */
    e->release = 30.0f;
    e->floor = md == HUM_TENDER ? 0.30f : (md == HUM_ALERT ? 0.0f : 0.12f);
    h->t += (md == HUM_TENDER ? 30.0f : 45.0f) * m->tempo;
  } else {
    e->release = 0.0f;                                 /* inside a word: a dip, not a stop */
  }

  if (h->in_phrase) {
    h->k++; h->si++;
    if (h->si >= w->n) {
      int a, b;
      if (next_word(h->text, w->b, h->ph_end, h->len, &a, &b)) read_word(h->text, a, b, w);
      h->si = 0;
    }
  }
}

static void make_flick(Hum *h, HumEvent *e) {
  const Mood *m = &MOODS[h->mood];
  e->t_ms = h->flick_t; e->ms = 95.0f * m->tempo;
  e->p0 = HOME + 1.0f; e->p1 = E4; e->scoop = 0.0f;
  e->amp = h->flick_silent ? 0.0f : 0.55f * expf(m->gain_db * LN10_20);
  e->fc = e->fc1 = 1150.0f * m->bright * SMALL;
  e->open = e->open1 = 0.25f;
  e->breath = m->breath; e->floor = 0.0f;
  e->vib = 0.0f; e->vib_hz = 5.2f;
  e->attack = 10.0f; e->release = 35.0f;
  e->hard = 0; e->word_start = 0; e->reveal_to = -1;
  e->console = m->console;
}

/* v2: delighted's chirp, C5 up to F5 (home an octave above) in 65 ms; it jumps to its note */
static void make_chirp(Hum *h, HumEvent *e) {
  const Mood *m = &MOODS[h->mood];
  e->t_ms = h->flick_t; e->ms = 65.0f;
  e->p0 = G4; e->p1 = HOME + 12.0f; e->scoop = 0.0f;
  e->amp = h->flick_silent ? 0.0f : h->flick_amp;
  e->fc = e->fc1 = 1700.0f * m->bright * SMALL;
  e->open = e->open1 = 0.25f;
  e->breath = m->breath; e->floor = 0.0f;
  e->vib = 0.0f; e->vib_hz = 5.2f;
  e->attack = 4.0f; e->release = 25.0f;
  e->hard = 0; e->word_start = 1; e->reveal_to = -1;
  e->console = 0.5f;
}

static int gen_gesture(Hum *h, HumEvent *e) {
  const Gesture *gs = h->mode == 1 ? GESTURE_OPEN : GESTURE_CLOSE;
  const int ng = h->mode == 1 ? 2 : 1;
  const Gesture *g;
  if (h->gi >= ng) {
    g = &gs[ng - 1];
    h->end_ms = g->t + g->ms + GESTURE_TAIL_MS;
    return 0;
  }
  g = &gs[h->gi++];
  e->t_ms = g->t; e->ms = g->ms; e->p0 = g->p0; e->p1 = g->p1; e->scoop = 0.0f;
  e->amp = g->amp; e->fc = g->fc; e->fc1 = g->fc1; e->open = g->open; e->open1 = g->open1;
  e->breath = 0.02f; e->floor = 0.0f; e->vib = g->vib; e->vib_hz = 5.2f;
  e->attack = g->attack; e->release = g->release; e->console = GESTURE_CONSOLE;
  e->hard = 0; e->word_start = 0; e->reveal_to = -1;
  finalize(e);
  return 1;
}

/* plan the next pulse; 0 once the reply has no more */
static int gen_next(Hum *h, HumEvent *e) {
  if (h->mode) return gen_gesture(h, e);
  if (h->flick_pending) {
    if (h->flick_pending == 2) make_chirp(h, e); else make_flick(h, e);
    h->flick_pending = 0;
    finalize(e);
    return 1;
  }
  if (!h->in_phrase && !find_phrase(h)) {
    h->end_ms = h->t;
    return 0;
  }
  make_syllable(h, e);
  finalize(e);
  return 1;
}

/* ------------------------------------------------------------------ the renderer */

static void set_anchor(Hum *h, float x) {
  h->x_anchor = x;
  h->f_anchor = C4_HZ * expf(LN2_12 * x);
}

/* per-harmonic gains, once per 32 samples; they sum to 1. v2: a share con of the sum is a
 * pulse wave's spectrum (through the same speaker high-pass), the rest is the hum's own */
static void harmonics(Hum *h, float f0, float fc, float opn, float con) {
  float bw_inv, tot = 0.0f, ptot = 0.0f, it, ip;
  float pw[HUM_HARM];
  int k, nh = 0;
  if (opn != h->kpow_open) {                           /* k^-(1.2 - 0.8 open): changes only in a gesture */
    float tilt = 1.2f - 0.8f * opn;
    for (k = 0; k < HUM_HARM; k++) h->kpow[k] = expf(-tilt * LOGK[k]);
    h->kpow_open = opn;
  }
  bw_inv = 1.0f / (380.0f + 500.0f * opn);
  for (k = 0; k < HUM_HARM; k++) {
    float f = f0 * (float)(k + 1), f2, hp, d, d2, a;
    if (f > FMAX) break;
    f2 = f * f;
    hp = f2 / (f2 + 176400.0f);                        /* nothing spent below the speaker (420 Hz) */
    d = (f - fc) * bw_inv;                             /* the moving colour */
    d2 = (f - 1650.0f) * (1.0f / 320.0f);              /* the case: one fixed resonance */
    a = h->kpow[k] * (0.22f + 1.0f / (1.0f + d * d)) * (1.0f + 0.9f / (1.0f + d2 * d2)) * hp;
    h->g[k] = a; tot += a; nh++;
    pw[k] = PULSE[k] * hp; ptot += pw[k];
  }
  it = tot > 0.0f ? (1.0f - con) / tot : 0.0f;
  ip = ptot > 0.0f ? con / ptot : 0.0f;
  for (k = 0; k < nh; k++) h->g[k] = h->g[k] * it + pw[k] * ip;
  for (; k < HUM_HARM; k++) h->g[k] = 0.0f;
  h->nh = (uint8_t)nh;
}

static void start_next(Hum *h) {
  h->cur = h->nxt;
  h->have_cur = 1;
  if (h->cur.word_start || h->amp < 0.02f)            /* hum.py: every word start jumps */
    h->st = h->cur.p0 + h->cur.scoop;
  if (h->cur.reveal_to > h->shown) h->shown = h->cur.reveal_to;
  h->have_nxt = (uint8_t)gen_next(h, &h->nxt);
}

static void reset(Hum *h, uint32_t seed) {
  int k;
  h->t = 0.0f; h->end_ms = 0.0f; h->scan = 0; h->ph_end = 0; h->mark_end = 0;
  h->n_syl = 0; h->nuclear = 0; h->k = 0; h->si = 0; h->mark = '.';
  memset(&h->word, 0, sizeof h->word);
  h->in_phrase = 0; h->q_wh = 0; h->chain_wh = 0; h->prev_comma = 0; h->last_phrase = 0;
  h->sentence = 0; h->first_of_sentence = 1; h->leapt = 0;
  h->flick_pending = 0; h->flick_silent = 0; h->flick_t = 0.0f; h->gi = 0;
  h->flick_amp = 0.0f; h->chirped = 0; h->beat = 0;
  h->have_cur = 0; h->have_nxt = 0;
  memset(&h->cur, 0, sizeof h->cur);
  memset(&h->nxt, 0, sizeof h->nxt);
  h->finished = 0; h->hushed = 0; h->nh = 0; h->shown = 0;
  h->pos = 0; h->rng = seed ? seed : 1u;
  h->amp = 0.0f; h->st = 5.0f; h->jit = 0.0f; h->vph = 0.0f;
  h->nz_lp = 0.0f; h->nz_hp = 0.0f; h->lp_out = 0.0f;
  set_anchor(h, h->st);
  h->level = 0.0f;
  h->kpow_open = -1.0f;
  for (k = 0; k < HUM_HARM; k++) { h->g[k] = 0.0f; h->ph[k] = 0.0f; h->kpow[k] = 0.0f; }
}

static void copy_text(Hum *h, const char *text) {
  int n = 0;
  if (text) while (n < HUM_MAX_TEXT && text[n]) n++;
  if (text && n == HUM_MAX_TEXT && text[n]) {          /* cut at a character boundary */
    while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
  }
  if (n) memcpy(h->text, text, (size_t)n);
  h->text[n] = 0;
  h->len = (int16_t)n;
}

void hum_begin(Hum *h, const char *text, int mood) {
  copy_text(h, text);
  reset(h, h32(h->text, h->len));                      /* render.py: seed = h32(text) */
  h->mood = (uint8_t)((mood >= 0 && mood < HUM_MOODS) ? mood : HUM_CALM);
  h->mode = 0;
  h->have_nxt = (uint8_t)gen_next(h, &h->nxt);
  if (!h->have_nxt) h->finished = 1;
}

static void begin_gesture(Hum *h, int mode) {
  h->text[0] = 0; h->len = 0;
  reset(h, GESTURE_SEED);
  h->mood = HUM_CALM;
  h->mode = (uint8_t)mode;
  h->have_nxt = (uint8_t)gen_next(h, &h->nxt);
}

void hum_open(Hum *h)  { begin_gesture(h, 1); }
void hum_close(Hum *h) { begin_gesture(h, 2); }

void hum_hush(Hum *h) {
  h->hushed = 1;
  h->have_nxt = 0;                                     /* the rest of the plan is dropped */
}

int hum_shown(const Hum *h) {
  return (h->finished || h->hushed) ? h->len : h->shown;
}

float hum_level(const Hum *h) { return h->level; }

int hum_render(Hum *h, int16_t *out, int n) {
  int w = 0;
  float lvl = 0.0f;
  while (w < n && !h->finished) {
    const uint32_t i = h->pos;
    float tms, target = 0.0f, p_t, fc = 1000.0f, opn = 0.25f, breath = 0.0f, vib = 0.0f;
    float ka = KA_IDLE, click = 0.0f, ea = 0.0f, vhz = 5.2f, con = 0.0f, x, dx, f0, inc, hk, s, r, band, y, v;
    int k, nh, q;

    while (h->have_nxt && h->nxt.start <= i) start_next(h);
    tms = (float)i * 0.0625f;
    p_t = h->st;
    if (h->have_cur) {
      const HumEvent *e = &h->cur;
      const float dt = tms - e->t_ms, u = dt * e->inv_ms;
      vhz = e->vib_hz; ea = e->amp; con = e->console;
      if (u < 1.0f) {
        float uu = u * 1.15f;
        if (uu > 1.0f) uu = 1.0f;
        p_t = e->p0 + (e->p1 - e->p0) * uu;
        target = e->amp * (1.0f - 0.18f * u);          /* a held syllable sinks a little */
        if (e->release == 0.0f && u > 0.82f) target *= 0.35f;
        fc = e->fc + (e->fc1 - e->fc) * u;
        opn = e->open + (e->open1 - e->open) * u;
        breath = e->breath;
        if (dt > 80.0f && e->ms > 160.0f) vib = e->vib;
        ka = e->ka_on;
      } else {
        target = e->floor * e->amp;
        breath = e->breath * 0.5f;
        ka = e->ka_off;
        p_t = e->p1; fc = e->fc1; opn = e->open1;
      }
      if (e->scoop != 0.0f && u < 0.25f) p_t = e->p0 + e->scoop * (1.0f - u * 4.0f);
      if (e->hard && dt >= 0.0f && dt < 6.0f) click = 0.45f * (1.0f - dt * (1.0f / 6.0f));
    }
    if (h->hushed) { target = 0.0f; ka = KA_HUSH; click = 0.0f; }
    h->amp += (target - h->amp) * ka;
    h->st += (p_t - h->st) * KP;
    if ((i & 31u) == 0) h->jit += rng_pm(&h->rng) * 0.06f - h->jit * 0.02f;   /* a slow wander */
    h->vph += vhz * INV_SR;
    if (h->vph >= 1.0f) h->vph -= 1.0f;
    /* f0 = C4 * 2^(x/12) every sample, from an anchor moved when x strays half a semitone */
    x = h->st + h->jit + vib * tsin(h->vph);
    dx = x - h->x_anchor;
    if (dx > 0.5f || dx < -0.5f) { set_anchor(h, x); dx = 0.0f; }
    dx *= LN2_12;
    f0 = h->f_anchor * (1.0f + dx * (1.0f + dx * (0.5f + dx * (1.0f / 6.0f))));
    if ((i & 31u) == 0) harmonics(h, f0, fc, opn, con);
    inc = f0 * INV_SR;
    hk = inc; s = 0.0f; nh = h->nh;
    for (k = 0; k < nh; k++) {
      float p = h->ph[k] + hk;
      if (p >= 1.0f) p -= 1.0f;
      h->ph[k] = p;
      s += h->g[k] * tsin(p);
      hk += inc;
    }
    /* breath and click: white noise through a 1.0 to 3.2 kHz band */
    r = rng_pm(&h->rng);
    h->nz_lp += (r - h->nz_lp) * 0.72f;
    h->nz_hp += (h->nz_lp - h->nz_hp) * 0.33f;
    band = h->nz_lp - h->nz_hp;
    y = h->have_cur ? h->amp * (s + breath * 2.2f * band) + click * band * ea : 0.0f;
    h->lp_out += (y - h->lp_out) * 0.80f;             /* one-pole near 4 kHz */
    v = h->lp_out * MASTER * 32767.0f;
    q = (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
    if (q > 32767) q = 32767;
    if (q < -32768) q = -32768;
    out[w++] = (int16_t)q;
    if (h->amp > lvl) lvl = h->amp;
    h->pos++;
    if (!h->have_nxt && h->amp < DONE_AMP && (h->hushed || (float)h->pos * 0.0625f >= h->end_ms))
      h->finished = 1;
  }
  h->level = lvl > 1.0f ? 1.0f : lvl;
  return w;
}

int hum_debug_plan(Hum *h, const char *text, int mood, HumEvent *out, int max) {
  int count = 0;
  HumEvent e;
  hum_begin(h, text, mood);
  if (!h->have_nxt) return 0;
  e = h->nxt;
  do {
    if (count < max) out[count] = e;
    count++;
  } while (gen_next(h, &e));
  return count;
}
