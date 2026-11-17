// Wisp: Wink's body, a puff of 512 coral motes on warm paper, one mote riding each unit of
// its mind, holding the dot face's two dark eyes and a mouth.
//
// Ported from look/sketch2/index.html (makeWisp and the shared world, expression and eye
// code). Plain C++11: needs only <stdint.h>, <math.h>, <string.h>, so the same files build on
// the Mac (look/port/wisp_png.cpp) and on the ESP32-S3.
//
// The frame buffer: 240 x 135 uint16_t, RGB565 with the two bytes of each pixel swapped, as
// the M5 sprite keeps them. wisp_frame does NOT clear it: like the sketch, it fades every pixel
// one step toward the paper and draws on top, so the motes leave short trails. It therefore
// depends on the previous frame still being in the buffer. Words the firmware draws over the
// frame are taken back by the paper in about a third of a second; the text band is repainted
// every frame. When the buffer has held another screen, call wisp_repaint first (or let it
// dissolve: any pixel reaches the paper in 13 frames at rest, about 21 while shaken, and a
// buffer of garbage makes the most expensive frames this library has).
//
// For whoever draws the words: draw them opaque, with the colour already mixed against the paper
// (mix(paper (246,235,220), ink, alpha)), never alpha-blended with the buffer, because the frame
// is not cleared and a blend would add up frame after frame (an alpha of 0.5 settles near 0.77).
//
// Not thread-safe; one creature per program. No heap, no recursion.
#pragma once
#include <stdint.h>

#define WISP_W 240
#define WISP_H 135
#define WISP_MOTES 512

// the moods, in the order of the device's own Mood enum (os.h)
enum {
  WISP_CALM, WISP_HAPPY, WISP_DELIGHTED, WISP_CURIOUS, WISP_THINKING, WISP_SLEEPY,
  WISP_SURPRISED, WISP_SAD, WISP_CHEEKY, WISP_TENDER, WISP_ALERT,
  WISP_MOOD_COUNT,
  WISP_DIZZY = WISP_MOOD_COUNT,   // worn by itself after a shake
  WISP_ASLEEP                     // worn by itself while asleep
};

struct WispInput {
  float dt;              // seconds since the last frame (clamped to 0.1)
  const float *state;    // the mind's state: n numbers in -1..1, may be null
  int n;                 // 512 today. At 512 the model's own mean, spread, order and principal directions
                         // (wisp_mind.h) are used; any other n up to 512 falls back to a running mean
  uint8_t mood;          // WISP_CALM..WISP_ALERT: the mood it rests in
  bool asleep;           // asleep: settles flat on the floor
  bool thinking;         // its own mind is writing: rises, wears "thinking" (pieces come by wisp_piece)
  bool listening;        // the room is being listened to: the motes stir with `room`
  bool whole;            // the whole mind (the Mac) is in reach: a voice opens the mouth instead of humming
  bool held;             // picked up: lifted, trembling, alert            (added to the suggested interface)
  float voice;           // 0..1 loudness of whatever is sounding now
  float room;            // 0..1 loudness of the room
  float tiltX, tiltY;    // gravity across / down the screen, -1..1 (tiltY is not used: the sketch has no use for it)
  float lookX, lookY;    // a point to attend to (the typing cursor) ...
  bool looking;          // ... while this is true: it floats beside the point and its eyes go to it
                         // (the sketch keeps attending for 3 s after the last key: hold it true that long)
  float textRows;        // pixels at the bottom reserved for words; the band is painted, the puff keeps clear
};

void wisp_begin(uint32_t seed);
void wisp_repaint(uint16_t *buf);                 // fill the whole buffer with paper
void wisp_frame(uint16_t *buf, const WispInput &in);   // advance by in.dt and draw everything except text

// things that happen once
void wisp_blink();
void wisp_wake();                                 // the coming-together: motes fly in from all over the paper, eyes open, delighted
void wisp_scatter(float strength);                // shaken (1 = the sketch's shake): scatters for 1.3 s, surprised, then dizzy
void wisp_bristle();                              // a loud sound: puffs out, surprised
void wisp_piece(float effort = 0.5f);             // its mind just wrote or read one piece; effort 0..1 sets the ring's darkness
void wisp_word_in(bool fromMac);                  // a word arrives: 5 blue motes from the cursor, or 2 ink-violet motes from the top
void wisp_poke(float x, float y, float strength); // a key pressed near x,y: nearby motes flinch away
void wisp_flash(uint8_t mood, float seconds);     // wear a passing expression, then back (as face::flash)

// where things are, for the words the firmware draws
void wisp_center(float *x, float *y);             // the puff's centre
void wisp_head(float *x, float *y);               // the top of the puff: where thoughts are born and z marks rise from
float wisp_floor();                               // the y the puff sleeps on (top of the text band, less 2)
// a drifting thought born at the head (bx, by), `age` seconds old, with `lines` lines of 10 px:
// where to centre its first line (x) and its top (y), as the sketch places it
void wisp_thought_at(float bx, float by, float age, int lines, float *x, float *y);

struct WispMark {
  float x, y;            // top-left of the glyph
  float alpha;           // 0..1 over the paper
  uint8_t px;            // font size in pixels: 8 or 11
};
int wisp_zs(WispMark *out, int max);              // the sleeping "z" marks (3 while asleep, else 0); draw each as the letter z

uint16_t wisp_paper(int x, int y);                // the ground at a pixel, byte-swapped, as wisp_frame paints it
uint8_t wisp_wearing();                           // the expression shown now: a WISP_ mood, WISP_DIZZY or WISP_ASLEEP
uint16_t wisp_tint();                             // the current mood tint (RGB565, byte-swapped), for words that belong to it
float wisp_restless();                            // 0..1, how far the mind is from its rest

// the library's working memory, for the record
unsigned wisp_state_bytes();

// counts of the work done by the last frame (for the cost check on the Mac)
struct WispCost {
  uint32_t paperFast;    // pixels already at paper (one compare)
  uint32_t faded;        // pixels stepped toward paper
  uint32_t blends;       // pixel blends (shadow, motes, eyes, mouth)
  uint32_t shapePx;      // pixels whose distance to an eye, the mouth or the shadow was evaluated
  uint32_t bandPx;       // pixels of the text band painted
  uint32_t motes;        // motes and extra motes moved
};
WispCost wisp_cost();
void wisp_mote_centroid(float *x, float *y);      // mean position of the 512 motes (for the drift check)
