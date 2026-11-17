// What the rooms share with the main loop.
#pragma once
#include "os.h"
#include <ArduinoJson.h>

struct Speech {
  String text;
  Mood mood = CALM;
  bool aloud = false;
  bool active = false;       // there is something being said or still showing
  bool started = false;
  uint32_t start = 0, utterAtQueue = 0, doneAt = 0;
};

namespace os {
Speech &speech();
int speechShown();           // how many characters of the speech are out so far
void ask(const String &text, const char *via);   // you said something: whole mind if here, else the small one
bool waiting();              // the whole mind is thinking
const char *waitText();      // what to show meanwhile
bool listeningPtt();
String heard();              // last words the Mac heard you say (shown briefly)
bool ownThinking();          // its own mind is writing an answer
String thought(float &life); // what it is thinking to itself just now, and how far through its showing (0..1)
void quiet();                // stop talking, forget the speech on screen
Gfx &canvas();
}

namespace apps {
App *home();
App *rooms();
App *terminal();
App *notes();
App *files();
App *clockRoom();
App *game();
App *body();
App *settings();
App *memory();
void showCard(const String &title, const String &kind, const String &body);   // a screen the whole mind composed
void setTimer(uint32_t ms, const String &say);   // 0 cancels
int timerLeft();                                 // seconds, or -1 when none runs
void background();   // timers and the like, whichever room is open
}

namespace shell {
void exec(const String &line, Print &out);   // the command language, shared by the Terminal room and the USB cable
}

namespace ops {
String run(const String &requestLine);   // one act asked for by the whole mind; returns the reply line
void situation(JsonDocument &d);         // what is going on, attached to what you say
void noteEvent(const char *name);        // a pick-up, a shake, a loud sound
void noteMoved();
void noteSaid();
}

namespace sense {
// motion, read by the main loop
extern float ax, ay, az;
extern bool hasImu;
extern uint32_t buttonPresses;
}
