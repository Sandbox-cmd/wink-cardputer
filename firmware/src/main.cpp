// The main loop: keys in, the creature's state forward, one frame out.
#include "kernel.h"
#include "face.h"
#include "link.h"
#include "mind.h"
#include "ui.h"
#include "voice.h"
#include "think.h"
#include <LittleFS.h>
#include <sys/time.h>

fs::FS *pathFs(String &path);   // shell.cpp
#include "mbedtls/base64.h"

static M5Canvas cv(&M5Cardputer.Display);
static App *cur = nullptr;
static Speech sp;
static uint32_t lastTouch = 0;
static float fpsAvg = 30;
static bool timeIsKnown = false;
static int32_t tzOff = 0;

static bool waitingReply = false;
static uint32_t waitStart = 0, waitLineAt = 0;
static String waitLine;
static String heardText;
static uint32_t heardAt = 0;
static bool pttHeld = false;
static uint32_t btnDownAt = 0;
static int dimLevel = 0;   // 0 awake, 1 dimmed, 2 dozing

// its own mind
static bool ownPending = false;      // it is writing an answer
static uint32_t ownStart = 0;
static String thoughtText;           // what it last thought to itself
static uint32_t thoughtAt = 0, nextThought = 0, lastSave = 0;
static const uint32_t THOUGHT_MS = 9000;

namespace sense {
float ax = 0, ay = 0, az = 1;
uint32_t buttonPresses = 0;
bool hasImu = false;
}

// ---------------------------------------------------------------- os --------
Gfx &os::canvas() { return cv; }
App *os::current() { return cur; }
float os::fps() { return fpsAvg; }
uint32_t os::idleMs() { return millis() - lastTouch; }
uint32_t os::uptimeSec() { return millis() / 1000; }
Speech &os::speech() { return sp; }
bool os::waiting() { return waitingReply; }
const char *os::waitText() { return waitLine.c_str(); }
bool os::listeningPtt() { return pttHeld; }
String os::heard() { return millis() - heardAt < 6000 ? heardText : String(); }
bool os::ownThinking() { return ownPending; }
String os::thought(float &life) {
  uint32_t age = millis() - thoughtAt;
  if (!thoughtAt || age > THOUGHT_MS) return String();
  life = age / (float)THOUGHT_MS;
  return thoughtText;
}

void os::applyBrightness() {
  int b = cfg.brightness * 25;
  if (dimLevel == 1) b = max(8, b / 4);
  if (dimLevel == 2) b = 6;
  M5Cardputer.Display.setBrightness(min(255, b));
}

// The board library knows which way up this model's panel is; this only makes sure the
// long side is across, and adds any quarter turns asked for in Settings.
static int baseRotation = -1;
void os::applyTurn() {
  auto &d = M5Cardputer.Display;
  if (baseRotation < 0) baseRotation = d.getRotation();
  d.setRotation(baseRotation & 3);
  if (d.width() < d.height()) d.setRotation((baseRotation + 1) & 3);
  if (cfg.turn) d.setRotation((d.getRotation() + cfg.turn) & 3);
}

void os::touched() {
  lastTouch = millis();
  if (dimLevel) {
    dimLevel = 0;
    applyBrightness();
  }
}

void os::open(App *a) {
  if (!a || a == cur) return;
  if (cur) cur->leave();
  cur = a;
  cur->enter();
}
void os::home() { open(apps::home()); }

void os::quiet() {
  voice::hush();
  sp.active = false;
}

void os::say(const String &text, Mood mood, bool aloud, uint32_t stream, uint32_t streamBytes) {
  if (!text.length()) {
    if (streamBytes) voice::streamDrop(stream);
    return;
  }
  voice::hush();
  sp.text = text;
  sp.mood = mood;
  sp.aloud = aloud && cfg.speak && cfg.volume > 0;
  sp.active = true;
  sp.started = false;
  sp.start = millis();
  sp.doneAt = 0;
  sp.utterAtQueue = voice::utterance();
  if (sp.aloud && streamBytes) voice::sayStream(text, stream, streamBytes);   // the Mac sent the sound
  else if (sp.aloud) voice::say(text);
  else if (streamBytes) voice::streamDrop(stream);
  face::flash(mood, 2500 + text.length() * 90);
}

void os::hum(const String &text, Mood mood) {
  if (!text.length()) return;
  voice::hush();
  sp.text = text;
  sp.mood = mood;
  sp.aloud = cfg.speak;   // with the sound off the words still appear, on a reading clock
  sp.active = true;
  sp.started = false;
  sp.start = millis();
  sp.doneAt = 0;
  sp.utterAtQueue = voice::utterance();
  if (sp.aloud) voice::hum(text, mood);
  face::flash(mood, 2500 + text.length() * 90);
}

void os::sayLine(const char *key, bool aloud) {
  if (!mind::has(key)) return;
  Line l = mind::pick(key);
  say(mind::fill(l.say), l.mood, aloud);
}

int os::speechShown() {
  if (!sp.active) return 0;
  int len = sp.text.length(), shown;
  if (sp.aloud) {
    if (!sp.started && voice::utterance() != sp.utterAtQueue) sp.started = true;
    if (!sp.started) shown = (millis() - sp.start > 2500) ? len : 0;   // the voice never came: show it anyway
    else shown = voice::speaking() ? min(len, voice::spokenChars()) : len;
  } else {
    shown = min(len, (int)((millis() - sp.start) * 0.034f));
  }
  if (shown >= len && !sp.doneAt) sp.doneAt = millis();
  return shown;
}

void os::askOwn(const String &text) {
  think::hear(text);
  quiet();
  ownPending = true;
  ownStart = millis();
  thoughtAt = 0;
  face::setThinking(true);
  think::reply();
}

void os::ask(const String &text, const char *via) {
  if (!text.length()) return;
  think::hear(text);   // its own mind hears everything said to it, whoever answers
  if (reach::up()) {
    quiet();
    reach::send(text, via);
    waitingReply = true;
    waitStart = millis();
    waitLineAt = 0;
    waitLine = "";
    face::setThinking(true);
    return;
  }
  mind::Answer a = mind::answer(text);
  if (cfg.ownMind && think::ready() && !a.grounded) {   // alone: it answers from its own mind
    quiet();
    ownPending = true;
    ownStart = millis();
    thoughtAt = 0;
    face::setThinking(true);
    think::reply();
    return;
  }
  think::said(a.say, a.mood);
  if (a.unknown) {   // keep the question, so the whole mind can pack an answer next time it is in reach
    File f = LittleFS.open("/asked.txt", FILE_APPEND);
    if (f && f.size() < 1500) f.println(text.substring(0, 100));
    if (f) f.close();
  }
  say(a.say, a.mood, !a.quiet);
  if (a.sing) voice::chime(CH_TUNE);
}

bool os::timeKnown() { return timeIsKnown; }
void os::setEpoch(uint32_t epoch, int32_t tzOffsetSec) {
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  tzOff = tzOffsetSec;
  timeIsKnown = true;
}
void os::setClock(int hour, int minute) { setEpoch(1794787200UL + hour * 3600 + minute * 60, 0); }

static void localNow(int &h, int &m, int &s) {
  time_t now = time(nullptr) + tzOff;
  struct tm tmv;
  gmtime_r(&now, &tmv);
  h = tmv.tm_hour;
  m = tmv.tm_min;
  s = tmv.tm_sec;
}
String os::clockText() {
  if (!timeIsKnown) return "";
  int h, m, s;
  localNow(h, m, s);
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", h, m);
  return b;
}
String os::spokenTime() {
  if (!timeIsKnown) return "";
  int h, m, s;
  localNow(h, m, s);
  auto hour = [](int hh) {
    hh %= 12;
    return voice::words(hh == 0 ? 12 : hh);
  };
  if (m == 0) return hour(h) + " o'clock";
  if (m == 15) return "quarter past " + hour(h);
  if (m == 30) return "half past " + hour(h);
  if (m == 45) return "quarter to " + hour(h + 1);
  if (m < 30) return voice::words(m) + " past " + hour(h);
  return voice::words(60 - m) + " to " + hour(h + 1);
}

// ---------------------------------------------------------------- keys ------
static std::vector<char> prevWord;
static bool prevDel = false, prevEnter = false, prevTab = false;
static Key repeatKey;
static uint32_t repeatAt = 0;

static void deliver(const Key &k) {
  voice::voxHoldOff(700);   // the click of a key is not someone speaking
  bool wasDozing = dimLevel == 2;
  os::touched();
  if (wasDozing && face::asleep()) {   // the first key only wakes it
    face::setAsleep(false);
    os::sayLine("wake", cfg.chatty > 0);
    return;
  }
  if (!cur) return;
  if (k.code == K_TAB && cur == apps::home()) {
    os::open(apps::rooms());
    return;
  }
  cur->key(k);
}

void os::injectKey(const Key &k) { deliver(k); }

static Key mapChar(char c, const Keyboard_Class::KeysState &st) {
  Key k;
  k.fn = st.fn;
  k.ctrl = st.ctrl;
  k.opt = st.opt;
  k.alt = st.alt;
  if (c == '`') {   // the top-left key always means back
    k.code = K_ESC;
    return k;
  }
  if (st.fn || (cur && cur->arrowsBare())) {
    switch (c) {
      case ';': case ':': k.code = K_UP; return k;
      case '.': case '>': k.code = K_DOWN; return k;
      case ',': case '<': k.code = K_LEFT; return k;
      case '/': case '?': k.code = K_RIGHT; return k;
      case '~': k.code = K_ESC; return k;
    }
  }
  k.code = K_CHAR;
  k.ch = c;
  return k;
}

// holding the talk button: the microphone goes to the whole mind until it is let go
static bool talkStart() {
  if (!reach::up()) {
    os::sayLine("sound.voice_offline");
    return false;
  }
  pttHeld = true;
  os::quiet();
  reach::pttBegin();
  return true;
}
static void talkStop() {
  pttHeld = false;
  reach::pttEnd();
  waitingReply = true;
  waitStart = millis();
  waitLine = "";
  waitLineAt = 0;
  face::setThinking(true);
}

static void readKeys() {
  auto &kb = M5Cardputer.Keyboard;
  uint32_t now = millis();
  if (kb.isChange()) {
    auto &st = kb.keysState();
    Key last;
    std::vector<char> rest = prevWord;
    for (char c : st.word) {
      auto it = std::find(rest.begin(), rest.end(), c);
      if (it != rest.end()) {
        rest.erase(it);   // still held from before
        continue;
      }
      last = mapChar(c, st);
      deliver(last);
    }
    auto special = [&](bool nowDown, bool &was, KeyCode code) {
      if (nowDown && !was) {
        last = Key();
        last.code = code;
        last.fn = st.fn;
        last.ctrl = st.ctrl;
        deliver(last);
      }
      was = nowDown;
    };
    special(st.del, prevDel, K_DEL);
    special(st.enter, prevEnter, K_ENTER);
    special(st.tab, prevTab, K_TAB);
    prevWord = st.word;
    if (last.code != K_NONE && last.code != K_ENTER && last.code != K_TAB && last.code != K_ESC) {
      repeatKey = last;
      repeatAt = now + 420;
    } else if (!kb.isPressed()) {
      repeatKey = Key();
    }
  } else if (kb.isPressed() && repeatKey.code != K_NONE && now >= repeatAt) {
    deliver(repeatKey);
    repeatAt = now + 55;
  }
  if (!kb.isPressed()) {
    repeatKey = Key();
    prevWord.clear();
    prevDel = prevEnter = prevTab = false;
  }

  // the button on the edge: hold to speak to the whole mind, tap to hush
  if (M5Cardputer.BtnA.wasPressed()) {
    sense::buttonPresses++;
    btnDownAt = now;
    os::touched();
    if (face::asleep()) face::setAsleep(false);
    if (reach::up()) voice::pttStart();   // start hearing at once, so the first word is not lost
  }
  if (M5Cardputer.BtnA.isPressed() && !pttHeld && btnDownAt && now - btnDownAt > 220) {
    if (!talkStart()) btnDownAt = 0;
  }
  if (M5Cardputer.BtnA.wasReleased()) {
    if (pttHeld) {
      talkStop();
    } else if (btnDownAt && now - btnDownAt <= 220) {
      voice::pttStop();   // it was only a tap
      os::quiet();
    }
    btnDownAt = 0;
  }
}

// ---------------------------------------------------------------- the link --
static void linkEvents() {
  reach::Event e;
  while (reach::poll(e)) {
    switch (e.type) {
      case reach::E_WIFI_UP: os::sayLine("wifi.up", false); break;
      case reach::E_WIFI_FAIL: os::sayLine("wifi.fail", false); break;
      case reach::E_LINK_UP: {
        think::event("mac_arrives");
        File asked = LittleFS.open("/asked.txt");   // what the small mind could not answer while alone
        if (asked && asked.size() > 0) {
          JsonDocument d;
          d["t"] = "asked";
          JsonArray qs = d["questions"].to<JsonArray>();
          while (asked.available() && qs.size() < 20) {
            String q = asked.readStringUntil('\n');
            q.trim();
            if (q.length()) qs.add(q);
          }
          asked.close();
          String out;
          serializeJson(d, out);
          reach::sendLine(out);
          LittleFS.remove("/asked.txt");
        }
      }
        face::setWhole(true);
        voice::gesture(true);   // its mouth opens: the whole mind can speak
        os::sayLine("link.up");
        os::touched();
        break;
      case reach::E_LINK_DOWN:
        think::event("mac_leaves");
        face::setWhole(false);
        face::setThinking(false);
        waitingReply = false;
        voice::gesture(false);
        os::sayLine("link.down");
        break;
      case reach::E_REPLY:
        waitingReply = false;
        face::setThinking(false);
        os::touched();
        if (face::asleep()) face::setAsleep(false);
        os::say(e.text, e.mood, e.aloud, e.stream, e.bytes);
        think::said(e.text, e.mood);
        for (int i = 0, w = 0; i < (int)e.text.length() && w < 6; i++)   // the Mac's words fall in
          if (i == 0 || e.text[i] == ' ') { face::wordIn(true); w++; }   // its own mind hears what the whole mind said through it
        break;
      case reach::E_MOOD: face::setMood(e.mood); break;
      case reach::E_HEARD:
        heardText = e.text;
        heardAt = millis();
        if (e.aloud) {   // it was called by name, hands-free: the whole mind is answering
          os::touched();
          if (face::asleep()) face::setAsleep(false);
          os::quiet();
          ops::noteSaid();
          waitingReply = true;
          waitStart = millis();
          waitLine = "";
          waitLineAt = 0;
          face::setThinking(true);
        }
        break;
      case reach::E_EMPTY:
        waitingReply = false;
        face::setThinking(false);
        os::sayLine("ptt.empty");
        break;
      case reach::E_ACK: waitStart = millis(); break;
      case reach::E_CHIME: voice::chime(CH_NOTE); break;
      case reach::E_DO: reach::sendLine(ops::run(e.text)); break;
      case reach::E_LOOK: reach::sendShot((const uint8_t *)cv.getBuffer(), SCREEN_W * SCREEN_H * 2, SCREEN_W, SCREEN_H); break;
      default: break;
    }
  }
  if (waitingReply) {
    uint32_t now = millis();
    if (now - waitStart > 90000) {
      waitingReply = false;
      face::setThinking(false);
      os::sayLine("link.timeout");
    } else if (now - waitStart > 1800 && now - waitLineAt > 4200 && mind::has("link.wait")) {
      waitLine = mind::pick("link.wait").say;
      waitLineAt = now;
    }
  }
}

// its own mind writes in lowercase: capitals at the start of each sentence, and for I
static String tidy(String t) {
  t.trim();
  bool start = true;
  for (size_t i = 0; i < t.length(); i++) {
    char c = t[i];
    bool word = i == 0 || t[i - 1] == ' ';
    if (c == 'i' && word && (i + 1 == t.length() || t[i + 1] == ' ' || t[i + 1] == '\'' || t[i + 1] == ',' || t[i + 1] == '.' || t[i + 1] == '?' || t[i + 1] == '!')) t[i] = 'I';
    if (start && isalpha((unsigned char)c)) {
      t[i] = toupper((unsigned char)c);
      start = false;
    }
    if (c == '.' || c == '?' || c == '!') start = true;
  }
  return t;
}

// ---------------------------------------------------------------- the body --
static void senses(float dt) {
  static uint32_t lastPower = 0, lastMuse = 0, nextMuse = 0, stillSince = 0, lastShake = 0, faceDownSince = 0;
  static int lastPct = -1, lastCharging = -1;
  static bool saidLow = false, saidCritical = false, saidFaceDown = false;
  static float gx = 0, gy = 0, gz = 1;
  static int shakes = 0;
  uint32_t now = millis();
  bool free = !sp.active && !waitingReply && !pttHeld && !voice::busy();

  // the room
  if (cfg.listen) {
    if (voice::micLoud() && !face::asleep()) {
      face::bristle();
      ops::noteEvent("loud sound");
      if (free && cfg.chatty > 0 && random(100) < 55) os::sayLine("sound.loud", cfg.chatty > 1);
    }
    if (voice::micVoice() && free && !reach::up() && cfg.chatty > 0 && !face::asleep()) os::sayLine("sound.voice_offline", false);
  }

  // motion
  if (sense::hasImu && M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    sense::ax = d.accel.x;
    sense::ay = d.accel.y;
    sense::az = d.accel.z;
    float dx = sense::ax - gx, dy = sense::ay - gy, dz = sense::az - gz;
    float jerk = sqrtf(dx * dx + dy * dy + dz * dz);
    gx += dx * 0.08f;
    gy += dy * 0.08f;
    gz += dz * 0.08f;
    face::tilt(constrain(gx, -1.0f, 1.0f), 0);
    face::held(stillSince && now - stillSince < 2500);
    if (jerk > 0.9f) {
      if (now - lastShake > 900) shakes = 0;
      shakes++;
      lastShake = now;
      if (shakes == 4) {
        os::touched();
        if (face::asleep()) face::setAsleep(false);
        face::shake();
        ops::noteEvent("shaken");
        if (free) os::sayLine("move.shake");
      }
    }
    if (jerk > 0.12f) {
      ops::noteMoved();
      if (stillSince && now - stillSince > 25000 && jerk < 0.9f) {
        os::touched();
        ops::noteEvent("picked up");
        if (face::asleep()) {
          face::setAsleep(false);
          os::sayLine("wake", false);
        } else if (free && cfg.chatty > 0 && random(100) < 60) os::sayLine("move.pickup", false);
      }
      stillSince = now;
    } else if (!stillSince) stillSince = now;
    if (gz < -0.85f) {
      if (!faceDownSince) faceDownSince = now;
      if (now - faceDownSince > 1500 && !saidFaceDown) {
        saidFaceDown = true;
        ops::noteEvent("laid face down");
        if (free) os::sayLine("move.facedown", false);
        face::setAsleep(true);
        dimLevel = 2;
        os::applyBrightness();
      }
    } else {
      faceDownSince = 0;
      if (saidFaceDown && gz > -0.3f) {
        saidFaceDown = false;
        os::touched();
        face::setAsleep(false);
      }
    }
  }

  // energy
  if (now - lastPower > 4000) {
    lastPower = now;
    int pct = M5Cardputer.Power.getBatteryLevel();
    int charging = (int)M5Cardputer.Power.isCharging();
    if (lastCharging >= 0 && charging != lastCharging && charging != 2 && lastCharging != 2 && free)
      os::sayLine(charging == 1 ? "power.plugged" : "power.unplugged", cfg.chatty > 1);
    lastCharging = charging;
    if (pct >= 0 && charging != 1) {
      if (pct <= 7 && !saidCritical) {
        saidCritical = true;
        os::sayLine("battery.critical");
      } else if (pct <= 20 && !saidLow) {
        saidLow = true;
        os::sayLine("battery.low");
      }
    }
    if (pct > 30) saidLow = saidCritical = false;
    lastPct = pct;
  }

  // its own mind: answers arrive, thoughts come and go, the state is kept
  think::Result made;
  while (think::poll(made)) {
    String text = tidy(mind::fill(made.text.c_str()));
    if (made.thought) {
      if (text.length() > 2 && !sp.active && !ownPending) {
        thoughtText = text;
        thoughtAt = now;
        face::flash(made.mood, 5000);
      }
    } else {
      ownPending = false;
      face::setThinking(false);
      if (text.length() > 1) os::hum(text, made.mood);
      else os::sayLine("unknown");
    }
  }
  if (ownPending && now - ownStart > 12000) {
    ownPending = false;
    face::setThinking(false);
  }
  if (cfg.ownMind && think::ready() && !think::busy() && cur == apps::home() && dimLevel < 2 && free && !ownPending &&
      os::idleMs() > 7000 && now > nextThought) {
    if (os::idleMs() > 90000 && random(100) < 40) think::event("quiet");
    think::muse();
    nextThought = now + 22000 + random(30000);
  }
  if (now - lastSave > 180000) {
    lastSave = now;
    think::save();
  }

  // being left alone
  uint32_t idle = os::idleMs();
  bool keepAwake = waitingReply || pttHeld || voice::busy();
  if (!keepAwake) {
    if (dimLevel == 0 && idle > 50000) {
      dimLevel = 1;
      os::applyBrightness();
    }
    if (dimLevel == 1 && idle > 240000) {
      dimLevel = 2;
      if (cur == apps::home()) os::sayLine("sleep", false);
      face::setAsleep(true);
      os::applyBrightness();
      think::save();
    }
  }
  if (cur == apps::home() && free && dimLevel < 2 && cfg.chatty > 0 && idle > 20000) {
    if (!nextMuse) nextMuse = now + 30000 + random(60000);
    if (now > nextMuse) {
      os::sayLine("idle.muse", cfg.chatty > 1 && random(100) < 35);
      nextMuse = now + (cfg.chatty > 1 ? 45000 : 90000) + random(120000);
    }
  } else if (idle < 20000) {
    nextMuse = 0;
  }
}

// ---------------------------------------------------------------- console ---
static String serialLine;

static void sendShot() {
  const uint8_t *buf = (const uint8_t *)cv.getBuffer();
  size_t total = SCREEN_W * SCREEN_H * 2;
  Serial.printf("#shot %d %d\n", SCREEN_W, SCREEN_H);
  static unsigned char out[4100];
  for (size_t off = 0; off < total; off += 3000) {
    size_t n = min((size_t)3000, total - off), olen = 0;
    mbedtls_base64_encode(out, sizeof(out), &olen, buf + off, n);
    Serial.write(out, olen);
    Serial.write('\n');
  }
  Serial.println("#ok");
}

// put <path> <bytes>: a file sent down the cable in pieces of up to 2048 bytes, each acknowledged
static void receiveFile(const String &c) {
  int sp1 = c.indexOf(' ', 4);
  String path = c.substring(4, sp1);
  uint32_t size = c.substring(sp1 + 1).toInt();
  String p = path;
  fs::FS *fs = sp1 > 0 ? pathFs(p) : nullptr;
  if (fs) {
    int slash = p.lastIndexOf('/');
    if (slash > 0) fs->mkdir(p.substring(0, slash));
  }
  File f = fs ? fs->open(p, FILE_WRITE) : File();
  if (!f) {
    Serial.println("#err could not open");
    return;
  }
  os::quiet();
  Serial.println("#ready");
  static uint8_t buf[2048];
  uint32_t got = 0, last = millis();
  while (got < size && millis() - last < 8000) {
    uint32_t want = min((uint32_t)sizeof(buf), size - got), have = 0;
    while (have < want && millis() - last < 8000) {
      int n = Serial.read(buf + have, want - have);
      if (n > 0) {
        have += n;
        last = millis();
      } else delay(1);
    }
    if (have < want) break;
    f.write(buf, have);
    got += have;
    Serial.println("#k");
  }
  f.close();
  Serial.printf("%u of %u bytes\n", (unsigned)got, (unsigned)size);
  Serial.println(got == size ? "#ok" : "#err short");
  if (path.endsWith("voice.pak")) voice::packReload();
}

static void console() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (serialLine.length() < 400) serialLine += ch;
      continue;
    }
    String c = serialLine;
    serialLine = "";
    if (c == "shot") {
      sendShot();
    } else if (c.startsWith("key ")) {
      for (size_t i = 4; i < c.length(); i++) {
        Key k;
        k.code = K_CHAR;
        k.ch = c[i];
        deliver(k);
      }
      Serial.println("#ok");
    } else if (c.startsWith("press ")) {
      String n = c.substring(6);
      Key k;
      k.code = n == "enter" ? K_ENTER : n == "del" ? K_DEL : n == "tab" ? K_TAB : n == "esc" ? K_ESC : n == "up" ? K_UP
               : n == "down" ? K_DOWN : n == "left" ? K_LEFT : n == "right" ? K_RIGHT : K_NONE;
      if (k.code != K_NONE) deliver(k);
      Serial.println(k.code != K_NONE ? "#ok" : "#err no such key");
    } else if (c.startsWith("put ")) {
      receiveFile(c);
    } else if (c == "talk on" || c == "talk off") {   // the talk button, pressed from the cable
      if (c == "talk on") Serial.println(talkStart() ? "listening" : "the whole mind is not in reach");
      else if (pttHeld) talkStop();
      Serial.println("#ok");
    } else if (c.length()) {
      shell::exec(c, Serial);
      Serial.println("#ok");
    }
  }
}

// ---------------------------------------------------------------- overlay ---
// in rooms other than the face, what the creature says shows as a strip along the bottom
static void toast(Gfx &g) {
  bool wait = waitingReply && waitLine.length();
  if (!sp.active && !wait && !pttHeld) return;
  int shown = os::speechShown();
  String text = pttHeld ? String("listening...") : sp.active ? sp.text.substring(0, shown) : waitLine;
  g.setFont(ui::fBody);
  auto lines = ui::wrap(g, text, SCREEN_W - 38);
  int rows = min(2, (int)lines.size());
  if (rows < 1) rows = 1;
  int h = rows * ui::TEXT_ROW + 6, y = SCREEN_H - h;
  g.fillRect(0, y, SCREEN_W, h, COL_BAND);
  g.drawFastHLine(0, y, SCREEN_W, face::tint());
  face::drawMini(g, 6, y + 5);
  g.setTextColor(COL_INK);
  int first = max(0, (int)lines.size() - rows);
  for (int i = 0; i < rows && first + i < (int)lines.size(); i++)
    g.drawString(text.substring(lines[first + i].start, lines[first + i].end), 32, y + 3 + i * ui::TEXT_ROW);
}

// ---------------------------------------------------------------- start -----
void setup() {
  auto mcfg = M5.config();
  M5Cardputer.begin(mcfg, true);
  Serial.setRxBufferSize(4608);
  Serial.begin(115200);
  M5Cardputer.Display.setBrightness(0);
  cfg.load();
  os::applyTurn();
  cv.setColorDepth(16);
  cv.createSprite(SCREEN_W, SCREEN_H);
  cv.fillScreen(COL_BG);
  cv.pushSprite(0, 0);

  bool first = cfg.boots == 0;
  cfg.boots++;
  cfg.save();
  LittleFS.begin(true);
  LittleFS.mkdir("/notes");
  randomSeed(esp_random());
  sense::hasImu = M5.Imu.isEnabled();

  voice::begin();
  think::begin();
  face::begin();
  reach::begin();
  os::applyBrightness();
  os::home();
  lastTouch = millis();

  voice::chime(CH_BOOT);
  think::event("wake");
  os::sayLine(first ? "boot.first" : "boot");
}

void loop() {
  static uint32_t lastFrame = micros();
  uint32_t nowUs = micros();
  float dt = (nowUs - lastFrame) / 1e6f;
  lastFrame = nowUs;
  if (dt > 0.1f) dt = 0.1f;
  fpsAvg += ((1.0f / max(dt, 0.001f)) - fpsAvg) * 0.05f;

  M5Cardputer.update();
  readKeys();
  console();
  linkEvents();
  senses(dt);
  apps::background();

  if (sp.active) {
    os::speechShown();
    uint32_t hold = 2600 + sp.text.length() * 55;
    if (sp.doneAt && millis() - sp.doneAt > hold) sp.active = false;
  }

  voice::voxAllow(reach::up() && cfg.hearName && cfg.listen && !pttHeld);
  face::setListening(pttHeld || voice::voxActive());
  face::update(dt, voice::mouth(), pttHeld ? max(voice::micLevel(), 0.05f) : voice::micLevel());
  if (cur) {
    cur->tick(dt);
    cur->draw(cv);
    if (cur != apps::home()) toast(cv);
  }
  cv.pushSprite(0, 0);

  // a dozing creature does not need thirty frames a second
  uint32_t spent = (micros() - nowUs) / 1000;
  uint32_t budget = dimLevel == 2 ? 90 : 30;
  if (spent < budget) delay(budget - spent);
}
