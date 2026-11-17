// Native check for firmware/lib/wisp: runs the sketch's scripted scenes at 30 frames a second and
// writes PNGs, a contact sheet, a set of checks, and frame timings.
//   wisp_png sheet <out.png>   the 12 states of look/scratch/sheet2.html, as the device would show them (trails)
//   wisp_png still <out.png>   the same, but the paper is repainted before the last frame, as the sketch's
//                              stills are drawn (one draw after the simulation, no trails)
//   wisp_png extra <dir>       a few states outside the twelve (waking, null state, n = 300, mid-shake)
//   wisp_png check             paper bit-for-bit, recovery from garbage, drift, long run
//   wisp_png time              microseconds per frame, calm and shaken, and the work counted
// The library draws no text; the harness draws text the way the firmware will (opaque, colour mixed
// against the paper), as "greeked" blocks, so the band, the typed line, thoughts and z marks show where they go.
#include "wisp.h"
#include "wisp_tables.h"
#include <algorithm>
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static const int W = WISP_W, H = WISP_H;
static uint16_t buf[W * H];

// ---------------- PNG (stored deflate blocks) ----------------
static uint32_t crcT[256];
static uint32_t crc(const uint8_t *p, size_t n, uint32_t c = 0xFFFFFFFFu) {
  if (!crcT[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t k = i;
      for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1;
      crcT[i] = k;
    }
  for (size_t i = 0; i < n; i++) c = crcT[(c ^ p[i]) & 255] ^ (c >> 8);
  return c;
}
static void be32(std::vector<uint8_t> &v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
static void chunk(FILE *f, const char *type, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> v;
  be32(v, (uint32_t)data.size());
  v.insert(v.end(), type, type + 4);
  v.insert(v.end(), data.begin(), data.end());
  uint32_t c = crc(v.data() + 4, v.size() - 4) ^ 0xFFFFFFFFu;
  be32(v, c);
  fwrite(v.data(), 1, v.size(), f);
}
static bool writePng(const char *path, const uint8_t *rgb, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); return false; }
  const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ih;
  be32(ih, w); be32(ih, h);
  ih.push_back(8); ih.push_back(2); ih.push_back(0); ih.push_back(0); ih.push_back(0);
  chunk(f, "IHDR", ih);
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), rgb + y * w * 3, rgb + (y + 1) * w * 3); }
  std::vector<uint8_t> z;
  z.push_back(0x78); z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size() || raw.empty()) {
    size_t n = raw.size() - pos; if (n > 65535) n = 65535;
    z.push_back(pos + n == raw.size() ? 1 : 0);
    z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (raw.empty()) break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  be32(z, (b << 16) | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", std::vector<uint8_t>());
  fclose(f);
  return true;
}
// the 16-bit frame to 8-bit RGB, channels expanded as the sketch's present() leaves them (x << 3, x << 2)
static void toRgb(const uint16_t *src, uint8_t *dst) {
  for (int i = 0; i < W * H; i++) {
    uint16_t c = (uint16_t)((src[i] >> 8) | (src[i] << 8));
    dst[i * 3] = (uint8_t)((c >> 11) << 3);
    dst[i * 3 + 1] = (uint8_t)(((c >> 5) & 63) << 2);
    dst[i * 3 + 2] = (uint8_t)((c & 31) << 3);
  }
}

// ---------------- the mind's real states ----------------
struct Mind {
  int H = 0, steps = 0;
  float E10 = 0, E90 = 1;
  std::vector<float> ent;
  std::vector<int8_t> h;
  float row[512];
  int k = 200;
  bool load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return false; }
    int32_t hd[2]; float e[2];
    if (fread(hd, 4, 2, f) != 2 || fread(e, 4, 2, f) != 2) return false;
    H = hd[0]; steps = hd[1]; E10 = e[0]; E90 = e[1];
    ent.resize(steps); h.resize((size_t)steps * H);
    if (fread(ent.data(), 4, steps, f) != (size_t)steps || fread(h.data(), 1, h.size(), f) != h.size()) return false;
    fclose(f);
    setRow();
    return true;
  }
  void setRow() { for (int i = 0; i < H && i < 512; i++) row[i] = h[(size_t)(k % steps) * H + i] / 127.0f; }
  float piece() {   // the sketch's MIND.piece: the next real state, and its effort from the entropy
    k++;
    setRow();
    float eff = (ent[k % steps] - E10) / (E90 - E10);
    return eff < 0 ? 0 : eff > 1 ? 1 : eff;
  }
} mind;

// ---------------- the world (the sketch's E and world(), as the firmware will play it) ----------------
static float rndf(float a, float b) { return a + (float)rand() / (float)RAND_MAX * (b - a); }
static const float PAPER_R = 246, PAPER_G = 235, PAPER_B = 220;

struct Thought { std::vector<std::string> lines; float x, y, t, life; };
struct World {
  float t = 0, typingT = -9, think = 0, pieceAcc = 0, lastAct = 0, heldEnd = 0, sleepT = 0, nextThought = 7, answerUntil = 0;
  bool thenAnswer = false, held = false, sleepy = false, asleep = false, whole = false;
  std::string typed, answer, pendingAnswer = "Well, hello.";
  float tilt = 0, cursorX = 8;   // the sketch's stills never move the cursor from 8
  uint8_t mood = WISP_CALM;
  struct Hum { bool on = false; std::string text; float t = 0, dur = 0; bool mac = false; } hum;
  float env = 0;
  std::vector<Thought> thoughts;
  int rows = 0;

  void piece(int src) {   // 0 own, 1 you, 2 mac
    float eff = mind.piece();
    wisp_piece(eff);
    if (src == 1) wisp_word_in(false);
    if (src == 2) wisp_word_in(true);
  }
  void act() { lastAct = t; if (asleep) { asleep = false; sleepy = false; wisp_wake(); } }
  void startThink(float sec, bool then) { act(); think = sec; thenAnswer = then; }
  void startHum() {
    act();
    hum.on = true; hum.text = pendingAnswer; hum.t = 0; hum.dur = 0.9f + hum.text.size() * 0.062f; hum.mac = whole;
    answer = hum.text;
  }
  void typeKey(char ch) {
    act(); typingT = t; answer.clear();
    if (ch == '\n') { if (!typed.empty()) { typed.clear(); startThink(1.2f, true); } return; }
    if (typed.size() < 34) { typed += ch; if (ch == ' ') piece(1); }
  }
  static float voiceEnv(float t) {
    float s = sinf(t * 6.2831853f * 4.3f); s = s > 0 ? powf(s, 0.7f) : 0;
    return s * (0.6f + 0.4f * sinf(t * 1.3f));
  }
  static int rowsOf(const std::string &s) { int r = (int)ceilf(s.size() * 6.3f / 228.0f); return r < 1 ? 1 : r > 3 ? 3 : r; }
  static std::vector<std::string> wrap(const std::string &text, size_t n) {
    std::vector<std::string> out; std::string line, w;
    for (size_t i = 0; i <= text.size(); i++) {
      if (i == text.size() || text[i] == ' ') {
        std::string t2 = line.empty() ? w : line + " " + w;
        if (t2.size() > n && !line.empty()) { out.push_back(line); line = w; } else line = t2;
        w.clear();
      } else w += text[i];
    }
    if (!line.empty()) out.push_back(line);
    return out;
  }
  void step(float dt) {
    t += dt;
    if (held && t > heldEnd) held = false;
    if (!asleep && t - lastAct > 45 && !sleepy) { sleepy = true; sleepT = t; }
    if (sleepy && !asleep && t - sleepT > 2.5f) asleep = true;
    if (think > 0) {
      think -= dt; pieceAcc += dt;
      while (pieceAcc > 0.063f) { pieceAcc -= 0.063f; piece(0); }
      if (think <= 0 && thenAnswer) startHum();
    }
    if (hum.on) {
      hum.t += dt; env = hum.t < hum.dur - 0.3f ? voiceEnv(hum.t) : 0;
      pieceAcc += dt;
      while (pieceAcc > 0.33f) { pieceAcc -= 0.33f; piece(hum.mac ? 2 : 0); }
      if (hum.t > hum.dur) { hum.on = false; answerUntil = t + 2.5f; }
    } else env = 0;
    if (!answer.empty() && !hum.on && t > answerUntil) answer.clear();
    float idle = t - lastAct;
    bool busy = think > 0 || hum.on || !typed.empty() || !answer.empty();
    if (!asleep && !busy && idle > 6 && t > nextThought && thoughts.empty()) {
      Thought th; th.lines = wrap("The moon drifts away about as fast as fingernails grow. No rush.", 24);
      wisp_head(&th.x, &th.y); th.t = 0; th.life = 9;
      thoughts.push_back(th);
      nextThought = t + rndf(14, 22); think = 0.8f; thenAnswer = false;
    }
    for (auto &th : thoughts) th.t += dt;
    for (size_t i = 0; i < thoughts.size();) if (thoughts[i].t >= thoughts[i].life) thoughts.erase(thoughts.begin() + i); else i++;
    rows = !answer.empty() ? rowsOf(answer) : (!typed.empty() || t - typingT < 1.5f) ? 1 : 0;
  }
  WispInput input(float dt) {
    WispInput in;
    memset(&in, 0, sizeof(in));
    in.dt = dt;
    in.state = mind.row; in.n = mind.H;
    in.mood = sleepy ? (uint8_t)WISP_SLEEPY : mood;
    in.asleep = asleep; in.thinking = think > 0; in.whole = whole; in.held = held;
    in.voice = env; in.tiltX = sinf(tilt);
    in.looking = t - typingT < 3; in.lookX = cursorX; in.lookY = H - (rows ? rows * 12 + 5 : 0) + 8;
    in.textRows = rows ? rows * 12 + 5.0f : 0;
    return in;
  }
};

// ---------------- greeked text, drawn as the firmware would (opaque, mixed against the paper) ----------------
static uint16_t sw565(float r, float g, float b) {
  uint16_t c = (uint16_t)((((int)r & 0xF8) << 8) | (((int)g & 0xFC) << 3) | ((int)b >> 3));
  return (uint16_t)((c >> 8) | (c << 8));
}
static uint16_t onPaper(int r, int g, int b, float a) { return sw565(PAPER_R + (r - PAPER_R) * a, PAPER_G + (g - PAPER_G) * a, PAPER_B + (b - PAPER_B) * a); }
static void rect(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (i >= 0 && j >= 0 && i < W && j < H) buf[j * W + i] = c;
}
static void greek(const std::string &s, int x, int top, int adv, int cap, int xh, uint16_t c) {
  for (char ch : s) {
    if (ch != ' ') {
      bool small = strchr("acemnorsuvwxz", ch) != nullptr, punct = strchr(".,'!?:;", ch) != nullptr;
      int h = punct ? 2 : small ? xh : cap;
      rect(x, top + cap - h, punct ? 1 : adv - 1, h, c);
    }
    x += adv;
  }
}
static void drawZ(float x, float y, int px, uint16_t c) {
  int s = px < 10 ? 4 : 6, x0 = (int)x, y0 = (int)y + (px < 10 ? 3 : 4);
  for (int i = 0; i < s; i++) { rect(x0 + i, y0, 1, 1, c); rect(x0 + i, y0 + s - 1, 1, 1, c); rect(x0 + s - 1 - i, y0 + i, 1, 1, c); }
}
static void drawWords(World &w) {
  float top = H - (w.rows ? w.rows * 12 + 5 : 0);
  if (!w.answer.empty()) {
    auto lines = World::wrap(w.answer, 36);
    for (size_t j = 0; j < lines.size() && j < 3; j++) greek(lines[j], W / 2 - (int)lines[j].size() * 3, (int)top + 3 + (int)j * 12, 6, 9, 6, sw565(52, 38, 36));
  } else if (!w.typed.empty() || w.t - w.typingT < 1.5f) {
    std::string s = "> " + w.typed;
    greek(s, 6, (int)top + 3, 6, 9, 6, sw565(48, 98, 140));
    if ((int)(w.t * 2) % 2 == 0) rect(6 + (int)s.size() * 6 + 1, (int)top + 3, 1, 11, sw565(48, 98, 140));
  }
  for (auto &th : w.thoughts) {
    float a = fminf(1, fminf(th.t / 1.2f, (th.life - th.t) / 2)) * 0.8f;
    if (a <= 0) continue;
    float x, y;
    wisp_thought_at(th.x, th.y, th.t, (int)th.lines.size(), &x, &y);
    for (size_t j = 0; j < th.lines.size(); j++) greek(th.lines[j], (int)x - (int)th.lines[j].size() * 5 / 2, (int)y + (int)j * 10, 5, 7, 4, onPaper(120, 70, 58, a));
  }
  WispMark z[3];
  int n = wisp_zs(z, 3);
  for (int i = 0; i < n; i++) drawZ(z[i].x, z[i].y, z[i].px, onPaper(130, 90, 110, z[i].alpha));
}

// ---------------- scenes (look/scratch/sheet2.html, in its order) ----------------
struct Scene { const char *name, *script; float at; const char *answer; };
static const Scene SCENES[12] = {
    {"calm-at-rest", "", 4, ""},
    {"typing", "type:hello_wink_how", 0.8f, ""},
    {"thinking", "think", 1.2f, ""},
    {"humming", "type:hi,enter", 2.6f, "Well, hello."},
    {"tilted-right", "tilt:0.7", 3, ""},
    {"picked-up", "pick", 1.2f, ""},
    {"shaken", "shake", 0.5f, ""},
    {"low", "mood:low", 3, ""},
    {"asleep", "sleep", 6, ""},
    {"whole-speaking", "whole,type:hi,enter", 3.2f, "Fine, thanks. It's a good day to be switched on."},
    {"thought-drifts", "idle", 9.5f, ""},
    {"cheeky", "mood:cheeky", 3, ""},
};

static void applyScript(World &w, const char *script) {
  std::string s = script;
  size_t p = 0;
  while (p < s.size()) {
    size_t e = s.find(',', p); if (e == std::string::npos) e = s.size();
    std::string ev = s.substr(p, e - p), k = ev, v;
    size_t c = ev.find(':'); if (c != std::string::npos) { k = ev.substr(0, c); v = ev.substr(c + 1); }
    if (k == "type") for (char ch : v) w.typeKey(ch == '_' ? ' ' : ch);
    else if (k == "enter") w.typeKey('\n');
    else if (k == "think") w.startThink(2.2f, false);
    else if (k == "tilt") { w.act(); w.tilt = (float)atof(v.c_str()); }
    else if (k == "pick") { w.act(); w.held = true; w.heldEnd = w.t + 2.8f; }
    else if (k == "shake") { w.act(); wisp_scatter(1); }
    else if (k == "loud") { w.act(); wisp_bristle(); }
    else if (k == "sleep") { w.sleepy = true; w.sleepT = w.t; }
    else if (k == "whole") { w.act(); w.whole = !w.whole; wisp_flash(w.whole ? WISP_DELIGHTED : WISP_TENDER, 2); }
    else if (k == "mood") { w.act(); w.mood = v == "low" ? WISP_SAD : v == "cheeky" ? WISP_CHEEKY : v == "pleased" ? WISP_HAPPY : v == "curious" ? WISP_CURIOUS : WISP_CALM; }
    else if (k == "idle") w.lastAct = -20;
    p = e + 1;
  }
}

// run one scene; returns the world (for its text); `still` repaints the paper before the last frame
static World runScene(const Scene &sc, bool still, uint32_t seed = 7) {
  srand(seed);
  wisp_begin(seed);
  mind.k = 200; mind.setRow();
  wisp_repaint(buf);
  World w;
  w.pendingAnswer = sc.answer;
  applyScript(w, sc.script);
  int n = (int)lroundf(sc.at * 30);
  const float dt = 1.0f / 30;
  for (int i = 0; i < n; i++) {
    w.step(dt);
    if (still && i == n - 1) wisp_repaint(buf);
    wisp_frame(buf, w.input(dt));
    drawWords(w);
  }
  return w;
}

static void sheet(const char *out, bool still, const char *dir) {
  const int S = 2, G = 6, CW = W * S, CH = H * S, SW = 3 * CW + 4 * G, SH = 4 * CH + 5 * G;
  std::vector<uint8_t> big((size_t)SW * SH * 3, 34);
  static uint8_t rgb[W * H * 3];
  for (int s = 0; s < 12; s++) {
    runScene(SCENES[s], still);
    toRgb(buf, rgb);
    if (dir) { char p[512]; snprintf(p, sizeof p, "%s/%s%s.png", dir, still ? "still-" : "", SCENES[s].name); writePng(p, rgb, W, H); }
    int ox = G + (s % 3) * (CW + G), oy = G + (s / 3) * (CH + G);
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) memcpy(&big[((size_t)(oy + y) * SW + ox + x) * 3], &rgb[((y / S) * W + x / S) * 3], 3);
  }
  writePng(out, big.data(), SW, SH);
  printf("wrote %s (%d x %d): ", out, SW, SH);
  for (int s = 0; s < 12; s++) printf("%s%s", s ? ", " : "", SCENES[s].name);
  printf("\n");
}

static std::vector<std::vector<uint8_t>> kept;   // frames saved by extra(), for its sheet
static void save(const char *path) {
  std::vector<uint8_t> rgb(W * H * 3);
  toRgb(buf, rgb.data());
  writePng(path, rgb.data(), W, H);
  kept.push_back(rgb);
}
static void keptSheet(const char *out) {
  const int S = 2, G = 6, CW = W * S, CH = H * S, cols = 3, rowsN = ((int)kept.size() + cols - 1) / cols;
  const int SW = cols * CW + (cols + 1) * G, SH = rowsN * CH + (rowsN + 1) * G;
  std::vector<uint8_t> big((size_t)SW * SH * 3, 34);
  for (size_t s = 0; s < kept.size(); s++) {
    int ox = G + (int)(s % cols) * (CW + G), oy = G + (int)(s / cols) * (CH + G);
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) memcpy(&big[((size_t)(oy + y) * SW + ox + x) * 3], &kept[s][((y / S) * W + x / S) * 3], 3);
  }
  writePng(out, big.data(), SW, SH);
  printf("wrote %s\n", out);
}

static void extra(const char *dir) {
  char p[512];
  const float dt = 1.0f / 30;
  // waking: the coming-together, 0.25 s and 0.7 s in
  {
    srand(3); wisp_begin(3); mind.k = 200; mind.setRow(); wisp_repaint(buf);
    World w;
    for (int i = 0; i < 30; i++) { w.step(dt); wisp_frame(buf, w.input(dt)); }
    wisp_wake();
    for (int i = 1; i <= 21; i++) {
      w.step(dt); wisp_frame(buf, w.input(dt));
      if (i == 8 || i == 21) { snprintf(p, sizeof p, "%s/wake-%03dms.png", dir, i * 33); save(p); }
    }
  }
  // the model's own 512, a null state and a 300-number state (no tables: a running mean): restlessness
  // 2 s into thinking, after 20 s of thinking, and after 6 s of quiet
  for (int pass = 0; pass < 3; pass++) {
    srand(5); wisp_begin(5); mind.k = 200; mind.setRow(); wisp_repaint(buf);
    World w;
    w.nextThought = 1e9f;   // no drifting thought (and its burst of thinking): like for like with scratch/sketch_restless.mjs
    const char *name = pass == 0 ? "n = 512 (own tables)" : pass == 1 ? "null state" : "n = 300 (running mean)";
    float r[3] = {0, 0, 0};
    for (int i = 0; i < 30 * 26; i++) {
      if (i < 600 && w.think <= 0) w.startThink(2.2f, false);
      w.step(dt);
      WispInput in = w.input(dt);
      if (pass == 1) { in.state = nullptr; in.n = 0; }
      if (pass == 2) in.n = 300;
      wisp_frame(buf, in);
      if (i == 59) {
        r[0] = wisp_restless();
        if (pass) { snprintf(p, sizeof p, "%s/%s.png", dir, pass == 1 ? "state-null-thinking" : "state-n300-thinking"); save(p); }
      }
      if (i == 599) r[1] = wisp_restless();
      if (getenv("WISP_DEBUG") && pass == 0 && i >= 540 && i % 15 == 14) printf("  t %.1f think %.2f k %d restless %.3f\n", (double)w.t, (double)w.think, mind.k, (double)wisp_restless());
    }
    r[2] = wisp_restless();
    printf("%-24s restless %.2f at 2 s of thinking, %.2f at 20 s, %.2f after 6 s quiet\n", name, (double)r[0], (double)r[1], (double)r[2]);
  }
  // shaken, 0.9 s in (the motes at their widest), and a loud sound
  {
    Scene sc = {"shaken-0.9s", "shake", 0.9f, ""}; runScene(sc, false); snprintf(p, sizeof p, "%s/shaken-0900ms.png", dir); save(p);
    Scene sl = {"loud", "loud", 0.3f, ""}; runScene(sl, false); snprintf(p, sizeof p, "%s/loud-300ms.png", dir); save(p);
    Scene sd = {"dizzy", "shake", 2.0f, ""}; runScene(sd, false); snprintf(p, sizeof p, "%s/dizzy-2s.png", dir); save(p);
  }
  // key pokes along the typed line while it floats beside the cursor
  {
    srand(9); wisp_begin(9); mind.k = 200; mind.setRow(); wisp_repaint(buf);
    World w;
    for (int i = 0; i < 75; i++) {
      if (i >= 30 && i % 4 == 0) { w.typeKey('a'); w.cursorX = 6 + (2 + (float)w.typed.size()) * 6; wisp_poke(w.cursorX, H - 10, 0.35f); }
      w.step(dt); wisp_frame(buf, w.input(dt)); drawWords(w);
    }
    snprintf(p, sizeof p, "%s/typing-pokes.png", dir); save(p);
  }
  // the order in the sheet: wake 264 ms, wake 693 ms, null state, n = 300, shaken 0.9 s, loud 0.3 s, dizzy 2 s, typing pokes
  keptSheet("scratch/wisp-extra.png");
}

// ---------------- checks ----------------
static int check() {
  int fails = 0;
  // 1. the paper against the sketch's: the same colours in the same shares (the grain's addresses differ:
  //    the port takes the hash's top 3 bits, the sketch the hash mod 7), and the fibres in the same places
  {
    FILE *f = fopen("scratch/paper-js.bin", "rb");
    std::vector<uint16_t> js(W * H);
    if (!f || fread(js.data(), 2, js.size(), f) != js.size()) { printf("FAIL paper: scratch/paper-js.bin missing (run gen_tables.mjs)\n"); fails++; }
    else {
      fclose(f);
      wisp_begin(1);
      // shares of each 16-bit colour, on the fibres and off them separately (so misplaced fibres would show)
      std::vector<int> a(2 * 65536, 0), b(2 * 65536, 0);
      int nf[2] = {0, 0};
      double ma[3] = {0, 0, 0}, mb[3] = {0, 0, 0};
      for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        uint16_t v = wisp_paper(x, y), c = (uint16_t)((v >> 8) | (v << 8)), j = js[y * W + x];
        int fib = (x + y * 3) % 23 == 0;
        a[fib * 65536 + c]++; b[fib * 65536 + j]++; nf[fib]++;
        ma[0] += (c >> 11) << 3; ma[1] += ((c >> 5) & 63) << 2; ma[2] += (c & 31) << 3;
        mb[0] += (j >> 11) << 3; mb[1] += ((j >> 5) & 63) << 2; mb[2] += (j & 31) << 3;
      }
      double worst[2] = {0, 0};
      for (int f2 = 0; f2 < 2; f2++)
        for (int i = 0; i < 65536; i++) { int k = f2 * 65536 + i; if (a[k] || b[k]) worst[f2] = fmax(worst[f2], fabs(a[k] - b[k]) * 100.0 / nf[f2]); }
      double dm = 0;
      for (int k = 0; k < 3; k++) dm = fmax(dm, fabs(ma[k] - mb[k]) / (W * H));
      bool bad = worst[0] > 2.5 || worst[1] > 4.0 || dm > 0.5;
      printf("%s paper: against the sketch's paper, each 16-bit colour's share differs by at most %.2f points off the fibres\n"
             "     and %.2f on them (%d fibre pixels); the mean colour differs by at most %.2f in 255\n", bad ? "FAIL" : "ok  ", worst[0], worst[1], nf[1], dm);
      fails += bad;
    }
  }
  const float dt = 1.0f / 30;
  // 2. recovery: a buffer of garbage (another screen) is taken back to exact paper outside the creature
  {
    srand(11); wisp_begin(11); mind.k = 200; mind.setRow();
    for (int i = 0; i < W * H; i++) buf[i] = (uint16_t)rand();
    World w;
    int firstClean = -1;
    for (int i = 0; i < 60; i++) {
      w.step(dt); wisp_frame(buf, w.input(dt));
      float cx, cy; wisp_center(&cx, &cy);
      int bad = 0;
      for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        if (fabsf(x - cx) < 52 && fabsf(y - cy) < 44) continue;          // the puff and its trails
        if (y >= (int)wisp_floor() - 4 && fabsf(x - cx) < 40) continue;  // its shadow
        if (buf[y * W + x] != wisp_paper(x, y)) bad++;
      }
      if (!bad && firstClean < 0) firstClean = i + 1;
      if (i == 59) { printf("%s recovery: after 60 frames, %d pixels away from the creature are not paper (clean from frame %d)\n", bad ? "FAIL" : "ok  ", bad, firstClean); fails += bad != 0; }
    }
  }
  // 3. creep: 30 s holding still (attending a fixed point, so the goal does not move), no pieces.
  //    Rounding to int16 must not make the motes creep away from their homes: the motes' centroid must sit
  //    where the homes' own centroid puts it, and not move between the first and the last second.
  {
    srand(12); wisp_begin(12); mind.k = 200; mind.setRow(); wisp_repaint(buf);
    World w;
    double hx = 0, hy = 0;
    for (int i = 0; i < WISP_MOTES; i++) { hx += WISP_HOME_X[i] / 16384.0; hy += WISP_HOME_Y[i] / 16384.0; }
    hx /= WISP_MOTES; hy /= WISP_MOTES;
    // 12 windows of 10 s after 3 s to arrive; creep would show as window means that walk away
    float worst = 0, win[12][2] = {};
    for (int i = 0; i < 90 + 3600; i++) {
      w.step(dt);
      w.lastAct = w.t;   // keep it awake
      WispInput in = w.input(dt);
      in.looking = true; in.lookX = 100; in.lookY = 130;
      wisp_frame(buf, in);
      if (i < 90) continue;
      float cx, cy, mx, my; wisp_center(&cx, &cy); wisp_mote_centroid(&mx, &my);
      float ex = mx - cx - (float)hx * 34, ey = my - cy - (float)hy * 28, d = hypotf(ex, ey);
      if (d > worst) worst = d;
      int k = (i - 90) / 300;
      win[k][0] += ex / 300; win[k][1] += ey / 300;
    }
    float spread = 0;
    for (int a = 0; a < 12; a++) for (int b = 0; b < 12; b++) spread = fmaxf(spread, hypotf(win[a][0] - win[b][0], win[a][1] - win[b][1]));
    bool bad = worst > 1.0f || spread > 0.15f;
    printf("%s creep: over 120 s held still the motes' centroid stayed within %.2f px of where the homes put it (frame by frame);\n"
           "     the 10 s averages lie within %.3f px of each other (first %.3f,%.3f; last %.3f,%.3f)\n",
           bad ? "FAIL" : "ok  ", (double)worst, (double)spread, (double)win[0][0], (double)win[0][1], (double)win[11][0], (double)win[11][1]);
    fails += bad;
  }
  // 4. a long run across the clock's wrap (3,700 s), with thinking, humming, shakes, sleep and wakes
  {
    srand(13); wisp_begin(13); mind.k = 200; mind.setRow(); wisp_repaint(buf);
    World w;
    bool bad = false;
    for (int i = 0; i < 3700 * 30; i++) {
      if (i % 600 == 0) { int r = rand() % 6; if (r == 0) w.startThink(2.2f, true); if (r == 1) wisp_scatter(1); if (r == 2) wisp_bristle(); if (r == 3) { w.sleepy = true; w.sleepT = w.t; } if (r == 4) w.act(); if (r == 5) wisp_word_in(rand() & 1); }
      w.step(dt); wisp_frame(buf, w.input(dt));
      float cx, cy, mx, my; wisp_center(&cx, &cy); wisp_mote_centroid(&mx, &my);
      if (!(cx > 0 && cx < W && cy > 0 && cy < H && mx > -50 && mx < W + 50 && my > -50 && my < H + 50)) { bad = true; printf("  out of bounds at frame %d: centre %.1f,%.1f motes %.1f,%.1f\n", i, (double)cx, (double)cy, (double)mx, (double)my); break; }
    }
    printf("%s long run: 3,700 simulated seconds (111,000 frames), the puff stayed on the screen and finite\n", bad ? "FAIL" : "ok  ");
    fails += bad;
  }
  printf("state: %u bytes of working memory (limit 8,192)\n", wisp_state_bytes());
  return fails;
}

// ---------------- timing ----------------
static void timing() {
  const float dt = 1.0f / 30;
  using clk = std::chrono::steady_clock;
  struct Run { const char *name, *script; int frames; int band; bool garbage; };
  Run runs[5] = {{"calm", "", 120, 0, false},
                 {"thinking", "think", 60, 0, false},
                 {"shaken (first 39 frames: motes flying)", "shake", 39, 0, false},
                 {"shaken under a 3-row answer band (41 px)", "shake", 39, 41, false},
                 {"from a garbage buffer (first 15 frames)", "", 15, 0, true}};
  for (auto &r : runs) {
    double sum = 0, worst = 0; int count = 0;
    std::vector<double> all;
    WispCost wc = {};
    for (int rep = 0; rep < 200; rep++) {
      srand(7); wisp_begin(7); mind.k = 200; mind.setRow(); wisp_repaint(buf);
      if (r.garbage) for (int i = 0; i < W * H; i++) buf[i] = (uint16_t)rand();
      World w; applyScript(w, r.script);
      for (int i = 0; i < r.frames; i++) {
        w.step(dt);
        WispInput in = w.input(dt);
        if (r.band) in.textRows = (float)r.band;
        auto t0 = clk::now();
        wisp_frame(buf, in);
        double us = std::chrono::duration<double, std::micro>(clk::now() - t0).count();
        sum += us; count++; all.push_back(us);
        if (us > worst) worst = us;
        WispCost c = wisp_cost();
        if (c.faded + c.blends > wc.faded + wc.blends) wc = c;
      }
    }
    // operations, counted per item (integer or float add/mul/compare/load/store, loop overhead included)
    double ops = wc.paperFast * 12.0 + wc.faded * 40.0 + wc.blends * 24.0 + wc.shapePx * 28.0 + wc.motes * 110.0 + wc.bandPx * 1.5 + 6000;
    std::sort(all.begin(), all.end());
    printf("%-40s mean %6.1f us, 99th percentile %6.1f us, worst %6.1f us per frame (%d frames)\n"
           "     busiest frame: paper %u, faded %u, blends %u, shape px %u, motes %u, band px %u => about %.0fk operations\n",
           r.name, sum / count, all[all.size() * 99 / 100], worst, count, wc.paperFast, wc.faded, wc.blends, wc.shapePx, wc.motes, wc.bandPx, ops / 1000);
  }
}

int main(int argc, char **argv) {
  if (!mind.load("mind.bin")) { fprintf(stderr, "run from look/port/ after node gen_tables.mjs\n"); return 2; }
  const char *mode = argc > 1 ? argv[1] : "sheet";
  if (!strcmp(mode, "sheet") || !strcmp(mode, "still")) sheet(argc > 2 ? argv[2] : "scratch/wisp-sheet.png", !strcmp(mode, "still"), argc > 3 ? argv[3] : nullptr);
  else if (!strcmp(mode, "extra")) extra(argc > 2 ? argv[2] : "scratch");
  else if (!strcmp(mode, "check")) return check() ? 1 : 0;
  else if (!strcmp(mode, "time")) timing();
  else { fprintf(stderr, "modes: sheet, still, extra, check, time\n"); return 2; }
  return 0;
}
