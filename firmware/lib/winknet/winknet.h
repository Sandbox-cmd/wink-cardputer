/* Wink's own mind: a small recurrent network (a GRU) that reads and writes one word
 * piece at a time. This file is plain C so the very same code runs on the chip and on
 * the Mac; what is judged on the Mac is what the chip will say.
 *
 * The weights are 8-bit and stay where they are (in the chip's storage, which it can
 * read like memory). The only thing that lives in working memory is the state: H
 * numbers between -1 and 1. That state is the mind's whole situation; it is carried
 * from piece to piece and never has to be reset.
 */
#ifndef WINKNET_H
#define WINKNET_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int V, E, H;            /* pieces known, size of a piece's vector, size of the state */
  const int8_t *emb;      /* V x E    a piece's vector, used for reading and for writing */
  const int8_t *wi;       /* 3H x E   input -> gates (reset, update, new) */
  const int8_t *wh;       /* 3H x H   state -> gates */
  const int8_t *proj;     /* E x H    state -> the vector of the piece it wants next */
  const float *bi, *bh;   /* 3H each */
  const float *pb;        /* E */
  const float *bo;        /* V */
  float emb_s, wi_s, wh_s, proj_s;   /* what one step of each 8-bit table is worth */
} MindModel;

/* Reads a model from a block of bytes without copying it. Returns 0 on success. */
int mind_load(MindModel *m, const uint8_t *blob, size_t n);

/* One piece in. `h` (H floats) is updated in place. If `logits` is not NULL it receives
 * V scores for the next piece. `scratch` must be mind_scratch_bytes() large. */
void mind_step(const MindModel *m, float *h, int token, float *logits, void *scratch);
size_t mind_scratch_bytes(const MindModel *m);

/* Picks the next piece from the scores: temperature, and only among the `topk` likeliest.
 * `allow` (may be NULL) marks the pieces that may be chosen (V bytes, nonzero = allowed). */
int mind_pick(const float *logits, int V, float temperature, int topk, const uint8_t *allow, uint32_t *rng);

#ifdef __cplusplus
}
#endif
#endif
