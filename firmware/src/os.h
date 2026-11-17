// Shared vocabulary of the system: moods, keys, apps, settings.
#pragma once
#include <M5Cardputer.h>

#define OS_VERSION "0.1"
#define SCREEN_W 240
#define SCREEN_H 135

typedef M5Canvas Gfx;

// ---- moods the face can wear and a line can carry -------------------------
enum Mood : uint8_t {
  CALM, HAPPY, DELIGHTED, CURIOUS, THINKING, SLEEPY, SURPRISED, SAD, CHEEKY, TENDER, ALERT, MOOD_COUNT
};
const char *moodName(Mood m);
Mood moodFromName(const char *s, Mood fallback = CALM);

// ---- keys ------------------------------------------------------------------
enum KeyCode : uint8_t { K_NONE, K_CHAR, K_ENTER, K_DEL, K_TAB, K_ESC, K_UP, K_DOWN, K_LEFT, K_RIGHT };
struct Key {
  KeyCode code = K_NONE;
  char ch = 0;
  bool fn = false, ctrl = false, opt = false, alt = false;
};

// ---- an app is a room the creature can be in --------------------------------
struct App {
  virtual const char *title() = 0;
  virtual void enter() {}
  virtual void leave() {}
  virtual void key(const Key &k) {}
  virtual void tick(float dt) {}
  virtual void draw(Gfx &g) = 0;
  // true when bare ; . , / should arrive as arrows (lists), false when they are text
  virtual bool arrowsBare() { return true; }
  virtual ~App() {}
};

namespace os {
void open(App *a);          // go to a room
void home();                // back to the face
App *current();
void say(const String &text, Mood mood, bool aloud = true, uint32_t stream = 0, uint32_t streamBytes = 0);   // it speaks, wherever it is
void hum(const String &text, Mood mood);    // words of its own making: shown, and hummed
void askOwn(const String &text);            // put a question to its own mind, whatever is in reach
void sayLine(const char *key, bool aloud = true);              // ... one of its own lines
void touched();             // any deliberate input: resets dimming and dozing
uint32_t idleMs();
float fps();
bool timeKnown();
String clockText();         // "16:10" or "" when unknown
String spokenTime();        // "ten past four"
void setClock(int hour, int minute);
void setEpoch(uint32_t epoch, int32_t tzOffsetSec);
uint32_t uptimeSec();
void injectKey(const Key &k);   // from the serial console
void applyBrightness();
void applyTurn();               // which way up the screen is
}

// ---- settings kept on the chip ---------------------------------------------
struct Settings {
  String name;          // the creature's name
  String wifiSsid, wifiPass;
  String token;         // shared secret with the Mac
  String host;          // optional fixed address of the Mac, else found by name
  uint16_t port = 8765;
  uint8_t volume = 7;       // 0..10
  uint8_t brightness = 7;   // 1..10
  uint8_t voiceSpeed = 72;  // SAM: lower is faster
  uint8_t voicePitch = 64;  // SAM: lower is higher
  uint8_t chatty = 1;       // 0 quiet, 1 some, 2 chatty
  uint8_t turn = 0;         // quarter turns added to the screen, if the board's own idea of up is wrong
  bool speak = true;        // speak aloud
  bool listen = true;       // react to sounds in the room
  bool hearName = true;     // with the Mac in reach, answer when called by name
  bool ownMind = true;      // alone, speak from its own mind (the network in the chip), not the written lines
  uint32_t boots = 0;
  void load();
  void save();
  void reset();
};
extern Settings cfg;

// ---- colours ---------------------------------------------------------------
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return lgfx::color565(r, g, b); }
// Wisp's world: warm paper, dark ink (look/port palette)
#define COL_BG rgb(246, 235, 220)      // paper
#define COL_BAND rgb(236, 222, 205)    // the band words sit on
#define COL_INK rgb(52, 38, 36)        // text
#define COL_DIM rgb(145, 103, 90)      // hints, secondary text
#define COL_FAINT rgb(176, 140, 124)   // the faintest text
#define COL_RULE rgb(220, 200, 182)    // rules and empty marks
#define COL_SEL rgb(230, 212, 194)     // a selected row
#define COL_CORAL rgb(222, 112, 76)    // Wink's coral
#define COL_YOU rgb(48, 98, 140)       // what you type
