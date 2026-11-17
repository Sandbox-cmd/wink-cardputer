/* Runs Wink's mind on the Mac with the very code the chip uses (winknet.c), so that
 * what is read here is what the device will say. Driven line by line on its input:
 *
 *   f <id> <id> ...        feed pieces
 *   m <temp>               pick a mood sign and feed it; prints its id
 *   b <amount> <id> ...     favour these pieces (the words just said to it) by this much; b 0 clears
 *   g <temp> <topk> <max>  write pieces until <end> or max; prints their ids
 *   n <file.i16> <count>   mean loss over a stream of pieces (to compare with training)
 *   q                      print the scores for the next piece
 *   s                      print the state (H numbers)
 *   w <file> / l <file>    save / load the state
 *   r                      reset the state to zero
 *
 * build: cc -O2 -o wn wn.c ../firmware/lib/winknet/winknet.c -I../firmware/lib/winknet -lm
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winknet.h"

#define END 0
#define TIME 5
#define PCT 6
#define MOOD_LO 23
#define MOOD_HI 34
#define TEXT_LO 34

int main(int argc, char **argv) {
  if (argc < 2) return fprintf(stderr, "usage: wn model.bin [seed]\n"), 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return fprintf(stderr, "no model\n"), 1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *blob = malloc(n);
  if (fread(blob, 1, n, f) != (size_t)n) return 1;
  fclose(f);
  MindModel m;
  if (mind_load(&m, blob, n)) return fprintf(stderr, "bad model\n"), 1;
  float *h = calloc(m.H, sizeof(float)), *logits = malloc(m.V * sizeof(float));
  void *scratch = malloc(mind_scratch_bytes(&m));
  uint8_t *text = calloc(m.V, 1), *mood = calloc(m.V, 1);
  for (int v = TEXT_LO; v < m.V; v++) text[v] = 1;
  text[END] = text[TIME] = text[PCT] = 1;
  for (int v = MOOD_LO; v < MOOD_HI; v++) mood[v] = 1;
  uint32_t rng = argc > 2 ? (uint32_t)atoi(argv[2]) : 12345;
  int have = 0;   /* logits are current */
  float *boost = calloc(m.V, sizeof(float));

  char line[1 << 16];
  while (fgets(line, sizeof(line), stdin)) {
    char cmd = line[0];
    char *p = line + 1;
    if (cmd == 'f') {
      char *tok;
      while ((tok = strsep(&p, " \n")) != NULL)
        if (*tok) { mind_step(&m, h, atoi(tok), logits, scratch); have = 1; }
      printf("ok\n");
    } else if (cmd == 'b') {
      char *tok = strsep(&p, " \n");
      while (tok && !*tok) tok = strsep(&p, " \n");
      float amount = tok ? atof(tok) : 0;
      memset(boost, 0, m.V * sizeof(float));
      while ((tok = strsep(&p, " \n")) != NULL)
        if (*tok && atoi(tok) >= TEXT_LO && atoi(tok) < m.V) boost[atoi(tok)] = amount;
      printf("ok\n");
    } else if (cmd == 'm') {
      float temp = atof(p);
      int id = have ? mind_pick(logits, m.V, temp, 11, mood, &rng) : MOOD_LO;
      mind_step(&m, h, id, logits, scratch);
      have = 1;
      printf("%d\n", id);
    } else if (cmd == 'g') {
      float temp = 0.7f;
      int topk = 20, max = 40;
      sscanf(p, "%f %d %d", &temp, &topk, &max);
      for (int i = 0; i < max && have; i++) {
        for (int v = 0; v < m.V; v++) logits[v] += boost[v];
        int id = mind_pick(logits, m.V, temp, topk, text, &rng);
        boost[id] *= 0.3f;   /* once it has used a word, the pull to use it again mostly goes */
        mind_step(&m, h, id, logits, scratch);
        printf("%d ", id);
        if (id == END) break;
      }
      printf("\n");
    } else if (cmd == 'n') {
      char path[1024];
      long count = 0;
      sscanf(p, "%1023s %ld", path, &count);
      FILE *t = fopen(path, "rb");
      if (!t) { printf("nan\n"); continue; }
      int16_t *ids = malloc(count * 2);
      count = fread(ids, 2, count, t);
      fclose(t);
      double total = 0;
      for (long i = 0; i + 1 < count; i++) {
        mind_step(&m, h, ids[i], logits, scratch);
        float top = logits[0];
        for (int v = 1; v < m.V; v++) if (logits[v] > top) top = logits[v];
        double z = 0;
        for (int v = 0; v < m.V; v++) z += exp(logits[v] - top);
        total += -(logits[ids[i + 1]] - top - log(z));
      }
      printf("%.4f\n", total / (count - 1));
      free(ids);
      have = 1;
    } else if (cmd == 'q') {   /* the scores for the next piece, all of them */
      for (int v = 0; v < m.V; v++) printf("%.4f ", have ? logits[v] : 0.0f);
      printf("\n");
    } else if (cmd == 's') {
      for (int j = 0; j < m.H; j++) printf("%.3f ", h[j]);
      printf("\n");
    } else if (cmd == 'w' || cmd == 'l') {
      char path[1024];
      sscanf(p, "%1023s", path);
      FILE *t = fopen(path, cmd == 'w' ? "wb" : "rb");
      if (t) { if (cmd == 'w') fwrite(h, sizeof(float), m.H, t); else if (fread(h, sizeof(float), m.H, t)) have = 0; fclose(t); }
      printf("ok\n");
    } else if (cmd == 'r') {
      memset(h, 0, m.H * sizeof(float));
      have = 0;
      printf("ok\n");
    }
    fflush(stdout);
  }
  return 0;
}
