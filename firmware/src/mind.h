// The small mind: the few hundred reflexes that fit in the chip.
#pragma once
#include "os.h"

struct Line {
  const char *say;
  Mood mood;
};

namespace mind {
bool has(const char *key);
Line pick(const char *key);                 // one line of a situation, never the same twice running
String fill(const char *tmpl, long n = 0);  // {time} {pct} {hours} {name} {n}
struct Answer {
  String say;
  Mood mood = CALM;
  bool sing = false;
  bool unknown = false;  // nothing matched
  bool grounded = false; // a fact the device itself holds: the time, the battery, a sum, a packed answer
  bool quiet = false;    // better shown than spoken into a room
};
Answer answer(const String &typed);         // what the small mind makes of a typed line
}
