/* Wink's little sounds. See chime.h. Written 17 November 2026.
 *
 * Each sound is a short table of notes. A note has a start and a length, a pitch that can
 * glide (semitones above C4, every fixed note on C major), a level, an attack and release,
 * a share of pulse-wave colour (the console), a spectral tilt that can move (dark to bright
 * is the hum's mouth opening), a little vibrato, and a breath of noise at its onset (chiff).
 * Notes alternate between two voices, so one rings out while the next begins.
 *
 * The sound of a voice is the hum's: up to 8 table-sine harmonics, each weighted by the
 * tilt, the hum's fixed 1650 Hz case resonance and the speaker high-pass at 420 Hz, mixed
 * with a 25% pulse wave's harmonics; then a one-pole low-pass near 4 kHz, as the hum ends.
 *
 * Per sample: two level slews, up to 16 table sines, a noise step, a filter. Per 16
 * samples: the pitch (one expf a voice) and the gains (expf only when the tilt moves).
 */
#include "chime.h"
#include <math.h>
#include <string.h>

#define C4_HZ   261.63f
#define LN2_12  5.776226505e-02f
#define INV_SR  (1.0f / 16000.0f)
#define FMAX    3800.0f
#define MASTER  0.40f              /* about Samantha's level through the speaker band; win and note a little over */
#define VIB_HZ  5.5f

struct ChimeNote {
  uint16_t t, ms;              /* start (ms from the sound's start) and length */
  float    p0, p1;             /* pitch at start and after the glide, st above C4 */
  uint16_t glide;              /* ms over which p0 moves to p1, from the note's start */
  float    scoop;              /* st below the pitch to start from, rising over 25 ms */
  float    amp;
  uint8_t  attack, release;    /* ms; at least 3 */
  float    console;            /* share of pulse-wave colour, 0..1 */
  float    tilt0, tilt1;       /* spectral tilt at start and end: 1.4 a closed mouth, 0.5 open */
  float    vib;                /* vibrato depth, st, from 60 ms in */
  float    chiff;              /* noise at the onset, over 8 ms */
};

/* notes of C major, semitones above C4 */
#define G4  7.0f
#define A4  9.0f
#define B4 11.0f
#define C5 12.0f
#define D5 14.0f
#define E5 16.0f
#define F5 17.0f
#define G5 19.0f
#define A5 21.0f
#define B5 23.0f
#define C6 24.0f
#define E6 28.0f

/*                   t    ms   p0  p1  glide scoop amp  atk rel console tilt0 tilt1 vib  chiff */

/* boot: the hum's mouth opening in one quick rise, a little arpeggio, a held top and a reply */
static const ChimeNote BOOT[] = {
  {   0, 140, G4, C5, 120, 0.0f, 0.60f, 10, 20, 0.30f, 1.40f, 0.80f, 0.00f, 0.00f },
  { 150,  60, E5, E5,   0, 0.0f, 0.70f,  4, 25, 0.50f, 0.80f, 0.80f, 0.00f, 0.25f },
  { 215,  60, G5, G5,   0, 0.0f, 0.75f,  4, 25, 0.50f, 0.80f, 0.80f, 0.00f, 0.25f },
  { 280, 280, C6, C6,   0, 1.0f, 1.00f,  5, 70, 0.45f, 0.80f, 0.60f, 0.15f, 0.10f },
  { 640,  45, G5, G5,   0, 0.0f, 0.40f,  3, 25, 0.50f, 0.80f, 0.80f, 0.00f, 0.20f },
  { 700, 200, C6, C6,   0, 0.0f, 0.55f,  4, 90, 0.45f, 0.70f, 0.70f, 0.10f, 0.00f },
};
/* the whole mind arrives: the shape of the hum's opening gesture, set in C (a closed G4 that
 * opens as it rises to C5; the hum's own version 2 gesture runs C5 to F5), then a sparkle up to C6 */
static const ChimeNote LINK_UP[] = {
  {   0, 420, G4, C5, 380, 0.0f, 0.85f, 15, 30, 0.20f, 1.40f, 0.60f, 0.10f, 0.00f },
  { 440,  55, E5, E5,   0, 0.0f, 0.60f,  4, 25, 0.45f, 0.70f, 0.70f, 0.00f, 0.20f },
  { 505,  55, G5, G5,   0, 0.0f, 0.65f,  4, 25, 0.45f, 0.70f, 0.70f, 0.00f, 0.20f },
  { 570, 200, C6, C6,   0, 0.0f, 0.80f,  4, 80, 0.45f, 0.60f, 0.60f, 0.12f, 0.00f },
};
/* the whole mind leaves: the same run backwards, closing as it falls home to G4 */
static const ChimeNote LINK_DOWN[] = {
  {   0,  60, C6, C6,   0, 0.0f, 0.70f,  4, 25, 0.45f, 0.60f, 0.60f, 0.00f, 0.20f },
  {  70,  60, G5, G5,   0, 0.0f, 0.65f,  4, 25, 0.45f, 0.70f, 0.70f, 0.00f, 0.15f },
  { 140, 420, C5, G4, 380, 0.0f, 0.85f,  8, 90, 0.20f, 0.60f, 1.50f, 0.00f, 0.00f },
};
/* a note arrived: two bright notes up a fourth, the second scooped */
static const ChimeNote NOTE[] = {
  {   0,  55, G5, G5,   0, 0.0f, 0.70f,  3, 25, 0.50f, 0.80f, 0.80f, 0.00f, 0.30f },
  {  70, 200, C6, C6,   0, 0.7f, 0.90f,  4, 70, 0.50f, 0.80f, 0.70f, 0.10f, 0.00f },
};
/* error: "uh-oh", the second note sagging from B4 to A4; buzzier, never harsh */
static const ChimeNote ERR[] = {
  {   0, 150, D5, D5,   0, 0.0f, 0.85f,  6, 25, 0.55f, 1.00f, 1.00f, 0.00f, 0.20f },
  { 190, 300, B4, A4, 280, 0.0f, 0.90f,  8, 80, 0.55f, 1.00f, 1.30f, 0.20f, 0.10f },
};
/* push to talk: a blip up to listen, a blip down when done */
static const ChimeNote PTT_ON[] = {
  {   0,  70, G5, C6,  50, 0.0f, 0.70f,  3, 25, 0.50f, 0.80f, 0.70f, 0.00f, 0.15f },
};
static const ChimeNote PTT_OFF[] = {
  {   0,  70, C6, G5,  50, 0.0f, 0.55f,  3, 25, 0.50f, 0.70f, 0.80f, 0.00f, 0.15f },
};
/* a key: very short, quiet, mostly the click */
static const ChimeNote TICK[] = {
  {   0,  14, E6, C6,  14, 0.0f, 0.30f,  3,  8, 0.60f, 0.80f, 0.80f, 0.00f, 0.60f },
};
/* sing: an original sixteen-note tune in C, sung in the hum's way (glides in, a scoop on
 * each downbeat, vibrato on the long notes); eighths of 150 ms */
static const ChimeNote TUNE[] = {
  {    0, 130, C5, C5,   0, 0.0f, 0.75f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  {  150, 130, G5, G5,   0, 0.0f, 0.70f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  {  300, 270, E5, E5,   0, 1.0f, 0.90f,  6, 40, 0.40f, 0.90f, 0.80f, 0.10f, 0.02f },
  {  600, 130, F5, F5,   0, 0.0f, 0.70f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  {  750, 130, A5, A5,   0, 0.0f, 0.70f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  {  900, 270, G5, G5,   0, 1.0f, 0.90f,  6, 40, 0.40f, 0.90f, 0.80f, 0.10f, 0.02f },
  { 1200, 130, E5, E5,   0, 0.0f, 0.70f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  { 1350, 130, D5, D5,   0, 0.0f, 0.65f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  { 1500, 130, E5, E5,   0, 0.0f, 0.75f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  { 1650, 130, C5, C5,   0, 0.0f, 0.65f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  { 1800, 270, D5, D5,   0, 1.0f, 0.85f,  6, 40, 0.40f, 0.90f, 0.80f, 0.10f, 0.02f },
  { 2100, 270, G4, G4,   0, 0.0f, 0.80f,  8, 40, 0.40f, 1.00f, 0.90f, 0.10f, 0.02f },
  { 2400, 130, C5, C5,   0, 0.0f, 0.75f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  { 2550, 130, E5, E5,   0, 0.0f, 0.75f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  { 2700, 130, G5, G5,   0, 0.0f, 0.80f,  6, 30, 0.40f, 0.90f, 0.90f, 0.00f, 0.04f },
  { 2850, 420, C6, C6,   0, 1.0f, 1.00f,  6, 90, 0.40f, 0.80f, 0.60f, 0.15f, 0.02f },
};
/* win: a sparkling run C5 E5 G5 C6 E6, then a bright held C6 */
static const ChimeNote WIN[] = {
  {   0,  40, C5, C5,   0, 0.0f, 0.60f,  3, 20, 0.50f, 0.80f, 0.80f, 0.00f, 0.20f },
  {  45,  40, E5, E5,   0, 0.0f, 0.65f,  3, 20, 0.50f, 0.80f, 0.80f, 0.00f, 0.20f },
  {  90,  40, G5, G5,   0, 0.0f, 0.70f,  3, 20, 0.50f, 0.80f, 0.80f, 0.00f, 0.20f },
  { 135,  40, C6, C6,   0, 0.0f, 0.75f,  3, 20, 0.50f, 0.80f, 0.80f, 0.00f, 0.20f },
  { 180,  60, E6, E6,   0, 0.0f, 0.70f,  3, 25, 0.50f, 0.70f, 0.70f, 0.00f, 0.15f },
  { 260, 420, C6, C6,   0, 0.5f, 1.00f,  5, 90, 0.45f, 0.60f, 0.50f, 0.20f, 0.00f },
};
/* lose: a soft descent E5, C5, then A4 sagging to G4 with a wide wobble, darkening */
static const ChimeNote LOSE[] = {
  {   0, 200, E5, E5,   0, 0.0f, 0.80f,  6, 30, 0.50f, 1.00f, 1.00f, 0.00f, 0.10f },
  { 230, 200, C5, C5,   0, 0.0f, 0.75f,  6, 30, 0.50f, 1.00f, 1.00f, 0.00f, 0.10f },
  { 460, 480, A4, G4, 400, 0.0f, 0.80f,  8, 100, 0.50f, 1.00f, 1.50f, 0.30f, 0.00f },
};

typedef struct { const ChimeNote *n; uint8_t count; } Sound;
#define S(x) { x, (uint8_t)(sizeof x / sizeof x[0]) }
static const Sound SOUNDS[CHIME_COUNT] = {
  S(BOOT), S(LINK_UP), S(LINK_DOWN), S(NOTE), S(ERR), S(PTT_ON), S(PTT_OFF), S(TICK), S(TUNE), S(WIN), S(LOSE)
};

/* ln k, k = 1..8 */
static const float LOGK[CHIME_HARM] = {
  0.000000000e+00f, 6.931471806e-01f, 1.098612289e+00f, 1.386294361e+00f,
  1.609437912e+00f, 1.791759469e+00f, 1.945910149e+00f, 2.079441542e+00f
};
/* a 25% pulse wave's harmonics, |sin(pi k / 4)| / k */
static const float PULSE[CHIME_HARM] = {
  7.071067812e-01f, 5.000000000e-01f, 2.357022604e-01f, 0.0f,
  1.414213562e-01f, 1.666666667e-01f, 1.010152545e-01f, 0.0f
};

/* sin(2 pi i / 256), i = 0..256: the same table as hum.c */
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

static float tsin(float ph) {                          /* ph in cycles, 0..1 */
  float x = ph * 256.0f;
  int i = (int)x;
  float f = x - (float)i;
  return SINT[i] + (SINT[i + 1] - SINT[i]) * f;
}

static float rng_pm(uint32_t *s) {                     /* xorshift32, uniform -1..1 */
  uint32_t x = *s;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  *s = x;
  return (float)x * 0x1p-31f - 1.0f;
}

static uint32_t note_end(const ChimeNote *n) {         /* samples, once the release has died (4 x release) */
  return ((uint32_t)n->t + n->ms + 4u * n->release + 2u) * 16u;
}

uint32_t chime_length(int which) {
  uint32_t end = 0;
  int i;
  if (which < 0 || which >= CHIME_COUNT) return 0;
  for (i = 0; i < SOUNDS[which].count; i++) {
    uint32_t e = note_end(&SOUNDS[which].n[i]);
    if (e > end) end = e;
  }
  return end;
}

void chime_start(ChimeSynth *c, int which) {
  memset(c, 0, sizeof *c);
  c->rng = 0x9E3779B9u;
  c->v[0].tilt = c->v[1].tilt = -1.0f;
  if (which < 0 || which >= CHIME_COUNT) { c->done = 1; return; }
  c->notes = SOUNDS[which].n;
  c->count = SOUNDS[which].count;
  c->end = chime_length(which);
}

static float slew_k(float ms) {                        /* one-pole coefficient for tau = ms / 3 */
  float tau = ms / 3.0f;
  if (tau < 0.5f) tau = 0.5f;
  return 1.0f - expf(-1.0f / (16.0f * tau));
}

static void begin_note(ChimeSynth *c, const ChimeNote *n) {
  ChimeVoice *v = &c->v[c->next & 1];
  v->n = n;
  v->start = (uint32_t)n->t * 16u;
  v->stop = v->start + (uint32_t)n->ms * 16u;
  v->ka_on = slew_k((float)n->attack);
  v->ka_off = slew_k((float)n->release);
  v->inv_len = 1.0f / (float)(n->ms * 16u);
  v->on = 1;
  c->next++;
}

/* pitch and harmonic gains of a voice, every 16 samples */
static float voice_f0(ChimeSynth *c, ChimeVoice *v, float *tilt) {
  const ChimeNote *n = v->n;
  float ms = (float)(c->pos - v->start) * 0.0625f, p, u;
  if (n->glide > 0 && ms < (float)n->glide) p = n->p0 + (n->p1 - n->p0) * ms / (float)n->glide;
  else p = n->glide > 0 ? n->p1 : n->p0;
  if (n->scoop > 0.0f && ms < 25.0f) p -= n->scoop * (1.0f - ms * (1.0f / 25.0f));
  if (n->vib > 0.0f && ms > 60.0f) p += n->vib * tsin(c->vph);
  u = ms / (float)n->ms;
  if (u > 1.0f) u = 1.0f;
  *tilt = n->tilt0 + (n->tilt1 - n->tilt0) * u;
  return C4_HZ * expf(p * LN2_12);
}

static void voice_gains(ChimeVoice *v, float f0, float tilt) {
  float tot = 0.0f, ptot = 0.0f, pw[CHIME_HARM], it, ip, con = v->n->console;
  int k, nh = 0;
  if (tilt != v->tilt) {
    for (k = 0; k < CHIME_HARM; k++) v->kpow[k] = expf(-tilt * LOGK[k]);
    v->tilt = tilt;
  }
  for (k = 0; k < CHIME_HARM; k++) {
    float f = f0 * (float)(k + 1), f2, hp, d2, a;
    if (f > FMAX) break;
    f2 = f * f;
    hp = f2 / (f2 + 176400.0f);                        /* the speaker high-pass, 420 Hz */
    if (f > FMAX - 400.0f) hp *= (FMAX - f) * (1.0f / 400.0f);   /* a harmonic fades in as a glide brings it under 3.8 kHz: no click */
    d2 = (f - 1650.0f) * (1.0f / 320.0f);              /* the hum's case resonance */
    a = v->kpow[k] * (1.0f + 0.9f / (1.0f + d2 * d2)) * hp;
    v->g[k] = a; tot += a;
    pw[k] = PULSE[k] * hp; ptot += pw[k];
    nh++;
  }
  it = tot > 0.0f ? (1.0f - con) / tot : 0.0f;
  ip = ptot > 0.0f ? con / ptot : 0.0f;
  for (k = 0; k < nh; k++) v->g[k] = v->g[k] * it + pw[k] * ip;
  for (; k < CHIME_HARM; k++) v->g[k] = 0.0f;
  v->nh = (uint8_t)nh;
}

int chime_render(ChimeSynth *c, int16_t *out, int n) {
  int w = 0;
  while (w < n && !c->done) {
    float y = 0.0f, band, r, val;
    int j, q;
    while (c->next < c->count && (uint32_t)c->notes[c->next].t * 16u <= c->pos) begin_note(c, &c->notes[c->next]);
    c->vph += VIB_HZ * INV_SR;
    if (c->vph >= 1.0f) c->vph -= 1.0f;
    r = rng_pm(&c->rng);
    c->nz_lp += (r - c->nz_lp) * 0.72f;                /* the hum's breath band, 1 to 3.2 kHz */
    c->nz_hp += (c->nz_lp - c->nz_hp) * 0.33f;
    band = c->nz_lp - c->nz_hp;
    for (j = 0; j < 2; j++) {
      ChimeVoice *v = &c->v[j];
      const ChimeNote *nt = v->n;
      const uint32_t dt = c->pos - v->start;
      float target, s = 0.0f, inc, hk;
      int k;
      if (!v->on) continue;
      if ((dt & 15u) == 0) {                           /* pitch and gains, every 1 ms */
        float tilt;
        v->f0 = voice_f0(c, v, &tilt);
        voice_gains(v, v->f0, tilt);
      }
      if (c->pos < v->stop) {
        target = nt->amp * (1.0f - 0.25f * (float)dt * v->inv_len);   /* a held note sinks a little */
        v->amp += (target - v->amp) * v->ka_on;
      } else {
        v->amp += (0.0f - v->amp) * v->ka_off;
        if (v->amp < 1e-5f) { v->on = 0; v->amp = 0.0f; continue; }
      }
      inc = v->f0 * INV_SR;
      hk = inc;
      for (k = 0; k < v->nh; k++) {
        float p = v->ph[k] + hk;
        if (p >= 1.0f) p -= 1.0f;
        v->ph[k] = p;
        s += v->g[k] * tsin(p);
        hk += inc;
      }
      y += v->amp * s;
      if (nt->chiff > 0.0f && dt < 128u)               /* a breath of noise at the onset, 8 ms */
        y += nt->chiff * (1.0f - (float)dt * (1.0f / 128.0f)) * band * nt->amp;
    }
    c->lp += (y - c->lp) * 0.80f;                      /* one-pole near 4 kHz, as the hum */
    val = c->lp * MASTER * 32767.0f;
    q = (int)(val >= 0.0f ? val + 0.5f : val - 0.5f);
    if (q > 32767) q = 32767;
    if (q < -32768) q = -32768;
    out[w++] = (int16_t)q;
    c->pos++;
    if (c->pos >= c->end) c->done = 1;
  }
  return w;
}
