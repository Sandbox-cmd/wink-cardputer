#include "face.h"
#include "think.h"
#include "wisp.h"

namespace {
WispInput in;
Mood baseMood = CALM;
bool isAsleep = false, isWhole = false, isThinking = false, isListening = false, isHeld = false;
uint32_t lookUntil = 0, lastAge = 0;
bool needPaper = true;

inline uint16_t unswap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }
}  // namespace

void face::begin() {
  wisp_begin(esp_random());
  memset(&in, 0, sizeof(in));
  in.mood = CALM;
  wisp_wake();
}

void face::setMood(Mood m) { baseMood = m; }
void face::flash(Mood m, uint32_t ms) { wisp_flash((uint8_t)m, ms / 1000.0f); }
Mood face::mood() {
  uint8_t w = wisp_wearing();
  if (w == WISP_ASLEEP) return SLEEPY;
  if (w == WISP_DIZZY) return SURPRISED;
  return w < MOOD_COUNT ? (Mood)w : baseMood;
}
void face::setWhole(bool w) { isWhole = w; }
bool face::whole() { return isWhole; }
void face::setAsleep(bool a) { isAsleep = a; }
bool face::asleep() { return isAsleep; }
void face::setThinking(bool t) { isThinking = t; }
void face::setListening(bool l) { isListening = l; }
void face::setTextRows(float px) { in.textRows = px; }
void face::lookAt(float x, float y, uint32_t ms) {
  in.lookX = x;
  in.lookY = y;
  uint32_t until = millis() + max(ms, (uint32_t)3000);   // Wisp keeps attending a while after the last key
  if (until > lookUntil) lookUntil = until;
}
void face::poke(float x, float y, float strength) { wisp_poke(x, y, strength); }
void face::tilt(float x, float y) {
  in.tiltX += (x - in.tiltX) * 0.15f;
  in.tiltY += (y - in.tiltY) * 0.15f;
}
void face::held(bool h) { isHeld = h; }
void face::blink() { wisp_blink(); }
void face::wake() { wisp_wake(); }
void face::shake() { wisp_scatter(1.0f); }
void face::bristle() { wisp_bristle(); }
void face::piece(float effort) { wisp_piece(effort); }
void face::wordIn(bool fromMac) { wisp_word_in(fromMac); }
void face::repaint() { needPaper = true; }

void face::update(float dt, float voice, float room) {
  in.dt = dt;
  in.voice = voice;
  in.room = room;
}

void face::draw(Gfx &g) {
  uint16_t *buf = (uint16_t *)g.getBuffer();
  if (needPaper) {
    wisp_repaint(buf);
    needPaper = false;
  }
  // its own mind moves its body: the state, and a stir for every piece it has read or written
  in.state = think::state();
  in.n = think::size();
  uint32_t age = think::age();
  if (age != lastAge) {
    uint32_t pieces = age - lastAge;
    if (lastAge && pieces < 64)
      for (uint32_t i = 0; i < min(pieces, (uint32_t)4); i++) wisp_piece(0.5f);
    lastAge = age;
  }
  in.mood = (uint8_t)baseMood;
  in.asleep = isAsleep;
  in.thinking = isThinking;
  in.listening = isListening;
  in.whole = isWhole;
  in.held = isHeld && !isAsleep;
  in.looking = millis() < lookUntil;
  wisp_frame(buf, in);
}

void face::drawMini(Gfx &g, int x, int y) {
  // the two dark eyes, small, for the corner of another room
  uint16_t ink = rgb(52, 38, 36);
  for (int i = 0; i < 2; i++) g.fillRoundRect(x + i * 10, y, 7, 9, 2, ink);
  g.drawPixel(x + 1, y + 1, rgb(246, 235, 220));
  g.drawPixel(x + 11, y + 1, rgb(246, 235, 220));
}

uint16_t face::tint() { return unswap(wisp_tint()); }

void face::head(float *x, float *y) { wisp_head(x, y); }
void face::thoughtAt(float age, int lines, float *x, float *y) {
  float bx, by;
  wisp_head(&bx, &by);
  wisp_thought_at(bx, by, age, lines, x, y);
}
int face::zs(int max, float *x, float *y, float *alpha, uint8_t *px) {
  WispMark m[4];
  int n = wisp_zs(m, max < 4 ? max : 4);
  for (int i = 0; i < n; i++) {
    x[i] = m[i].x;
    y[i] = m[i].y;
    alpha[i] = m[i].alpha;
    px[i] = m[i].px;
  }
  return n;
}
