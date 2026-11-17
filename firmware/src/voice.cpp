// Wink's mouth and ears. One task owns the audio hardware.
//
// It speaks in three ways, best first:
//   1. sound streamed from the Mac while the whole mind is in reach (a natural voice),
//   2. recordings of the small mind's lines, in the same voice, from a pack on the
//      memory card (made by tools/make-voice.mjs),
//   3. the SAM synthesizer of 1982 on the chip, when neither of those has the words.
#include "voice.h"
#include "os.h"
#include "samvoice.h"
#include "hum.h"
#include "chime.h"
#include <FS.h>
#include <freertos/stream_buffer.h>
#include <vector>

fs::FS *pathFs(String &path);   // shell.cpp

namespace {

enum CmdType : uint8_t { C_SAY, C_CHIME, C_TONE, C_STREAM, C_HUM, C_GESTURE };
struct Cmd {
  uint8_t type;
  String *text;
  uint16_t a, b;
  uint32_t x, y;
  uint32_t gen;
};

QueueHandle_t q;
StreamBufferHandle_t pttBuf, rxAudio;
enum Mode : uint8_t { M_NONE, M_SPK, M_MIC };
Mode mode = M_NONE;

volatile uint32_t gen = 0;      // hush() bumps this; speech queued under an older number is dropped
uint32_t runGen = 0;
#define abortFlag (runGen != gen)
volatile bool isSpeaking = false;
volatile bool pttOn = false;
volatile int pending = 0;
volatile uint32_t utterCount = 0;

// where the voice is in the text of the current utterance
volatile int pieceStart = 0, pieceEnd = 0;
volatile uint32_t pieceT0 = 0;
volatile float charsPerSec = 14.0f;

// loudness of what is sounding, stamped with the time it will be heard
struct Env {
  uint32_t ms;
  uint8_t v;
};
Env ring[256];
volatile uint16_t ringW = 0;
uint16_t ringR = 0;
float mouthNow = 0;
int envPeak = 0, envCount = 0;
uint32_t samplesOut = 0, playT0 = 0, playRate = SAM_RATE;

inline void envAdd(int level255) {   // one sample's loudness, 0..255
  if (level255 > envPeak) envPeak = level255;
  samplesOut++;
  if (++envCount == 128) {
    uint16_t w = ringW;
    ring[w].ms = playT0 + (uint32_t)((uint64_t)samplesOut * 1000 / playRate);
    ring[w].v = (uint8_t)min(255, envPeak);
    ringW = (w + 1) & 255;
    envPeak = 0;
    envCount = 0;
  }
}

// ---- the chip's own voice: 8-bit samples at 22050 Hz -------------------------
uint8_t chunk[3][512];
int chunkI = 0, chunkP = 0;

// ---- recorded and streamed voice: mu-law at 16000 Hz --------------------------
int16_t ulaw[256];
int16_t pcm[3][512];
int pcmI = 0, pcmP = 0;
const uint32_t PCM_RATE = 16000;

struct Clip {
  uint32_t hash, off, len;
};
std::vector<Clip> clips;
File pack;
volatile bool packAsk = false;
const char *lastFrom = "nothing yet";
volatile uint32_t streamId = 0;
volatile bool streamDiscard = true, streamPlaying = false, streamKill = false;

// Wink's own voice: the hum (lib/hum)
Hum humState;
volatile bool humming = false;
int16_t humBlock[512];

// ---- the room ----------------------------------------------------------------
volatile float roomLevel = 0;
volatile bool loudFlag = false, voiceFlag = false;
float roomFloor = 60;
uint32_t deafUntil = 0, loudQuietUntil = 0, voiceQuietUntil = 0;
uint8_t activeHist[64];
uint8_t activeI = 0;
int16_t rec[4][256];

// hearing a voice by itself
int16_t pre[12][256];          // the last fifth of a second, so the first word is not lost
uint8_t preI = 0;
volatile bool voxOk = false, voxOn = false;
uint32_t voxHold = 0;
int voxHot = 0, voxQuiet = 0, voxBlocks = 0;
volatile uint32_t dbgStarts = 0, dbgLastBlocks = 0, dbgDropped = 0, dbgBlocks = 0;
volatile float dbgRms = 0;
volatile bool voxForce = false;
uint8_t recI = 0;
uint32_t recCount = 0;

void applyVolume() { M5Cardputer.Speaker.setVolume((uint8_t)min(255, cfg.volume * 25)); }

void toSpeaker() {
  if (mode == M_SPK) return;
  if (mode == M_MIC) {
    while (M5Cardputer.Mic.isRecording()) vTaskDelay(1);
    M5Cardputer.Mic.end();
  }
  M5Cardputer.Speaker.begin();
  applyVolume();
  mode = M_SPK;
}

void toMic() {
  if (mode == M_MIC) return;
  if (mode == M_SPK) {
    while (M5Cardputer.Speaker.isPlaying()) vTaskDelay(1);
    M5Cardputer.Speaker.end();
  }
  M5Cardputer.Mic.begin();
  mode = M_MIC;
  recCount = 0;
  deafUntil = millis() + 350;   // let the line settle, and do not startle at its own voice
  roomLevel = 0;
}

void startClock(uint32_t rate) {
  playRate = rate;
  samplesOut = 0;
  envPeak = envCount = 0;
  playT0 = millis() + 40;
}

void flushChunk() {
  if (!chunkP) return;
  while (M5Cardputer.Speaker.isPlaying(0) == 2 && !abortFlag) vTaskDelay(1);
  if (!abortFlag) M5Cardputer.Speaker.playRaw(chunk[chunkI], chunkP, SAM_RATE, false, 1, 0, false);
  chunkI = (chunkI + 1) % 3;
  chunkP = 0;
}

void sink(void *, unsigned char b) {
  if (abortFlag) return;
  chunk[chunkI][chunkP++] = b;
  envAdd(abs((int)b - 128) * 3);
  if (chunkP == 512) flushChunk();
}

void flushPcm() {
  if (!pcmP) return;
  while (M5Cardputer.Speaker.isPlaying(0) == 2 && !abortFlag) vTaskDelay(1);
  if (!abortFlag) M5Cardputer.Speaker.playRaw(pcm[pcmI], pcmP, PCM_RATE, false, 1, 0, false);
  pcmI = (pcmI + 1) % 3;
  pcmP = 0;
}

void pushUlaw(const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n && !abortFlag; i++) {
    int16_t s = ulaw[d[i]];
    pcm[pcmI][pcmP++] = s;
    envAdd(abs(s) >> 6);
    if (pcmP == 512) flushPcm();
  }
}

void pushSilence(int samples) {
  for (int i = 0; i < samples && !abortFlag; i++) {
    pcm[pcmI][pcmP++] = 0;
    envAdd(0);
    if (pcmP == 512) flushPcm();
  }
}

void waitQuiet() {
  while (M5Cardputer.Speaker.isPlaying(0) && !abortFlag) vTaskDelay(2);
  if (abortFlag) M5Cardputer.Speaker.stop();
}

// ---- the pack of recordings --------------------------------------------------
void loadPack() {
  clips.clear();
  if (pack) pack.close();
  String p = "/sd/wink/voice.pak";
  fs::FS *fs = pathFs(p);
  if (!fs) return;
  pack = fs->open(p);
  if (!pack) return;
  uint8_t head[8];
  if (pack.read(head, 8) != 8 || memcmp(head, "WVP1", 4)) {
    pack.close();
    return;
  }
  uint32_t count;
  memcpy(&count, head + 4, 4);
  if (count > 4000) {
    pack.close();
    return;
  }
  clips.resize(count);
  if (pack.read((uint8_t *)clips.data(), count * 12) != (int)(count * 12)) {
    clips.clear();
    pack.close();
  }
}

const Clip *findClip(uint32_t h) {
  int lo = 0, hi = (int)clips.size() - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (clips[mid].hash == h) return &clips[mid];
    if (clips[mid].hash < h) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

// Cover the whole text with recordings: the longest run of words that has one, then the
// next, and so on. Fails (returns false) if any word is left without a recording.
bool planClips(const String &text, std::vector<const Clip *> &out) {
  if (clips.empty()) return false;
  // the words, the way tools/make-voice.mjs normalizes them
  std::vector<String> words;
  String w;
  for (size_t i = 0; i <= text.length(); i++) {
    char c = i < text.length() ? tolower((unsigned char)text[i]) : ' ';
    if (isalnum((unsigned char)c) || c == '\'') w += c;
    else if (w.length()) {
      words.push_back(w);
      w = "";
    }
  }
  int n = words.size();
  if (n == 0 || n > 60) return false;
  // from the end backwards: next[i] is where the longest usable recording starting at word i ends
  std::vector<int> next(n + 1, -1);
  std::vector<const Clip *> pick(n + 1, nullptr);
  next[n] = n;
  for (int i = n - 1; i >= 0; i--) {
    uint32_t h = 0x811c9dc5;
    for (int j = i; j < n; j++) {
      if (j > i) h = (h ^ (uint8_t)' ') * 0x01000193;
      for (size_t k = 0; k < words[j].length(); k++) h = (h ^ (uint8_t)words[j][k]) * 0x01000193;
      if (next[j + 1] < 0) continue;
      const Clip *c = findClip(h);
      if (c) {
        next[i] = j + 1;   // keeps the longest, since j only grows
        pick[i] = c;
      }
    }
  }
  if (next[0] < 0) return false;
  for (int i = 0; i < n; i = next[i]) out.push_back(pick[i]);
  return true;
}

void beginUtterance(int textLen, uint32_t totalSamples) {
  toSpeaker();
  isSpeaking = true;
  utterCount++;
  startClock(PCM_RATE);
  pcmP = 0;
  pieceStart = 0;
  pieceEnd = textLen;
  pieceT0 = playT0;
  charsPerSec = totalSamples ? textLen * (float)PCM_RATE / totalSamples : 14.0f;
}

// a recording of this exact sentence, sent over by the whole mind and kept on the card
bool sayFromTaught(const String &text) {
  uint32_t h = 0x811c9dc5;
  bool gap = false, any = false;
  for (size_t i = 0; i < text.length(); i++) {
    char c = tolower((unsigned char)text[i]);
    if (isalnum((unsigned char)c) || c == '\'') {
      if (gap && any) h = (h ^ (uint8_t)' ') * 0x01000193;
      h = (h ^ (uint8_t)c) * 0x01000193;
      gap = false;
      any = true;
    } else gap = true;
  }
  if (!any) return false;
  char name[40];
  snprintf(name, sizeof(name), "/sd/wink/x/%08x.ul", (unsigned)h);
  String p = name;
  fs::FS *fs = pathFs(p);
  File f = fs ? fs->open(p) : File();
  if (!f || f.size() < 800) return false;
  beginUtterance(text.length(), f.size());
  static uint8_t tbuf[512];
  int n;
  while (!abortFlag && (n = f.read(tbuf, sizeof(tbuf))) > 0) pushUlaw(tbuf, n);
  f.close();
  flushPcm();
  waitQuiet();
  pieceStart = pieceEnd;
  isSpeaking = false;
  return true;
}

bool sayFromPack(const String &text) {
  std::vector<const Clip *> plan;
  if (!planClips(text, plan)) return sayFromTaught(text);
  uint32_t total = 0;
  for (auto c : plan) total += c->len + 900;
  beginUtterance(text.length(), total);
  static uint8_t buf[512];
  for (size_t s = 0; s < plan.size() && !abortFlag; s++) {
    pack.seek(plan[s]->off);
    uint32_t left = plan[s]->len;
    while (left && !abortFlag) {
      int n = pack.read(buf, min((uint32_t)sizeof(buf), left));
      if (n <= 0) break;
      pushUlaw(buf, n);
      left -= n;
    }
    pushSilence(900);   // a breath between pieces
  }
  flushPcm();
  waitQuiet();
  pieceStart = pieceEnd;
  isSpeaking = false;
  return true;
}

void pushPcm16(const int16_t *d, int n) {
  for (int i = 0; i < n && !abortFlag; i++) {
    pcm[pcmI][pcmP++] = d[i];
    envAdd(min(255, abs(d[i]) >> 5));
    if (pcmP == 512) flushPcm();
  }
}

// hum a line it wrote itself: the text appears word by word on the hum's own clock
void sayHum(const String &text, uint8_t mood) {
  hum_begin(&humState, text.c_str(), mood % HUM_MOODS);
  beginUtterance(text.length(), 0);
  humming = true;
  int n;
  while (!abortFlag && (n = hum_render(&humState, humBlock, 512)) > 0) {
    pushPcm16(humBlock, n);
    if (n < 512) break;
  }
  if (abortFlag) hum_hush(&humState);
  flushPcm();
  waitQuiet();
  humming = false;
  pieceStart = pieceEnd;
  isSpeaking = false;
}

// the mouth opening as the whole mind arrives (1), or closing as it leaves (2)
void sayGesture(uint8_t which) {
  if (which == 1) hum_open(&humState);
  else hum_close(&humState);
  toSpeaker();
  startClock(PCM_RATE);
  pcmP = 0;
  int n;
  while (!abortFlag && (n = hum_render(&humState, humBlock, 512)) > 0) {
    pushPcm16(humBlock, n);
    if (n < 512) break;
  }
  flushPcm();
  waitQuiet();
}

void sayFromStream(const String &text, uint32_t id, uint32_t total) {
  if (id != streamId || streamDiscard) return;
  streamPlaying = true;
  beginUtterance(text.length(), total);
  static uint8_t buf[512];
  uint32_t got = 0, lastData = millis();
  while (got < total && !abortFlag && !streamKill) {
    size_t n = xStreamBufferReceive(rxAudio, buf, min((uint32_t)sizeof(buf), total - got), pdMS_TO_TICKS(20));
    if (n) {
      pushUlaw(buf, n);
      got += n;
      lastData = millis();
    } else if (millis() - lastData > 3000) break;
  }
  if (got < total && id == streamId) streamDiscard = true;   // whatever is still coming is not wanted
  flushPcm();
  waitQuiet();
  pieceStart = pieceEnd;
  isSpeaking = false;
  streamKill = false;
  streamPlaying = false;
}

// ---- the chip's voice, a sentence at a time ------------------------------------
void speakPiece(const String &original, int start, int end) {
  String s = voice::speakable(original.substring(start, end));
  bool hasLetter = false;
  for (size_t i = 0; i < s.length(); i++)
    if (isalpha((unsigned char)s[i])) hasLetter = true;
  if (!hasLetter) return;
  if (s.length() > 240) s = s.substring(0, 240);

  SamParams p;
  p.speed = cfg.voiceSpeed;
  p.pitch = cfg.voicePitch;
  p.mouth = 138;
  p.throat = 118;
  startClock(SAM_RATE);
  chunkP = 0;
  pieceStart = start;
  pieceEnd = end;
  pieceT0 = playT0;
  uint32_t began = millis();
  sam_speak(s.c_str(), p, sink, nullptr);
  flushChunk();
  while (M5Cardputer.Speaker.isPlaying(0) && !abortFlag) vTaskDelay(2);
  uint32_t took = millis() - began;
  if (!abortFlag && took > 300 && end > start) charsPerSec = (end - start) * 1000.0f / took;
  pieceStart = end;
}

void sayWithChip(const String &text) {
  toSpeaker();
  isSpeaking = true;
  pieceStart = pieceEnd = 0;
  charsPerSec = 14.0f;
  utterCount++;
  int n = text.length(), start = 0;
  while (start < n && !abortFlag) {
    // one piece: up to a sentence end, or 110 characters broken at a comma or space
    int end = start, lastBreak = -1;
    while (end < n) {
      char c = text[end];
      if (c == '\n') { end++; break; }
      if ((c == '.' || c == '?' || c == '!' || c == ';' || c == ':') && (end + 1 >= n || text[end + 1] == ' ')) { end++; break; }
      if (c == ',' || c == ' ') lastBreak = end;
      if (end - start >= 110) {
        if (lastBreak > start) end = lastBreak + 1;
        break;
      }
      end++;
    }
    speakPiece(text, start, end);
    start = end;
    if (!abortFlag) vTaskDelay(pdMS_TO_TICKS(90));
  }
  if (abortFlag) M5Cardputer.Speaker.stop();
  isSpeaking = false;
}

struct Note {
  uint16_t hz, ms;
};
void play(const Note *n, int count) {
  toSpeaker();
  for (int i = 0; i < count; i++) {
    if (n[i].hz) M5Cardputer.Speaker.tone(n[i].hz, n[i].ms);
    vTaskDelay(pdMS_TO_TICKS(n[i].ms + 12));
  }
}

// the little sounds: one family, made by the same synth as the hum (lib/hum/chime.c)
ChimeSynth chimeState;
void doChime(uint8_t c) {
  chime_start(&chimeState, (int)c);
  toSpeaker();
  startClock(PCM_RATE);
  pcmP = 0;
  int n;
  while ((n = chime_render(&chimeState, humBlock, 512)) > 0) {
    for (int i = 0; i < n; i++) {
      pcm[pcmI][pcmP++] = humBlock[i];
      if (pcmP == 512) {
        while (M5Cardputer.Speaker.isPlaying(0) == 2) vTaskDelay(1);
        M5Cardputer.Speaker.playRaw(pcm[pcmI], 512, PCM_RATE, false, 1, 0, false);
        pcmI = (pcmI + 1) % 3;
        pcmP = 0;
      }
    }
    if (n < 512) break;
  }
  if (pcmP) {
    while (M5Cardputer.Speaker.isPlaying(0) == 2) vTaskDelay(1);
    M5Cardputer.Speaker.playRaw(pcm[pcmI], pcmP, PCM_RATE, false, 1, 0, false);
    pcmI = (pcmI + 1) % 3;
    pcmP = 0;
  }
  while (M5Cardputer.Speaker.isPlaying(0)) vTaskDelay(2);
}

void listenBlock() {
  int16_t *buf = rec[recI];
  if (!M5Cardputer.Mic.record(buf, 256, 16000)) {
    vTaskDelay(5);
    return;
  }
  recCount++;
  recI = (recI + 1) & 3;
  if (recCount < 3) return;
  const int16_t *done = rec[(recI + 1) & 3];   // the buffer queued three calls ago is complete
  if ((pttOn || voxOn) && xStreamBufferSend(pttBuf, done, 512, 0) != 512) dbgDropped++;
  dbgBlocks++;

  int32_t mean = 0;
  for (int i = 0; i < 256; i++) mean += done[i];
  mean /= 256;
  float sum = 0;
  for (int i = 0; i < 256; i++) {
    float v = done[i] - mean;
    sum += v * v;
  }
  float rms = sqrtf(sum / 256);
  dbgRms = rms;
  uint32_t now = millis();
  if (now < deafUntil) return;
  if (rms < roomFloor) roomFloor += (rms - roomFloor) * 0.05f;
  else if (rms < roomFloor * 2.2f + 60) roomFloor += (rms - roomFloor) * 0.002f;   // creeps up with a noisier room, not with speech
  else roomFloor += 0.004f;
  if (roomFloor < 20) roomFloor = 20;
  float over = rms - roomFloor * 1.5f;
  float lvl = over > 0 ? sqrtf(over / 1200.0f) : 0;
  if (lvl > 1) lvl = 1;
  roomLevel = roomLevel * 0.6f + lvl * 0.4f;

  if (rms > roomFloor * 9 + 900 && now > loudQuietUntil) {
    loudFlag = true;
    loudQuietUntil = now + 4000;
  }
  bool hot = rms > roomFloor * 2.2f + 60;
  if (!pttOn) {
    if (!voxOn) {
      memcpy(pre[preI], done, 512);
      preI = (preI + 1) % 12;
      voxHot = hot ? voxHot + 1 : max(0, voxHot - 1);
      if ((voxOk || voxForce) && voxHot >= 3 && now > voxHold) {
        dbgStarts++;   // someone is speaking: start, beginning with what was just heard
        xStreamBufferReset(pttBuf);
        for (int i = 0; i < 12; i++) xStreamBufferSend(pttBuf, pre[(preI + i) % 12], 512, 0);
        voxOn = true;
        voxQuiet = voxBlocks = 0;
      }
    } else {
      voxBlocks++;
      voxQuiet = hot ? 0 : voxQuiet + 1;
      if (voxQuiet > 50 || voxBlocks > 620 || !(voxOk || voxForce)) {
        dbgLastBlocks = voxBlocks;   // 0.8 s of quiet, or ten seconds in all
        voxOn = false;
        voxHot = 0;
      }
    }
  } else if (voxOn) voxOn = false;
  activeHist[activeI] = hot;
  activeI = (activeI + 1) & 63;
  int active = 0;
  for (int i = 0; i < 64; i++) active += activeHist[i];
  if (active > 34 && now > voiceQuietUntil) {
    voiceFlag = true;
    voiceQuietUntil = now + 45000;
    memset(activeHist, 0, sizeof(activeHist));
  }
}

void task(void *) {
  uint32_t lastSound = millis();
  loadPack();
  for (;;) {
    if (packAsk) {
      packAsk = false;
      loadPack();
    }
    Cmd c;
    TickType_t wait = (mode == M_MIC) ? 0 : pdMS_TO_TICKS(40);
    if (pttOn && mode != M_MIC) {
      toMic();
      deafUntil = 0;
    }
    if (!pttOn && xQueueReceive(q, &c, wait) == pdTRUE) {
      bool speech = c.type == C_SAY || c.type == C_STREAM || c.type == C_HUM;
      runGen = speech ? c.gen : gen;   // hushing stops speech; a chime is short enough to finish
      if (!abortFlag) {
        if (c.type == C_HUM && c.text) {
          lastFrom = "its own hum";
          sayHum(*c.text, (uint8_t)c.a);
        } else if (c.type == C_GESTURE) {
          sayGesture((uint8_t)c.a);
        } else if (c.type == C_STREAM && c.text) {
          lastFrom = "sound from the Mac";
          sayFromStream(*c.text, c.x, c.y);
        } else if (c.type == C_SAY && c.text) {
          if (sayFromPack(*c.text)) lastFrom = "recordings on the card";
          else {
            lastFrom = "the chip's own voice";
            sayWithChip(*c.text);
          }
        } else if (c.type == C_CHIME) doChime(c.a);
        else if (c.type == C_TONE) {
          Note n = {c.a, c.b};
          play(&n, 1);
        }
      } else if (c.type == C_STREAM && c.x == streamId) {
        streamDiscard = true;
      }
      if (c.text) delete c.text;
      pending--;
      if (pending < 0) pending = 0;
      lastSound = millis();
      continue;
    }
    if (mode == M_MIC) {
      listenBlock();
    } else if (cfg.listen && millis() - lastSound > 300) {
      toMic();
    }
  }
}

const char *ones[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                      "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
const char *tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

String numberWords(long n) {
  if (n < 0) return "minus " + numberWords(-n);
  if (n < 20) return ones[n];
  if (n < 100) return String(tens[n / 10]) + (n % 10 ? String(" ") + ones[n % 10] : String());
  if (n < 1000) return String(ones[n / 100]) + " hundred" + (n % 100 ? " and " + numberWords(n % 100) : String());
  if (n < 1000000) return numberWords(n / 1000) + " thousand" + (n % 1000 ? " " + numberWords(n % 1000) : String());
  return numberWords(n / 1000000) + " million" + (n % 1000000 ? " " + numberWords(n % 1000000) : String());
}

struct Swap {
  const char *from, *to;
};
// for the chip's voice only: words its 1982 reading rules get wrong, respelled
const Swap swaps[] = {
    {"claude", "clawd"}, {"ok", "oh kay"}, {"okay", "oh kay"}, {"ai", "ay eye"}, {"usb", "you ess bee"},
    {"os", "oh ess"}, {"sd", "ess dee"}, {"hmm", "hm"}, {"vs", "versus"}, {"etc", "et cetera"},
    {"them", "thehm"}, {"ninety", "nine tee"}, {"nineteen", "nine teen"}, {"minus", "mynus"},
    {"heard", "herd"}, {"heart", "hart"}, {"heavy", "hevvy"}, {"live", "liv"}, {"alive", "a lyve"},
    {"maybe", "may be"}, {"inside", "in side"}, {"goodbye", "good by"}, {"hey", "hay"},
    {"listening", "lissen ing"}, {"notice", "noatiss"}, {"finally", "fynally"}, {"meant", "ment"},
    {"lose", "looz"}, {"happening", "happen ing"}, {"purpose", "purpus"}, {"silence", "sy lence"},
    {"secret", "see cret"}, {"whoa", "woe"}, {"window", "win doe"}, {"camera", "cam era"},
};
}  // namespace

String voice::speakable(const String &text) {
  String out;
  String word;
  auto flushWord = [&]() {
    if (!word.length()) return;
    bool allDigits = true;
    for (size_t i = 0; i < word.length(); i++)
      if (!isdigit((unsigned char)word[i])) allDigits = false;
    if (allDigits && word.length() <= 9) {
      String spoken = numberWords(atol(word.c_str()));
      spoken.replace("ninety", "nine tee");
      spoken.replace("nineteen", "nine teen");
      spoken.replace("minus", "mynus");
      out += spoken;
    } else {
      const char *rep = nullptr;
      for (auto &s : swaps)
        if (word == s.from) rep = s.to;
      if (rep) out += rep;
      else {
        for (size_t i = 0; i < word.length(); i++)
          if (isalpha((unsigned char)word[i]) || word[i] == '\'') out += word[i];
          else if (isdigit((unsigned char)word[i])) { out += ' '; out += ones[word[i] - '0']; out += ' '; }
      }
    }
    word = "";
  };
  for (size_t i = 0; i < text.length(); i++) {
    unsigned char c = text[i];
    if (c >= 0x80) continue;   // the chip only knows plain letters
    char l = tolower(c);
    if (c == ',' && i > 0 && i + 1 < text.length() && isdigit((unsigned char)text[i - 1]) && isdigit((unsigned char)text[i + 1])) continue;   // 8,849
    if (isalnum(c) || l == '\'' || (l == '-' && word.length() && i + 1 < text.length() && isalpha((unsigned char)text[i + 1]))) {
      word += l;
    } else {
      flushWord();
      if (l == '.' || l == '!' || l == ';' || l == ':') out += '.';
      else if (l == '?') out += '?';
      else if (l == ',' ) out += ',';
      else if (l == '%') out += " per cent";
      else if (l == '+') out += " plus ";
      else if (l == '=') out += " equals ";
      else if (l == '&') out += " and ";
      else if (out.length() && out[out.length() - 1] != ' ') out += ' ';
    }
  }
  flushWord();
  out.trim();
  return out;
}

String voice::words(long n) { return numberWords(n); }

void voice::begin() {
  // mu-law to 16-bit, the standard table
  for (int i = 0; i < 256; i++) {
    int u = ~i & 0xFF;
    int t = ((u & 0x0F) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    ulaw[i] = (int16_t)((u & 0x80) ? (0x84 - t) : (t - 0x84));
  }
  q = xQueueCreate(12, sizeof(Cmd));
  pttBuf = xStreamBufferCreate(12000, 1);    // a third of a second of your voice
  rxAudio = xStreamBufferCreate(12000, 1);   // three quarters of a second of its own
  M5Cardputer.Mic.end();
  M5Cardputer.Speaker.end();
  mode = M_NONE;
  xTaskCreatePinnedToCore(task, "voice", 8192, nullptr, 3, nullptr, 0);
}

static void push(Cmd c) {
  c.gen = gen;
  pending++;
  if (xQueueSend(q, &c, 0) != pdTRUE) {
    pending--;
    if (c.type == C_STREAM && c.x == streamId) streamDiscard = true;
    if (c.text) delete c.text;
  }
}

void voice::say(const String &text) {
  if (!text.length()) return;
  push({C_SAY, new String(text), 0, 0, 0, 0, 0});
}
void voice::sayStream(const String &text, uint32_t id, uint32_t totalBytes) {
  push({C_STREAM, new String(text), 0, 0, id, totalBytes, 0});
}
void voice::hum(const String &text, Mood mood) {
  if (!text.length()) return;
  push({C_HUM, new String(text), (uint16_t)mood, 0, 0, 0, 0});
}
void voice::gesture(bool opening) { push({C_GESTURE, nullptr, (uint16_t)(opening ? 1 : 2), 0, 0, 0, 0}); }

void voice::chime(Chime c) { push({C_CHIME, nullptr, (uint16_t)c, 0, 0, 0, 0}); }
void voice::tone(uint16_t hz, uint16_t ms) { push({C_TONE, nullptr, hz, ms, 0, 0, 0}); }
void voice::hush() { gen++; }

bool voice::busy() { return pending > 0; }
bool voice::speaking() { return isSpeaking; }
uint32_t voice::utterance() { return utterCount; }

float voice::mouth() {
  uint32_t now = millis();
  bool fresh = false;
  while (ringR != ringW && (int32_t)(now - ring[ringR].ms) >= 0) {
    mouthNow = ring[ringR].v / 255.0f;
    ringR = (ringR + 1) & 255;
    fresh = true;
  }
  if (!fresh && (!isSpeaking || ringR == ringW)) mouthNow *= 0.7f;
  return mouthNow;
}

int voice::spokenChars() {
  if (!isSpeaking) return 1 << 20;
  if (humming) return hum_shown(&humState);   // the hum shows each word as its first pulse sounds
  int32_t dt = (int32_t)(millis() - pieceT0);
  if (dt < 0) dt = 0;
  int c = pieceStart + (int)(dt * charsPerSec / 1000.0f) + 2;
  return c > pieceEnd ? (int)pieceEnd : c;
}

float voice::micLevel() { return mode == M_MIC ? (float)roomLevel : 0.0f; }
bool voice::micLoud() {
  if (!loudFlag) return false;
  loudFlag = false;
  return true;
}
bool voice::micVoice() {
  if (!voiceFlag) return false;
  voiceFlag = false;
  return true;
}

void voice::pttStart() {
  hush();
  voxOn = false;
  xStreamBufferReset(pttBuf);
  pttOn = true;
}
void voice::pttStop() { pttOn = false; }
void voice::voxAllow(bool allow) { voxOk = allow; }
bool voice::voxActive() { return voxOn; }
void voice::voxHoldOff(uint32_t ms) { voxHold = millis() + ms; }
String voice::debug(int force) {
  if (force >= 0) voxForce = force;
  char b[200];
  snprintf(b, sizeof(b), "mode %d blocks %u rms %.0f floor %.0f | vox ok %d on %d starts %u last %u blocks | ptt %d dropped %u", (int)mode, (unsigned)dbgBlocks, (float)dbgRms, roomFloor, (int)voxOk, (int)voxOn, (unsigned)dbgStarts, (unsigned)dbgLastBlocks, (int)pttOn, (unsigned)dbgDropped);
  return b;
}
bool voice::pttActive() { return pttOn; }
size_t voice::pttRead(uint8_t *dst, size_t maxBytes) { return xStreamBufferReceive(pttBuf, dst, maxBytes, 0); }

// ---- sound arriving from the Mac (called from the link task) ------------------
uint32_t voice::streamOpen() {
  if (streamPlaying) {   // an earlier reply is still sounding: it gives way
    streamKill = true;
    for (int i = 0; i < 100 && streamPlaying; i++) vTaskDelay(5);
  }
  static uint8_t sinkhole[256];
  while (xStreamBufferReceive(rxAudio, sinkhole, sizeof(sinkhole), 0) > 0) {
  }
  streamDiscard = false;
  return ++streamId;
}
size_t voice::streamSpace() { return streamDiscard ? 4096 : xStreamBufferSpacesAvailable(rxAudio); }
void voice::streamFeed(const uint8_t *d, size_t n) {
  if (!streamDiscard) xStreamBufferSend(rxAudio, d, n, 0);
}
void voice::streamDrop(uint32_t id) {
  if (id == streamId) streamDiscard = true;
}

void voice::packReload() { packAsk = true; }
int voice::packCount() { return clips.size(); }
const char *voice::lastSource() { return lastFrom; }
