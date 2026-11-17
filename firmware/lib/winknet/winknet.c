#include "winknet.h"
#include <math.h>
#include <string.h>

/* The file (written by train/mindlib.py): "WNK2", V, E, H (int32 each), then, each
 * 8-bit table padded to a multiple of four bytes:
 *   emb_s f32, emb  int8[V*E]
 *   wi_s  f32, wi   int8[3H*E]
 *   wh_s  f32, wh   int8[3H*H]
 *   bi f32[3H], bh f32[3H]
 *   proj_s f32, proj int8[E*H]
 *   pb f32[E], bo f32[V]
 */
static size_t pad4(size_t n) { return (n + 3) & ~(size_t)3; }

int mind_load(MindModel *m, const uint8_t *b, size_t n) {
  if (n < 16 || memcmp(b, "WNK2", 4)) return -1;
  int32_t dims[3];
  memcpy(dims, b + 4, 12);
  m->V = dims[0]; m->E = dims[1]; m->H = dims[2];
  size_t V = m->V, E = m->E, H = m->H, p = 16;
#define TAKE_SCALE(dst) do { memcpy(&(dst), b + p, 4); p += 4; } while (0)
#define TAKE_I8(dst, count) do { (dst) = (const int8_t *)(b + p); p += pad4(count); } while (0)
#define TAKE_F32(dst, count) do { (dst) = (const float *)(b + p); p += 4 * (count); } while (0)
  TAKE_SCALE(m->emb_s); TAKE_I8(m->emb, V * E);
  TAKE_SCALE(m->wi_s);  TAKE_I8(m->wi, 3 * H * E);
  TAKE_SCALE(m->wh_s);  TAKE_I8(m->wh, 3 * H * H);
  TAKE_F32(m->bi, 3 * H); TAKE_F32(m->bh, 3 * H);
  TAKE_SCALE(m->proj_s); TAKE_I8(m->proj, E * H);
  TAKE_F32(m->pb, E); TAKE_F32(m->bo, V);
  return p <= n ? 0 : -2;
}

/* scratch: hq int8[H], yq int8[E], then hn f32[H], y f32[E] */
size_t mind_scratch_bytes(const MindModel *m) {
  return pad4((size_t)m->H) + pad4((size_t)m->E) + ((size_t)m->H + (size_t)m->E) * sizeof(float);
}

static inline int32_t dot8(const int8_t *a, const int8_t *b, int n) {
  int32_t s = 0;
  int i = 0;
  for (; i + 4 <= n; i += 4) s += a[i] * b[i] + a[i + 1] * b[i + 1] + a[i + 2] * b[i + 2] + a[i + 3] * b[i + 3];
  for (; i < n; i++) s += a[i] * b[i];
  return s;
}

static inline float sigmoidf(float x) { return 1.0f / (1.0f + expf(-x)); }

static inline int8_t q8(float v) { return (int8_t)(v > 127 ? 127 : v < -127 ? -127 : (v >= 0 ? v + 0.5f : v - 0.5f)); }

void mind_step(const MindModel *m, float *h, int token, float *logits, void *scratch) {
  const int E = m->E, H = m->H;
  int8_t *hq = (int8_t *)scratch;
  int8_t *yq = hq + pad4((size_t)H);
  float *hn = (float *)(yq + pad4((size_t)E));
  float *y = hn + H;
  if (token < 0 || token >= m->V) token = 0;
  const int8_t *x = m->emb + (size_t)token * E;
  for (int j = 0; j < H; j++) hq[j] = q8(h[j] * 127.0f);

  const float si = m->wi_s * m->emb_s, sh = m->wh_s / 127.0f;
  /* one unit of the state at a time: its three gates from the input and from the old state */
#define GI(k) (si * dot8(m->wi + (size_t)(k) * E, x, E) + m->bi[k])
#define GH(k) (sh * dot8(m->wh + (size_t)(k) * H, hq, H) + m->bh[k])
  for (int j = 0; j < H; j++) {
    float r = sigmoidf(GI(j) + GH(j));
    float z = sigmoidf(GI(H + j) + GH(H + j));
    float nn = tanhf(GI(2 * H + j) + r * GH(2 * H + j));
    hn[j] = (1.0f - z) * nn + z * h[j];
  }
  memcpy(h, hn, (size_t)H * sizeof(float));
  if (!logits) return;

  /* the vector of the piece it wants next, then how close each piece's vector is to it */
  for (int j = 0; j < H; j++) hq[j] = q8(h[j] * 127.0f);
  const float sp = m->proj_s / 127.0f;
  float top = 1e-6f;
  for (int e = 0; e < E; e++) {
    y[e] = sp * dot8(m->proj + (size_t)e * H, hq, H) + m->pb[e];
    float a = fabsf(y[e]);
    if (a > top) top = a;
  }
  const float ys = top / 127.0f;
  for (int e = 0; e < E; e++) yq[e] = q8(y[e] / ys);
  const float so = m->emb_s * ys;
  for (int v = 0; v < m->V; v++) logits[v] = so * dot8(m->emb + (size_t)v * E, yq, E) + m->bo[v];
}

static uint32_t next_rand(uint32_t *s) {
  uint32_t x = *s ? *s : 0x9E3779B9u;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return *s = x;
}

int mind_pick(const float *logits, int V, float temperature, int topk, const uint8_t *allow, uint32_t *rng) {
  if (topk < 1) topk = 1;
  if (topk > 40) topk = 40;
  int idx[40];
  float val[40];
  int n = 0;
  for (int v = 0; v < V; v++) {   /* keep the topk highest */
    if (allow && !allow[v]) continue;
    float l = logits[v];
    if (n < topk) {
      int i = n++;
      while (i > 0 && val[i - 1] < l) { val[i] = val[i - 1]; idx[i] = idx[i - 1]; i--; }
      val[i] = l; idx[i] = v;
    } else if (l > val[n - 1]) {
      int i = n - 1;
      while (i > 0 && val[i - 1] < l) { val[i] = val[i - 1]; idx[i] = idx[i - 1]; i--; }
      val[i] = l; idx[i] = v;
    }
  }
  if (n == 0) return 0;
  if (temperature < 0.05f) return idx[0];
  float sum = 0, p[40];
  for (int i = 0; i < n; i++) { p[i] = expf((val[i] - val[0]) / temperature); sum += p[i]; }
  float r = (next_rand(rng) >> 8) * (1.0f / 16777216.0f) * sum;
  for (int i = 0; i < n; i++) { r -= p[i]; if (r <= 0) return idx[i]; }
  return idx[n - 1];
}
