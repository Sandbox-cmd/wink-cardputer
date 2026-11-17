/* Native harness for firmware/lib/hum: render a reply in blocks of 512 and write a WAV.
 *
 *   humwav [options] "<text>" <mood> out.wav
 *     mood: calm happy delighted curious thinking sleepy surprised sad cheeky tender alert,
 *           or open / close for the two gestures (the text is then ignored)
 *   -e path   also write the planned events, one per line
 *   -s path   also write, per block: samples so far, hum_shown, hum_level
 *   -b reps   render the reply reps more times and print the speed in multiples of real time
 *   -H n      call hum_hush before the first block that starts at or after sample n
 *   -z        print sizeof(Hum) and exit
 *
 * Build: cc -O2 -std=c99 -Wall -Wextra -I../../firmware/lib/hum humwav.c ../../firmware/lib/hum/hum.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hum.h"

#define D(x) ((double)(x))   /* printf takes doubles; the library itself never does */

static const char *MOOD_NAMES[HUM_MOODS] = {
  "calm", "happy", "delighted", "curious", "thinking", "sleepy",
  "surprised", "sad", "cheeky", "tender", "alert"
};

static void put32(FILE *f, unsigned v) {
  unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
  fwrite(b, 1, 4, f);
}
static void put16(FILE *f, unsigned v) {
  unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
  fwrite(b, 1, 2, f);
}

static void start(Hum *h, const char *text, int mood) {
  if (mood == -1) hum_open(h);
  else if (mood == -2) hum_close(h);
  else hum_begin(h, text, mood);
}

int main(int argc, char **argv) {
  const char *events_path = 0, *shown_path = 0, *text, *mood_name, *out_path;
  int reps = 0, i, mood = -100, n, total = 0, cap = 16000 * 120, hush_at = -1;
  int16_t *pcm, block[512];
  static Hum h;
  FILE *f;

  for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
    if (!strcmp(argv[i], "-z")) { printf("sizeof(Hum) = %u bytes; sizeof(HumEvent) = %u\n", (unsigned)sizeof(Hum), (unsigned)sizeof(HumEvent)); return 0; }
    else if (!strcmp(argv[i], "-e") && i + 1 < argc) events_path = argv[++i];
    else if (!strcmp(argv[i], "-s") && i + 1 < argc) shown_path = argv[++i];
    else if (!strcmp(argv[i], "-b") && i + 1 < argc) reps = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-H") && i + 1 < argc) hush_at = atoi(argv[++i]);
    else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
  }
  if (argc - i != 3) { fprintf(stderr, "usage: humwav [-e events] [-s shown] [-b reps] \"<text>\" <mood> out.wav\n"); return 2; }
  text = argv[i]; mood_name = argv[i + 1]; out_path = argv[i + 2];
  if (!strcmp(mood_name, "open")) mood = -1;
  else if (!strcmp(mood_name, "close")) mood = -2;
  else for (n = 0; n < HUM_MOODS; n++) if (!strcmp(mood_name, MOOD_NAMES[n])) mood = n;
  if (mood == -100) { fprintf(stderr, "unknown mood %s\n", mood_name); return 2; }

  if (events_path && mood >= 0) {
    static HumEvent ev[1024];
    int ne = hum_debug_plan(&h, text, mood, ev, 1024), k;
    f = fopen(events_path, "w");
    if (!f) { perror(events_path); return 1; }
    for (k = 0; k < ne && k < 1024; k++)
      fprintf(f, "%.4f %.4f %.4f %.4f %.5f %.3f %.3f %.3f %.3f %d %.3f %d %d %.3f %.3f %.4f %.3f %.3f %.4f\n",
              D(ev[k].t_ms), D(ev[k].ms), D(ev[k].p0), D(ev[k].p1), D(ev[k].amp), D(ev[k].fc), D(ev[k].attack), D(ev[k].release),
              D(ev[k].floor), ev[k].hard, D(ev[k].scoop), ev[k].reveal_to, ev[k].word_start, D(ev[k].vib), D(ev[k].vib_hz),
              D(ev[k].breath), D(ev[k].open), D(ev[k].fc1), D(ev[k].console));
    fclose(f);
  }

  pcm = (int16_t *)malloc(sizeof(int16_t) * (size_t)cap);
  if (!pcm) return 1;
  start(&h, text, mood);
  f = shown_path ? fopen(shown_path, "w") : 0;
  for (;;) {
    int got;
    if (hush_at >= 0 && total >= hush_at) { hum_hush(&h); hush_at = -1; }
    got = hum_render(&h, block, 512);
    if (total + got > cap) break;
    memcpy(pcm + total, block, sizeof(int16_t) * (size_t)got);
    total += got;
    if (f) fprintf(f, "%d %d %.4f\n", total, hum_shown(&h), (double)hum_level(&h));
    if (got < 512) break;
  }
  if (f) fclose(f);

  f = fopen(out_path, "wb");
  if (!f) { perror(out_path); return 1; }
  fwrite("RIFF", 1, 4, f); put32(f, 36u + 2u * (unsigned)total); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1); put32(f, HUM_RATE); put32(f, HUM_RATE * 2); put16(f, 2); put16(f, 16);
  fwrite("data", 1, 4, f); put32(f, 2u * (unsigned)total);
  for (n = 0; n < total; n++) put16(f, (unsigned)(uint16_t)pcm[n]);
  fclose(f);

  if (reps > 0) {
    long samples = 0;
    clock_t c0 = clock(), c1;
    double secs;
    int r;
    for (r = 0; r < reps; r++) {
      int got;
      start(&h, text, mood);
      do { got = hum_render(&h, block, 512); samples += got; } while (got == 512);
    }
    c1 = clock();
    secs = (double)(c1 - c0) / CLOCKS_PER_SEC;
    printf("rendered %.1f s of audio in %.3f s of CPU: %.0f x real time\n",
           samples / 16000.0, secs, secs > 0 ? (samples / 16000.0) / secs : 0.0);
  }
  free(pcm);
  return 0;
}
