// Wink's body on screen: Wisp, a floating puff of coral motes on warm paper, one mote
// riding each unit of its own mind, holding two dark eyes and a mouth (lib/wisp).
// This module keeps the calls the rest of the system has always made and feeds Wisp.
#pragma once
#include "os.h"

namespace face {
void begin();
void setMood(Mood m);                  // the mood it rests in
void flash(Mood m, uint32_t ms);       // a passing expression, then back
Mood mood();                           // the expression worn now
void setWhole(bool whole);             // the whole mind is here
bool whole();
void setAsleep(bool a);
bool asleep();
void setThinking(bool t);              // its own mind is writing
void setListening(bool l);             // sound is going to the Mac
void setTextRows(float px);            // pixels at the bottom given over to words
void lookAt(float x, float y, uint32_t ms);
void poke(float x, float y, float strength);
void tilt(float x, float y);           // which way is down, -1..1
void held(bool h);                     // in a hand
void blink();
void wake();                           // replay its coming-together
void shake();                          // it was shaken
void bristle();                        // a loud sound
void piece(float effort);              // its mind read or wrote one piece
void wordIn(bool fromMac);             // a word arrives from you, or from the Mac
void repaint();                        // the screen held something else: lay fresh paper
void update(float dt, float voice, float roomLevel);
void draw(Gfx &g);                     // the whole screen, words excepted
void drawMini(Gfx &g, int x, int y);   // a small pair of eyes for the other rooms
uint16_t tint();                       // current colour of the creature, for words that belong to it
void head(float *x, float *y);         // where a thought is born
void thoughtAt(float age, int lines, float *x, float *y);   // where a drifting thought sits
int zs(int max, float *x, float *y, float *alpha, uint8_t *px);   // the sleeping z marks
}
