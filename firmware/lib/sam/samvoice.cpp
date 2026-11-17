#include "samvoice.h"
#include <ctype.h>
#include <string.h>
#include <new>
#include "reciter.h"
#include "sam.h"
#include "SamData.h"

SamData *samdata = nullptr;

bool sam_speak(const char *text, const SamParams &p, sam_sink_t sink, void *ctx) {
  if (!text || !sink) return false;
  size_t n = strlen(text);
  if (n == 0 || n > 250) return false;

  samdata = new (std::nothrow) SamData;
  if (!samdata) return false;

  EnableSingmode(p.sing ? 1 : 0);
  SetSpeed(p.speed);
  SetPitch(p.pitch);
  SetMouth(p.mouth);
  SetThroat(p.throat);

  char input[256];
  for (size_t i = 0; i < n; i++) input[i] = (char)toupper((unsigned char)text[i]);
  input[n] = 0;
  strncat(input, "[", 255 - n);

  bool ok = TextToPhonemes(input) != 0;
  if (ok) {
    SetInput(input);
    SAMMain(sink, ctx);
  }
  delete samdata;
  samdata = nullptr;
  return ok;
}
