/* Native harness for firmware/lib/hum/chime.c: render every sound in blocks of 512 to
 * <outdir>/chime-<name>.wav, and print each one's length, its peak, and the speed.
 *
 *   chimewav <outdir>
 *   chimewav -z            print sizeof(Chime) and exit
 *
 * Build: cc -O2 -std=c99 -Wall -Wextra -Wdouble-promotion -I../../firmware/lib/hum chimewav.c ../../firmware/lib/hum/chime.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "chime.h"

static const char *NAMES[CHIME_COUNT] = {
  "boot", "link-up", "link-down", "note", "error", "ptt-on", "ptt-off", "tick", "tune", "win", "lose"
};

static void put32(FILE *f, unsigned v) {
  unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
  fwrite(b, 1, 4, f);
}
static void put16(FILE *f, unsigned v) {
  unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
  fwrite(b, 1, 2, f);
}

int main(int argc, char **argv) {
  static int16_t pcm[16000 * 8];
  int16_t block[512];
  Chime c;                                   /* the C name for the state */
  int which;
  if (argc == 2 && !strcmp(argv[1], "-z")) {
    printf("sizeof(Chime) = %u bytes\n", (unsigned)sizeof(Chime));
    return 0;
  }
  if (argc != 2) { fprintf(stderr, "usage: chimewav <outdir> | -z\n"); return 2; }
  for (which = 0; which < CHIME_COUNT; which++) {
    char path[1024];
    int total = 0, got, k, peak = 0, r;
    long samples = 0;
    clock_t c0, c1;
    FILE *f;
    chime_start(&c, which);
    do {
      got = chime_render(&c, block, 512);
      if (total + got > (int)(sizeof pcm / sizeof pcm[0])) break;
      memcpy(pcm + total, block, sizeof(int16_t) * (size_t)got);
      total += got;
    } while (got == 512);
    for (k = 0; k < total; k++) { int a = pcm[k] < 0 ? -pcm[k] : pcm[k]; if (a > peak) peak = a; }
    snprintf(path, sizeof path, "%s/chime-%s.wav", argv[1], NAMES[which]);
    f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite("RIFF", 1, 4, f); put32(f, 36u + 2u * (unsigned)total); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1); put32(f, CHIME_RATE); put32(f, CHIME_RATE * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, 2u * (unsigned)total);
    for (k = 0; k < total; k++) put16(f, (unsigned)(uint16_t)pcm[k]);
    fclose(f);
    c0 = clock();
    for (r = 0; r < 300; r++) {
      chime_start(&c, which);
      do { got = chime_render(&c, block, 512); samples += got; } while (got == 512);
    }
    c1 = clock();
    printf("%-10s %6d samples (%5.0f ms; chime_length %5.0f ms)  peak %6.1f dBFS  %6.0f x real time\n", NAMES[which], total,
           total / 16.0, chime_length(which) / 16.0, peak > 0 ? 20.0 * __builtin_log10(peak / 32768.0) : -120.0,
           (samples / 16000.0) / ((double)(c1 - c0) / CLOCKS_PER_SEC));
  }
  return 0;
}
