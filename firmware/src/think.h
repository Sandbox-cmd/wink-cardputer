// Wink's own mind: a small neural network that lives in the chip.
// It reads everything that is said and everything that happens to the device as one
// unbroken stream, carries a state through all of it, and writes its own words.
#pragma once
#include "os.h"

namespace think {
struct Result {
  bool thought = false;   // a thought it had by itself, not an answer
  String text;
  Mood mood = CALM;
};

bool begin();                                  // load the mind, restore its state, start it
bool ready();
void hear(const String &text);                 // the person said this
void said(const String &text, Mood mood);      // Wink said this by other means (a written line, the whole mind): it should know
void event(const char *name);                  // something happened: pickup, shake, loud, quiet, night ...
void reply();                                  // answer what was heard; the result arrives through poll()
void muse();                                   // have a thought
bool poll(Result &r);
bool busy();                                   // reading or writing right now
String partial();                              // the words written so far of the line being made
const float *state();                          // the state: size() numbers between -1 and 1, changing as it reads and writes
int size();
uint32_t age();                                // pieces read and written since this mind first woke
float pace();                                  // milliseconds a piece, as last measured
void save();                                   // keep the state for the next waking
String about();                                // a line of facts for the body room and the cable
}
