// Wisp: see wisp.h. Every behaviour here comes from look/sketch2/index.html (makeWisp, world,
// face, eyeDist, mouthDist) unless a comment says "invented" or "from face.cpp".
// Single precision only; every literal carries its f.
#include "wisp.h"
#include <math.h>
#include <string.h>
#include "wisp_tables.h"
#include "wisp_mind.h"

namespace {

const int W = WISP_W, H = WISP_H, N = WISP_MOTES;
const int MAX_EXTRA = 48;     // the sketch allows 160; on the chip 48 x 16 bytes
const int MAX_PULSE = 6;
const float POS_Q = 64.0f;    // positions: int16, 1/64 px (range +-512 px)
const float VEL_Q = 32.0f;    // velocities: int16, 1/32 px/s (range +-1024 px/s)
const float PH_Q = 10430.378f;  // phases: uint16, 65536 = one turn
const float FALLBACK_INV = 7.5f;  // 1 / (1.5 x a typical spread), when the model's own table does not fit

// ---------------- expression (the sketch's SH, which is face.cpp's table with the sketch's tints) ----------------
struct Shape {
  float w, h, r, lid, slope, bot, asym, curve, mw, open, gx, gy, en, tr, tg, tb;
};
//                       w   h   r  lidT   slope  lidB  asym   curve  mw  open  gx     gy    energy tint
const Shape SH[WISP_ASLEEP + 1] = {
    /* calm      */ {19, 23, 10, 0.05f, 0.00f, 0.00f, 0.00f, 0.34f, 17, 0.00f, 0.0f, 0.0f, 0.50f, 232, 124, 88},
    /* happy     */ {19, 22, 10, 0.00f, 0.00f, 0.32f, 0.00f, 0.72f, 18, 0.00f, 0.0f, 0.0f, 0.70f, 242, 142, 78},
    /* delighted */ {20, 21, 10, 0.00f, 0.00f, 0.56f, 0.00f, 1.00f, 20, 0.30f, 0.0f, -0.1f, 1.00f, 252, 172, 70},
    /* curious   */ {18, 23, 10, 0.04f, 0.08f, 0.00f, 0.20f, 0.10f, 7, 0.22f, 0.35f, -0.25f, 0.80f, 236, 142, 110},
    /* thinking  */ {18, 21, 10, 0.24f, 0.00f, 0.00f, 0.10f, 0.00f, 9, 0.00f, -0.65f, -0.75f, 1.20f, 222, 132, 122},
    /* sleepy    */ {19, 19, 9, 0.58f, -0.05f, 0.00f, 0.00f, 0.05f, 11, 0.00f, 0.0f, 0.3f, 0.20f, 176, 112, 132},
    /* surprised */ {20, 26, 12, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 8, 0.75f, 0.0f, 0.0f, 1.10f, 255, 176, 124},
    /* sad (low) */ {18, 22, 10, 0.24f, -0.34f, 0.00f, 0.00f, -0.62f, 13, 0.00f, 0.0f, 0.45f, 0.25f, 150, 140, 190},
    /* cheeky    */ {19, 22, 10, 0.10f, 0.10f, 0.22f, -0.16f, 0.70f, 17, 0.00f, 0.3f, 0.0f, 0.80f, 246, 132, 92},
    /* tender    */ {19, 22, 11, 0.08f, -0.15f, 0.20f, 0.00f, 0.46f, 13, 0.00f, 0.0f, 0.1f, 0.40f, 242, 132, 124},
    /* alert     */ {20, 25, 11, 0.00f, 0.10f, 0.00f, 0.00f, 0.00f, 12, 0.00f, 0.0f, 0.0f, 1.00f, 255, 204, 164},
    /* dizzy     */ {18, 20, 9, 0.38f, 0.12f, 0.10f, 0.25f, 0.20f, 12, 0.10f, 0.0f, 0.0f, 0.60f, 236, 130, 110},
    /* asleep    */ {19, 20, 9, 0.93f, 0.00f, 0.00f, 0.00f, 0.15f, 9, 0.00f, 0.0f, 0.3f, 0.08f, 170, 105, 140},
};
static_assert(sizeof(Shape) == 16 * sizeof(float), "Shape is eased as 16 floats in a row");

struct Mote {        // 10 bytes
  int16_t x, y, vx, vy;
  uint16_t ph;
};
struct Extra {       // 16 bytes: a word's mote, yours or the Mac's
  int16_t x, y, vx, vy;
  int8_t hx, hy;     // its place in the puff, Q7
  uint16_t lifeMs;
  uint16_t ph;
  uint8_t mac;
  uint8_t pad;
};
struct Pulse {
  float r, s;
};

struct State {
  Mote m[N];
  int16_t phrase[N];        // the mind's state smoothed over pieces (the sketch's phrase), Q15
  int8_t runMean[N];        // only when the model's tables do not fit n: a running mean of the state, Q7
  Extra ex[MAX_EXTRA];
  Pulse pu[MAX_PULSE];
  uint16_t paperT[64];      // paper colours, swapped: index (hash >> 27) + 32 on a fibre
  Shape cur;
  float t;                  // seconds, wrapped at 3600 (used only for oscillators)
  float cx, cy, cvx, cvy, goalX, goalY, nextGoal, bristle, rot, tiltS;
  float shakeLeft, shakeAmp, flashLeft, gatherLeft;
  float rows, env;
  float gaze[2], gazeT[2], nextSacc, blinkT, nextBlink, blinkAmt;
  float quiet, restless, pc[2];
  float rx, ry, floorY;     // as last drawn
  float lastLookX;
  float pokeX, pokeY, pokeS;
  uint32_t rng;
  int16_t lastN;
  uint8_t nex, npu, pieces, flashMood, wearing;
  bool dbl, bristleNow, asleep, held, whole, seeded;
  WispCost cost;
};
State S;
static_assert(sizeof(State) <= 8192, "Wisp's working memory must stay under 8 KB");

// ---------------- small helpers ----------------
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float mixf(float a, float b, float k) { return a + (b - a) * k; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline int16_t q16(float v, float scale) {
  v *= scale;
  if (v > 32000.0f) v = 32000.0f;
  if (v < -32000.0f) v = -32000.0f;
  return (int16_t)(v >= 0 ? v + 0.5f : v - 0.5f);   // round to nearest: truncation would creep
}
inline float fsin(float x) {   // radians, through the 256-entry table
  int i = (int)(x * 40.743665f + (x >= 0 ? 0.5f : -0.5f));
  return WISP_SIN[i & 255];
}
// the puff's lean is at most 0.3 rad: a short series is exact to 1e-6 there (and smoother than the table)
inline void rotCS(float r, float *c, float *s) {
  float r2 = r * r;
  *c = 1 - r2 * (0.5f - r2 * (1.0f / 24.0f));
  *s = r * (1 - r2 * (1.0f / 6.0f - r2 * (1.0f / 120.0f)));
}
inline uint32_t rnd32() {
  uint32_t x = S.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return S.rng = x;
}
inline float rnd(float a, float b) { return a + (float)(rnd32() >> 8) * (1.0f / 16777216.0f) * (b - a); }
inline uint16_t swap565(int r, int g, int b) {   // 8-bit channels in, byte-swapped RGB565 out
  uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xFF) >> 3));
  return (uint16_t)((c >> 8) | (c << 8));
}
inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

// blend one pixel toward (r,g,b) by a (0..256), in 8-bit channels, stored at 16 bits (the sketch's blend + present)
inline void blendPx(uint16_t *buf, int x, int y, int r, int g, int b, int a) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H || a <= 0) return;
  if (a > 256) a = 256;
  uint16_t *p = buf + y * W + x;
  uint16_t c = (uint16_t)((*p >> 8) | (*p << 8));
  int R = (c >> 11) << 3, G = ((c >> 5) & 63) << 2, B = (c & 31) << 3;
  R += ((r - R) * a + 128) >> 8;
  G += ((g - G) * a + 128) >> 8;
  B += ((b - B) * a + 128) >> 8;
  *p = swap565(R, G, B);
  S.cost.blends++;
}
inline int a256(float a) { return a <= 0 ? 0 : (a >= 1 ? 256 : (int)(a * 256.0f + 0.5f)); }

// ---------------- the paper ----------------
// (246,235,220) with a +-3 grain at fixed addresses and a darker fibre every 23rd diagonal step, as the
// sketch's paper; the grain takes the top 5 bits of the sketch's hash instead of the hash mod 7, so the
// fade's test costs about 10 instructions a pixel instead of 24 (the grain is equally random, not the same;
// each of -3..3 has a share of 4 or 5 in 32, against the sketch's 1 in 7, and the same spread)
const int8_t GRAIN[32] = {-3, 1, -2, 3, 0, -1, 2, -3, 1, -2, 3, 0, -1, 2, -3, 1,
                          -2, 3, 0, -1, 2, -3, 1, -2, 3, 0, -1, 2, -3, -1, 1, 3};
inline int paperIndex(int x, int y) {
  uint32_t n = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
  return (int)(n >> 27) + (((x + y * 3) % 23 == 0) ? 32 : 0);
}
void buildPaper() {
  for (int i = 0; i < 64; i++) {
    int f = GRAIN[i & 31] + (i >= 32 ? -4 : 0);
    S.paperT[i] = swap565(246 + f, 235 + f, 220 + f);
  }
}

// every pixel one step toward its own paper; each step moves a channel by at least one of its own steps
// (the sketch's lesson: a fractional fade in 16 bits stalls short and leaves a green stain)
void fade(uint16_t *buf, int k256, int yEnd) {   // rows from yEnd down are about to be painted as the band
  const uint16_t *pt = S.paperT;
  uint32_t faded = 0;
  for (int y = 0; y < yEnd; y++) {
    uint16_t *row = buf + y * W;
    uint32_t hx = 0, hy = (uint32_t)y * 19349663u;
    int fib = (23 - (y * 3) % 23) % 23;   // pixels until the next fibre ((x + 3y) % 23 == 0)
    for (int x = 0; x < W; x++, hx += 73856093u) {
      int idx = (int)((hx ^ hy) >> 27);
      if (fib == 0) { idx += 32; fib = 23; }
      fib--;
      uint16_t v = row[x], pv = pt[idx];
      if (v == pv) continue;
      // slow path: step each channel toward this pixel's paper
      uint16_t c = (uint16_t)((v >> 8) | (v << 8)), q = (uint16_t)((pv >> 8) | (pv << 8));
      int r = c >> 11, g = (c >> 5) & 63, b = c & 31, d, s;
      // floor of d x k, as the sketch's store-and-mask does; never 0, so every step moves (d < 0 floors to <= -1)
      if ((d = (q >> 11) - r) != 0) { s = (d * k256) >> 8; r += s ? s : 1; }
      if ((d = ((q >> 5) & 63) - g) != 0) { s = (d * k256) >> 8; g += s ? s : 1; }
      if ((d = (q & 31) - b) != 0) { s = (d * k256) >> 8; b += s ? s : 1; }
      c = (uint16_t)((r << 11) | (g << 5) | b);
      row[x] = (uint16_t)((c >> 8) | (c << 8));
      faded++;
    }
  }
  S.cost.faded = faded;
  S.cost.paperFast += (uint32_t)(W * yEnd) - S.cost.faded;
}

// ---------------- the eyes and mouth (the sketch's face(), eyeDist(), mouthDist()) ----------------
struct Eye {
  float cx, cy, w, h, r, lid, slope, bot;
};
struct Mouth {
  float x, y, mw, curve, open, s, hum;
};

inline float eyeDist(const Eye &e, float px, float py) {
  float qx = fabsf(px) - (e.w - e.r), qy = fabsf(py) - (e.h - e.r);
  float out;
  if (qx > 0 && qy > 0) out = sqrtf(qx * qx + qy * qy);   // only in the four corners
  else out = maxf(qx, 0) + maxf(qy, 0);
  float d = out + minf(maxf(qx, qy), 0) - e.r;
  float lidY = -e.h + e.lid * 2 * e.h + e.slope * px;
  d = maxf(d, lidY - py);
  if (e.bot > 0.01f) {
    float R = e.w * 1.7f, ccy = e.h + R - e.bot * 2 * e.h, dy = py - ccy, q = px * px + dy * dy;
    if (q < (R + 1) * (R + 1)) d = maxf(d, R - sqrtf(q));   // farther out it cannot matter: d >= -1 is needed to show
  }
  return d;
}

void drawEye(uint16_t *buf, const Eye &e) {
  int y0 = (int)floorf(e.cy - e.h - 2), y1 = (int)(e.cy + e.h + 2), x0 = (int)floorf(e.cx - e.w - 2), x1 = (int)(e.cx + e.w + 2);
  for (int y = y0; y <= y1; y++) {
    float ey = y + 0.5f - e.cy;
    for (int x = x0; x <= x1; x++) {
      float ex = x + 0.5f - e.cx, d = eyeDist(e, ex, ey);
      S.cost.shapePx++;
      float cov = clampf(0.5f - d, 0, 1);
      if (cov <= 0) continue;
      blendPx(buf, x, y, 52, 26, 28, a256(cov));
      float gx = ex + e.w * 0.35f, gy = ey + e.h * 0.4f, gl = 1 - (gx * gx + gy * gy) * 0.25f;
      if (gl > 0 && d < -1) blendPx(buf, x, y, 255, 248, 238, a256(minf(1, gl * 1.6f)));
    }
  }
}

void drawMouth(uint16_t *buf, const Mouth &m) {
  const float thick = 0.75f;
  int y0 = (int)floorf(m.y - 10), y1 = (int)(m.y + 12), x0 = (int)floorf(m.x - m.mw - 3), x1 = (int)(m.x + m.mw + 3);
  if (m.open > 1.6f) {
    float ax = 1.0f / (m.mw * 0.62f + 2), ay = 1.0f / (m.open + 1.5f);
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        float nx = (x + 0.5f - m.x) * ax, ny = (y + 0.5f - m.y) * ay;
        float d = (sqrtf(nx * nx + ny * ny) - 1) * (m.open + 1.5f);
        S.cost.shapePx++;
        if (d < 0.5f) blendPx(buf, x, y, 70, 30, 30, a256(clampf(0.5f - d, 0, 1)));
      }
    return;
  }
  float inv = 1.0f / maxf(1, m.mw);
  for (int x = x0; x <= x1; x++) {
    float mx = x + 0.5f - m.x, k = mx * inv;
    float yy = m.curve * 8.5f * m.s * (1 - 2 * k * k) + m.hum * 1.2f * fsin(k * 9 + S.t * 30);
    float over = fabsf(mx) - m.mw;
    for (int y = y0; y <= y1; y++) {
      float my = y + 0.5f - m.y, d = fabsf(my - yy) - thick;
      if (over > 0) d = d > 0 ? sqrtf(d * d + over * over) : over;
      S.cost.shapePx++;
      if (d < 0.5f) blendPx(buf, x, y, 70, 30, 30, a256(clampf(0.5f - d, 0, 1)));
    }
  }
}

// the shadow: the sketch's ellipse(), with the square root only in the rim
void drawEllipse(uint16_t *buf, float cx, float cy, float rx, float ry, int r, int g, int b, float a) {
  if (rx < 0.5f || ry < 0.5f || a <= 0) return;
  float mn = minf(rx, ry), lo = 1 - 0.5f / mn, hi = 1 + 0.5f / mn;
  float lo2 = lo > 0 ? lo * lo : 0, hi2 = hi * hi, irx = 1 / rx, iry = 1 / ry;
  int A = a256(a);
  for (int y = (int)floorf(cy - ry - 1); y <= cy + ry + 1; y++) {
    float ny = (y + 0.5f - cy) * iry;
    for (int x = (int)floorf(cx - rx - 1); x <= cx + rx + 1; x++) {
      float nx = (x + 0.5f - cx) * irx, q = nx * nx + ny * ny;
      S.cost.shapePx++;
      if (q >= hi2) continue;
      if (q <= lo2) { blendPx(buf, x, y, r, g, b, A); continue; }
      float d = (sqrtf(q) - 1) * mn;
      blendPx(buf, x, y, r, g, b, a256(a * clampf(0.5f - d, 0, 1)));
    }
  }
}

// ---------------- the mind ----------------
inline bool ownTables(int n) { return n == WISP_MIND_N; }
inline float meanOf(int u, bool own) { return own ? WISP_MIND_MEAN[u] : S.runMean[u] * (1.0f / 127.0f); }
inline float devOf(int u, bool own) {   // the sketch's MIND.dev
  float v = (S.phrase[u] * (1.0f / 32767.0f) - meanOf(u, own)) * (own ? WISP_MIND_INV[u] : FALLBACK_INV);
  return clampf(v, -1, 1);
}

void resetPhrase(int n) {
  bool own = ownTables(n);
  memset(S.runMean, 0, sizeof(S.runMean));
  S.seeded = own;
  for (int u = 0; u < N; u++) S.phrase[u] = q16(u < n ? meanOf(u, own) : 0.0f, 32767.0f);
  S.lastN = (int16_t)n;
}

void mindUpdate(const WispInput &in, float dt) {
  int n = in.state ? clampi(in.n, 0, N) : 0;
  if (n != S.lastN) resetPhrase(n);
  bool own = ownTables(n);
  if (S.pieces) {
    // each piece moves the phrase 1/8 of the way to the live state
    if (n && !own) {
      // no tables for this mind: the first piece seeds a running mean, later pieces pull it 1/64 of the way
      for (int u = 0; u < n; u++) {
        int v = (int)(clampf(in.state[u], -1, 1) * 127.0f + (in.state[u] >= 0 ? 0.5f : -0.5f));
        if (!S.seeded) { S.runMean[u] = (int8_t)v; S.phrase[u] = q16(v * (1.0f / 127.0f), 32767.0f); continue; }
        int d = v - S.runMean[u];
        if (!d) continue;
        int s = d / 64;
        if (s == 0) s = d > 0 ? 1 : -1;
        S.runMean[u] = (int8_t)(S.runMean[u] + s);
      }
      S.seeded = true;
    }
    if (n) {
      float keep = 1;
      for (int i = 0; i < S.pieces; i++) keep *= 0.875f;
      float k = 1 - keep;
      for (int u = 0; u < n; u++) {
        float tgt = clampf(in.state[u], -1, 1) * 32767.0f;
        S.phrase[u] = q16(S.phrase[u] + (tgt - S.phrase[u]) * k, 1.0f);
      }
    }
    S.quiet = 0;
    S.pieces = 0;
  }
  S.quiet += dt;
  // with nothing new, the phrase relaxes to the model's mean over about 3 s (at least one step, so it arrives)
  if (S.quiet > 0.4f && n) {
    float k = 1 - expf(-dt / 3);
    for (int u = 0; u < n; u++) {
      int m = q16(meanOf(u, own), 32767.0f), d = m - S.phrase[u];
      if (!d) continue;
      float fs = (float)d * k;
      int s = (int)(fs >= 0 ? fs + 0.5f : fs - 0.5f);   // rounded: truncating would settle more slowly than the sketch
      if (s == 0) s = d > 0 ? 1 : -1;
      S.phrase[u] = (int16_t)(S.phrase[u] + s);
    }
  }
  float rest = 0;
  if (n >= 2) {
    float s = 0;
    for (int u = 0; u < n; u += 2) {
      float d = (S.phrase[u] * (1.0f / 32767.0f) - meanOf(u, own)) * (own ? WISP_MIND_INV[u] : FALLBACK_INV);
      s += d * d;
    }
    rest = clampf(sqrtf(s / (float)((n + 1) / 2)) * 1.4f, 0, 1);
  }
  S.restless += (rest - S.restless) * minf(1, dt * 4);
  S.pc[0] = S.pc[1] = 0;
  if (own) {
    float d0 = 0, d1 = 0;
    for (int u = 0; u < n; u++) {
      float d = S.phrase[u] * (1.0f / 32767.0f) - WISP_MIND_MEAN[u];
      d0 += d * WISP_MIND_PC0[u];
      d1 += d * WISP_MIND_PC1[u];
    }
    S.pc[0] = clampf(d0 / (WISP_MIND_PCSD[0] * 2), -1, 1);
    S.pc[1] = clampf(d1 / (WISP_MIND_PCSD[1] * 2), -1, 1);
  }
}

// ---------------- the puff ----------------
void sizeNow(bool asleep, bool sleepy, float *rx, float *ry) {
  float x = 34, y = 28;
  if (asleep) { x = 40; y = 15; }
  else if (sleepy) { x = 37; y = 22; }
  if (S.rows > 20) { x *= 0.85f; y *= 0.8f; }
  float breath = 1 + (asleep ? 0.05f : 0.025f) * fsin(S.t * (asleep ? 1.3f : 1.9f)) + 0.07f * S.env;
  float b = 1 + S.bristle * 0.4f;
  *rx = x * breath * b;
  *ry = y * breath * b;
}

void pushPulse(float s) {
  if (S.npu == MAX_PULSE) {
    memmove(S.pu, S.pu + 1, sizeof(Pulse) * (MAX_PULSE - 1));
    S.npu--;
  }
  S.pu[S.npu].r = 0;
  S.pu[S.npu].s = s;
  S.npu++;
}

void pushExtra(float x, float y, float vx, float vy, float hx, float hy, float life, bool mac) {
  if (S.nex == MAX_EXTRA) {
    memmove(S.ex, S.ex + 1, sizeof(Extra) * (MAX_EXTRA - 1));
    S.nex--;
  }
  Extra &e = S.ex[S.nex++];
  e.x = q16(x, POS_Q);
  e.y = q16(y, POS_Q);
  e.vx = q16(vx, VEL_Q);
  e.vy = q16(vy, VEL_Q);
  e.hx = (int8_t)clampi((int)(hx * 127.0f), -127, 127);
  e.hy = (int8_t)clampi((int)(hy * 127.0f), -127, 127);
  e.lifeMs = (uint16_t)(life * 1000.0f);
  e.ph = (uint16_t)(rnd32() >> 16);
  e.mac = mac ? 1 : 0;
  e.pad = 0;
}

void flashFor(uint8_t mood, float sec) {
  S.flashMood = mood;
  S.flashLeft = sec;
}

}  // namespace

// =====================================================================================
void wisp_begin(uint32_t seed) {
  memset(&S, 0, sizeof(S));
  S.rng = seed ? seed : 0x9E3779B9u;
  buildPaper();
  S.cur = SH[WISP_CALM];
  S.flashMood = WISP_CALM;
  S.cx = 120;
  S.cy = 50;
  S.goalX = 120;
  S.goalY = 52;
  S.nextGoal = 2;
  S.nextBlink = 2;
  S.blinkT = -1;
  S.lastLookX = 8;   // the sketch's starting cursor
  for (int i = 0; i < N; i++) {
    S.m[i].x = q16(S.cx + WISP_HOME_X[i] * (34.0f / 16384.0f), POS_Q);
    S.m[i].y = q16(S.cy + WISP_HOME_Y[i] * (28.0f / 16384.0f), POS_Q);
    S.m[i].ph = (uint16_t)(rnd32() >> 16);
  }
  resetPhrase(WISP_MIND_N);
  S.rx = 34;
  S.ry = 28;
  S.floorY = H - 2;
}

void wisp_repaint(uint16_t *buf) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) buf[y * W + x] = S.paperT[paperIndex(x, y)];
}

uint16_t wisp_paper(int x, int y) {
  if (S.paperT[0] == 0) buildPaper();
  return S.paperT[paperIndex(clampi(x, 0, W - 1), clampi(y, 0, H - 1))];
}

void wisp_blink() {
  if (S.blinkT < 0) S.blinkT = 0;
}

// invented (the sketch has no coming-together for Wisp): the motes are strewn over the paper and
// fly home on their own springs; the eyes stay shut until they have nearly arrived
void wisp_wake() {
  float fy = S.floorY;
  for (int i = 0; i < N; i++) {
    S.m[i].x = q16(rnd(3, W - 3), POS_Q);
    S.m[i].y = q16(rnd(3, fy - 2), POS_Q);
    S.m[i].vx = S.m[i].vy = 0;
  }
  S.gatherLeft = 0.9f;
  flashFor(WISP_DELIGHTED, 1.2f);   // the sketch's wake()
}

void wisp_scatter(float strength) {
  S.shakeLeft = 1.3f;
  S.shakeAmp = clampf(strength, 0, 3);
  flashFor(WISP_SURPRISED, 1.3f);
}

void wisp_bristle() {
  S.bristleNow = true;
  flashFor(WISP_SURPRISED, 1.4f);
}

void wisp_piece(float effort) {
  pushPulse(0.4f + 0.6f * clampf(effort, 0, 1));
  if (S.pieces < 255) S.pieces++;
}

void wisp_word_in(bool fromMac) {
  if (fromMac)
    for (int k = 0; k < 2; k++) pushExtra(rnd(20, 220), -2, 0, 30, rnd(-1, 1) * 0.8f, rnd(-1, 1) * 0.8f, 9, true);
  else
    for (int k = 0; k < 5; k++) pushExtra(S.lastLookX, H - S.rows + 4, rnd(-20, 20), -40, rnd(-1, 1) * 0.7f, rnd(-1, 1) * 0.7f, 4, false);
}

void wisp_poke(float x, float y, float strength) {
  S.pokeX = x;
  S.pokeY = y;
  S.pokeS = clampf(strength, 0, 2);
}

void wisp_flash(uint8_t mood, float seconds) {
  if (mood <= WISP_ASLEEP) flashFor(mood, seconds);
}

void wisp_center(float *x, float *y) {
  *x = S.cx;
  *y = S.cy;
}
void wisp_head(float *x, float *y) {
  *x = S.cx;
  *y = S.cy - S.ry;
}
float wisp_floor() { return S.floorY; }

void wisp_thought_at(float bx, float by, float age, int lines, float *x, float *y) {
  float tx = clampf(bx + age * 2, 52, W - 52), y0 = by - 8 - age * 3 - lines * 10;
  if (y0 < 2) {
    tx = bx < W / 2 ? clampf(bx + 78, 52, W - 52) : clampf(bx - 78, 52, W - 52);
    y0 = clampf(by - age * 3, 2, H - S.rows - lines * 10 - 2);
  }
  *x = tx;
  *y = y0;
}

int wisp_zs(WispMark *out, int max) {
  if (!S.asleep) return 0;
  int k = 0;
  for (int i = 0; i < 3 && k < max; i++) {
    float p = S.t * 0.3f + i / 3.0f;
    p -= floorf(p);
    out[k].x = S.cx + 10 + p * 20;
    out[k].y = S.cy - S.ry - 4 - p * 26;
    out[k].alpha = 1 - p;
    out[k].px = p < 0.5f ? 8 : 11;
    k++;
  }
  return k;
}

uint8_t wisp_wearing() { return S.wearing; }
uint16_t wisp_tint() { return swap565((int)S.cur.tr, (int)S.cur.tg, (int)S.cur.tb); }
float wisp_restless() { return S.restless; }
unsigned wisp_state_bytes() { return (unsigned)sizeof(State); }
WispCost wisp_cost() { return S.cost; }
void wisp_mote_centroid(float *x, float *y) {
  float sx = 0, sy = 0;
  for (int i = 0; i < N; i++) {
    sx += S.m[i].x;
    sy += S.m[i].y;
  }
  *x = sx / (N * POS_Q);
  *y = sy / (N * POS_Q);
}

// =====================================================================================
void wisp_frame(uint16_t *buf, const WispInput &in) {
  memset(&S.cost, 0, sizeof(S.cost));
  float dt = clampf(in.dt, 0, 0.1f);
  S.t += dt;
  if (S.t > 3600) S.t -= 3600;
  const bool asleep = in.asleep, sleepy = !asleep && in.mood == WISP_SLEEPY;

  // ---- what happens to it ----
  S.tiltS += (clampf(in.tiltX, -1, 1) - S.tiltS) * minf(1, dt * 5);   // the sensor, smoothed (tiltS is sin of the tilt)
  float v = clampf(in.voice, 0, 1);
  S.env = v > 0.001f ? v : S.env * expf(-dt * 15.3f);   // the sketch: the voice while sounding, then x0.6 a frame
  if (S.env < 0.002f) S.env = 0;
  const bool humming = S.env > 0;
  S.rows += (clampf(in.textRows, 0, H - 40) - S.rows) * minf(1, dt * 12);
  if (S.shakeLeft > 0) {
    S.shakeLeft -= dt;
    if (S.shakeLeft <= 0 && !asleep) flashFor(WISP_DIZZY, 1.8f);   // the sketch: dizzy once the shaking stops
  }
  if (in.held && !S.held) flashFor(WISP_ALERT, 1000);      // picked up: alert while held
  if (!in.held && S.held && S.flashMood == WISP_ALERT) S.flashLeft = 0;
  S.held = in.held;
  S.whole = in.whole;
  if (S.flashLeft > 0) S.flashLeft -= dt;
  if (S.gatherLeft > 0) S.gatherLeft -= dt;
  if (S.asleep && !asleep) S.blinkT = -1;
  S.asleep = asleep;

  // ---- the expression it wears (asleep, a passing flash, sleepy, thinking, the mood it rests in) ----
  uint8_t wear = asleep ? (uint8_t)WISP_ASLEEP
               : S.flashLeft > 0 ? S.flashMood
               : sleepy ? (uint8_t)WISP_SLEEPY
               : in.thinking ? (uint8_t)WISP_THINKING
               : (in.mood < WISP_MOOD_COUNT ? in.mood : (uint8_t)WISP_CALM);
  S.wearing = wear;
  {
    float k = 1 - expf(-dt * 9);
    float *c = (float *)&S.cur;
    const float *g = (const float *)&SH[wear];
    for (int i = 0; i < 16; i++) c[i] += (g[i] - c[i]) * k;
  }

  // ---- the mind ----
  mindUpdate(in, dt);

  // ---- gaze: the point it attends to, else saccades leaning where the mind leans ----
  if (in.looking) {
    S.lastLookX = in.lookX;
    S.gazeT[0] = clampf((in.lookX - S.cx) / 70, -1, 1);
    S.gazeT[1] = clampf((in.lookY - S.cy) / 40, -1, 1);
  }
  S.nextSacc -= dt;
  if (!in.looking && S.nextSacc <= 0) {
    if (asleep) { S.gazeT[0] = 0; S.gazeT[1] = 0.3f; }
    else if (rnd(0, 1) < 0.35f) { S.gazeT[0] = S.cur.gx; S.gazeT[1] = S.cur.gy; }
    else {
      S.gazeT[0] = clampf(S.cur.gx + S.pc[0] * 0.6f + rnd(-0.5f, 0.5f), -1, 1);
      S.gazeT[1] = clampf(S.cur.gy + S.pc[1] * 0.4f + rnd(-0.35f, 0.35f), -1, 1);
    }
    S.nextSacc = rnd(0.9f, 3.8f) * (1 - 0.5f * S.restless);
  }
  {
    float gk = 1 - expf(-dt * 16);
    S.gaze[0] += (S.gazeT[0] - S.gaze[0]) * gk;
    S.gaze[1] += (S.gazeT[1] - S.gaze[1]) * gk;
  }

  // ---- blinks (with face.cpp's occasional double blink) ----
  if (S.blinkT >= 0) {
    S.blinkT += dt;
    if (S.blinkT > 0.24f) {
      S.blinkT = -1;
      if (S.dbl) { S.dbl = false; S.nextBlink = 0.09f; }
    }
  } else {
    S.nextBlink -= dt;
    if (S.nextBlink <= 0 && !asleep) {
      S.blinkT = 0;
      S.dbl = rnd(0, 1) < 0.18f;
      S.nextBlink = rnd(1.6f, 5.8f) * (S.cur.lid > 0.4f ? 0.5f : 1);
    }
  }
  float bt = S.blinkT;
  S.blinkAmt = bt < 0 ? 0 : bt < 0.07f ? bt / 0.07f : bt < 0.11f ? 1 : 1 - (bt - 0.11f) / 0.13f;
  if (S.gatherLeft > 0.3f) S.blinkAmt = 1;

  // ---- where the puff goes ----
  const float fy = H - S.rows - 2;
  S.floorY = fy;
  float rx, ry;
  sizeNow(asleep, sleepy, &rx, &ry);
  S.nextGoal -= dt;
  if (asleep) S.goalY = fy - ry - 1;
  else if (in.looking) { S.goalX = clampf(in.lookX + 20, 45, 195); S.goalY = fy - ry - 10; }
  else if (in.thinking) S.goalY = 34 + ry * 0.3f;
  else if (S.nextGoal <= 0) {
    S.goalX = rnd(50, 190);
    S.goalY = rnd(ry + 6, fy - ry - 8);
    S.nextGoal = rnd(4, 9) * (1 - 0.5f * S.restless);
  }
  if (in.held) S.goalY = fy - ry - 22;
  const bool shaking = S.shakeLeft > 0;
  S.cvx += ((S.goalX - S.cx) * 1.4f - S.cvx * 1.8f + S.tiltS * 90) * dt;
  S.cvy += ((S.goalY - S.cy) * 1.4f - S.cvy * 1.8f) * dt;
  if (in.held) { S.cvx += rnd(-80, 80) * dt; S.cvy += rnd(-80, 80) * dt; }
  if (shaking) { S.cvx += rnd(-1400, 1400) * dt * S.shakeAmp; S.cvy += rnd(-900, 900) * dt * S.shakeAmp; }
  const bool loud = S.bristleNow;
  S.bristleNow = false;
  if (loud) { S.bristle = 1; S.cvy -= 40; }
  S.bristle *= expf(-dt * 2.2f);
  S.cx = clampf(S.cx + S.cvx * dt, rx + 2, W - rx - 2);
  S.cy = clampf(S.cy + S.cvy * dt, ry + 2, fy - ry);
  S.rot += (clampf(S.cvx * 0.004f, -0.3f, 0.3f) - S.rot) * minf(1, dt * 4);
  for (int i = 0; i < S.npu; i++) S.pu[i].r += dt * 40;
  while (S.npu && S.pu[0].r > 50) {
    memmove(S.pu, S.pu + 1, sizeof(Pulse) * (MAX_PULSE - 1));
    S.npu--;
  }

  // ---- the motes: each rides a home in the puff; how far and how fast it swirls is its unit's live deviation ----
  {
    int n = in.state ? clampi(in.n, 0, N) : 0;
    bool own = ownTables(n);
    const float spin = (0.4f + 3 * S.restless) * (asleep ? 0.2f : 1), K = shaking ? 4.0f : 38.0f, damp = shaking ? 1.0f : 7.0f;
    float cr, sr;
    rotCS(S.rot, &cr, &sr);
    const float sag = S.tiltS * 3;
    const float orbK = 4.5f * (0.25f + S.restless), orbS = asleep ? 0.3f : 1.0f;
    const float listen = in.listening ? clampf(in.room, 0, 1) * 2.5f : 0;   // invented: the puff stirs with the room
    const float shakeV = 900 * dt * S.shakeAmp;
    const float humK = S.env * 40 * dt, humPh = S.t * 25, mouthY = S.cy + ry * 0.45f;
    const bool poke = S.pokeS > 0;
    const float pokeR = 28;
    const float sx = rx * (1.0f / 16384.0f), sy = ry * (1.0f / 16384.0f);
    for (int i = 0; i < N; i++) {
      Mote &m = S.m[i];
      float d = 0;
      if (n) d = devOf(own ? WISP_MIND_ORDER[i] : i % n, own);
      float ad = fabsf(d);
      float dph = dt * spin * (0.5f + ad) * PH_Q;
      m.ph = (uint16_t)(m.ph + (int)(d >= 0 ? dph + 0.5f : -dph - 0.5f));
      float orb = ((0.6f + orbK * ad) * orbS) + listen;
      int pi = m.ph >> 8;
      float co = WISP_SIN[(pi + 64) & 255], si = WISP_SIN[pi];
      float hx = WISP_HOME_X[i] * sx, hy = WISP_HOME_Y[i] * sy;
      float tx = S.cx + hx * cr - hy * sr + co * orb + sag * (1 - WISP_HOME_Y[i] * (1.0f / 16384.0f)) * 0.5f;
      float ty = S.cy + hx * sr + hy * cr + si * orb;
      float X = m.x * (1.0f / POS_Q), Y = m.y * (1.0f / POS_Q), VX = m.vx * (1.0f / VEL_Q), VY = m.vy * (1.0f / VEL_Q);
      VX += ((tx - X) * K - VX * damp) * dt;
      VY += ((ty - Y) * K - VY * damp) * dt;
      if (shaking) { VX += rnd(-1, 1) * shakeV; VY += rnd(-1, 1) * shakeV; }
      if (loud) { VX += WISP_HOME_X[i] * (140.0f / 16384.0f); VY += WISP_HOME_Y[i] * (140.0f / 16384.0f); }
      if (humming) {
        float mdx = X - S.cx, mdy = Y - mouthY, md = sqrtf(mdx * mdx + mdy * mdy) + 1;
        float w = humK * fsin(md * 0.8f - humPh) / md;
        VX += mdx * w;
        VY += mdy * w;
      }
      if (poke) {   // invented: a key pressed nearby makes the nearest motes flinch away
        float px = X - S.pokeX, py = Y - S.pokeY, q = px * px + py * py;
        if (q < pokeR * pokeR) {
          float dd = sqrtf(q) + 0.5f, kk = S.pokeS * 60 * (1 - dd / pokeR) / dd;
          VX += px * kk;
          VY += py * kk;
        }
      }
      X += VX * dt;
      Y += VY * dt;
      if (Y > fy) { Y = fy; VY *= -0.3f; }
      m.x = q16(X, POS_Q);
      m.y = q16(Y, POS_Q);
      m.vx = q16(VX, VEL_Q);
      m.vy = q16(VY, VEL_Q);
    }
    S.pokeS = 0;
    S.cost.motes = N;
  }

  // ---- words' motes: yours rise from the cursor, the Mac's fall from the top and float off when it leaves ----
  {
    int w = 0;
    for (int i = 0; i < S.nex; i++) {
      Extra e = S.ex[i];
      float life = e.lifeMs * 0.001f - dt * (in.whole || !e.mac ? 1 : 3);
      e.ph = (uint16_t)(e.ph + (int)(dt * 2 * PH_Q + 0.5f));
      float x = e.x * (1.0f / POS_Q), y = e.y * (1.0f / POS_Q), vx = e.vx * (1.0f / VEL_Q), vy = e.vy * (1.0f / VEL_Q);
      float hx = e.hx * (1.0f / 127.0f), hy = e.hy * (1.0f / 127.0f);
      bool leaving = e.mac && !in.whole;
      int pi = e.ph >> 8;
      float tx = leaving ? x + hx * 10 : S.cx + hx * rx + WISP_SIN[(pi + 64) & 255] * 2;
      float ty = leaving ? -20 : S.cy + hy * ry + WISP_SIN[pi] * 2;
      float kk = leaving ? 2.0f : 10.0f;
      vx += ((tx - x) * kk - vx * 3) * dt;
      vy += ((ty - y) * kk - vy * 3) * dt;
      x += vx * dt;
      y += vy * dt;
      if (life <= 0 || y < -15) continue;
      e.x = q16(x, POS_Q);
      e.y = q16(y, POS_Q);
      e.vx = q16(vx, VEL_Q);
      e.vy = q16(vy, VEL_Q);
      e.lifeMs = (uint16_t)(life * 1000.0f);
      S.ex[w++] = e;
    }
    S.nex = (uint8_t)w;
    S.cost.motes += (uint32_t)w;
  }

  // ===================== draw =====================
  const int bandTop = S.rows > 1 ? (int)(H - S.rows + 0.5f) : H;
  fade(buf, shaking ? 46 : 77, bandTop);   // k = 0.18 while shaken, else 0.3 (x256)
  sizeNow(asleep, sleepy, &rx, &ry);
  S.rx = rx;
  S.ry = ry;

  // its shadow on the floor: smaller and paler the higher it floats
  {
    float hgt = clampf((fy - (S.cy + ry)) / 60, 0, 1);
    drawEllipse(buf, S.cx, fy, rx * (0.9f - 0.4f * hgt), 2.6f, 205, 180, 160, 0.35f * (1 - hgt * 0.7f));
  }

  // motes: deep coral at the core to the mood's tint at the rim; a piece is a darker ring passing outward
  {
    const float tr = S.cur.tr, tg = S.cur.tg * 0.95f, tb = S.cur.tb * 0.9f;
    const float a = asleep ? 0.7f : 0.9f;
    const int A0 = a256(a), A1 = a256(a * 0.8f), A2 = a256(a * 0.6f);
    for (int i = 0; i < N; i++) {
      float rr = WISP_HOME_R[i] * (1.0f / 256.0f), deep = 0.35f + 0.65f * rr;
      for (int k = 0; k < S.npu; k++) {
        float dd = fabsf(rr * 30 - S.pu[k].r);
        if (dd < 4) deep -= 0.5f * S.pu[k].s * (1 - dd * 0.25f);
      }
      deep = clampf(deep, 0, 1);
      int r = (int)(mixf(150, tr, deep) + 0.5f), g = (int)(mixf(46, tg, deep) + 0.5f), b = (int)(mixf(40, tb, deep) + 0.5f);
      int x = S.m[i].x / 64, y = S.m[i].y / 64;   // toward zero, as the sketch's x | 0
      blendPx(buf, x, y, r, g, b, A0);
      blendPx(buf, x + 1, y, r, g, b, A1);
      blendPx(buf, x, y + 1, r, g, b, A1);
      blendPx(buf, x + 1, y + 1, r, g, b, A2);
    }
    for (int i = 0; i < S.nex; i++) {
      const Extra &e = S.ex[i];
      int A = a256(clampf(e.lifeMs * 0.001f, 0, 1));
      int r = e.mac ? 112 : 60, g = e.mac ? 72 : 112, b = e.mac ? 150 : 160;
      int x = e.x / 64, y = e.y / 64;
      blendPx(buf, x, y, r, g, b, A);
      blendPx(buf, x + 1, y, r, g, b, A);
      blendPx(buf, x, y + 1, r, g, b, A);
      blendPx(buf, x + 1, y + 1, r, g, b, A);
    }
  }

  // the face: the dot face's eyes, dark ink on the warm puff, with a glint; half the old size
  {
    const float s = 0.5f * clampf(ry / 28, 0.6f, 1.05f);
    const float fcx = S.cx, fcy = S.cy - ry * 0.18f, spacing = 15 * (rx / 34);
    float cr, sr;
    rotCS(S.rot, &cr, &sr);
    const float gx = S.gaze[0], gy = S.gaze[1], blink = S.blinkAmt;
    const Shape &c = S.cur;
    for (int i = 0; i < 2; i++) {
      float side = i ? 1.0f : -1.0f, grow = 1 + c.asym * (i ? 1.0f : -0.6f);
      float lid = c.lid;
      if (c.asym < 0 && i == 1) lid += -c.asym * 1.6f;   // the cheeky wink
      Eye e;
      e.w = c.w * s * (1 + 0.07f * gx * side);
      e.h = c.h * s * grow;
      e.r = minf(minf(c.r * s, e.w), e.h);
      e.lid = minf(0.93f, maxf(lid, blink));
      e.slope = c.slope * (i ? -1.0f : 1.0f);
      e.bot = c.bot * (1 - blink * 0.6f);
      float ox = side * spacing + gx * 9 * s, oy = gy * 6 * s;
      e.cx = fcx + ox * cr - oy * sr;
      e.cy = fcy + ox * sr + oy * cr;
      drawEye(buf, e);
    }
    Mouth m;
    float mo = 37 * s;
    m.x = fcx + gx * 5 * s - mo * sr;
    m.y = fcy + gy * 3 * s + mo * cr;
    m.mw = c.mw * s * (1 - 0.3f * minf(1, S.env * 1.5f));
    m.curve = c.curve;
    m.open = c.open * 6.5f * s + (humming && in.whole ? S.env * 8 * s : 0);   // with the whole mind here it speaks
    m.s = s;
    m.hum = humming && !in.whole ? S.env : 0;                                // alone, it hums with its mouth shut
    drawMouth(buf, m);
  }

  // the text band (not text: the ground the firmware writes its words on)
  if (bandTop < H) {
    uint16_t band = swap565(236, 222, 205);
    for (int i = bandTop * W; i < W * H; i++) buf[i] = band;
    S.cost.bandPx = (uint32_t)((H - bandTop) * W);
  }
}
