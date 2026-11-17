// A thin, dependency-free door onto the SAM speech synthesizer core in this folder.
// SAM renders 8-bit unsigned samples at 22050 Hz, one at a time, into a sink.
#pragma once
#include <stdint.h>

#define SAM_RATE 22050

struct SamParams {
  uint8_t speed = 72;   // lower is faster
  uint8_t pitch = 64;   // lower is higher
  uint8_t mouth = 128;
  uint8_t throat = 128;
  bool sing = false;
};

typedef void (*sam_sink_t)(void *ctx, unsigned char sample);

// Speaks `text` (plain English, at most 250 characters) into `sink`. Blocks until done.
// Returns false when the text could not be turned into phonemes or memory ran out.
bool sam_speak(const char *text, const SamParams &p, sam_sink_t sink, void *ctx);
