#include "think.h"
#include "kernel.h"
#include <LittleFS.h>
extern "C" {
#include "winknet.h"
extern const uint8_t wink_model_start[];
extern const uint8_t wink_model_end[];
extern const uint8_t wink_pieces_start[];
extern const uint8_t wink_pieces_end[];
}

namespace {

// signs, in the order train/pieces.py gives them
enum { S_END = 0, S_YOU = 1, S_WINK = 2, S_THOUGHT = 3, S_STORY = 4, S_TIME = 5, S_PCT = 6, S_EVENT0 = 7, S_MOOD0 = 23, S_TEXT0 = 34 };
const char *EVENT_NAMES[] = {"wake", "pickup", "putdown", "shake", "facedown", "loud", "voices", "quiet", "morning", "night", "low",
                             "plugged", "unplugged", "mac_arrives", "mac_leaves", "long_gap"};

MindModel model;
bool loaded = false;
float *h = nullptr, *logits = nullptr;
void *scratch = nullptr;
uint8_t *allowText = nullptr, *allowMood = nullptr;
uint8_t *favour = nullptr;      // how strongly each piece is favoured just now: the words last said to it
const float FAVOUR = 1.3f / 255.0f;
const float TEMPERATURE = 0.55f;
const int TOPK = 12;
uint32_t rng = 0;
bool haveLogits = false;

// the pieces: count, longest, offsets into the text, ids sorted by text, the text
int pieceCount = 0, pieceLongest = 0;
const uint16_t *pieceOff = nullptr, *pieceSorted = nullptr;
const char *pieceText = nullptr;

struct Cmd {
  uint8_t kind;   // 0 feed ids, 1 reply, 2 muse
  uint16_t *ids;
  uint16_t n;
};
QueueHandle_t cmdQ, outQ;
SemaphoreHandle_t lock;
volatile bool working = false;
String partialText;
volatile uint32_t lifetime = 0;
volatile float msPerPiece = 0;
uint32_t stamp = 0;         // which mind this state belongs to
bool dirty = false;
// the network forgets an event within a few pieces (measured: train/measure.py), so the
// device remembers the last one for it and reminds it just before it speaks or thinks
int lastEventSign = -1;
uint32_t lastEventAt = 0;
const uint32_t REMIND_MS = 120000;

int findPiece(const char *s, int len) {
  int lo = 0, hi = pieceCount - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, id = pieceSorted[mid];
    int plen = pieceOff[id + 1] - pieceOff[id];
    int c = memcmp(s, pieceText + pieceOff[id], min(len, plen));
    if (c == 0) c = len - plen;
    if (c == 0) return id;
    if (c < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return -1;
}

// the same cutting rule as train/pieces.py: lowercase; words of letters, digits, ' and -;
// the marks . , ? ! alone; each word cut into the longest known pieces, the first led by "_"
void cut(const String &text, std::vector<uint16_t> &out, std::vector<uint16_t> *subject = nullptr) {
  int n = text.length(), i = 0;
  char buf[24];
  while (i < n) {
    char c = tolower((unsigned char)text[i]);
    if (c == '.' || c == ',' || c == '?' || c == '!') {
      int id = findPiece(&c, 1);
      if (id >= 0) out.push_back(id);
      i++;
      continue;
    }
    if (!(isalnum((unsigned char)c) || c == '\'' || c == '-')) {
      i++;
      continue;
    }
    int start = i;
    while (i < n && (isalnum((unsigned char)text[i]) || text[i] == '\'' || text[i] == '-')) i++;
    int pos = start;
    size_t firstOfWord = out.size();
    while (pos < i) {
      bool first = pos == start;
      int maxLen = min(pieceLongest - (first ? 1 : 0), i - pos), took = 0;
      for (int len = maxLen; len > 0 && !took; len--) {
        int k = 0;
        if (first) buf[k++] = '_';
        for (int j = 0; j < len; j++) buf[k++] = tolower((unsigned char)text[pos + j]);
        int id = findPiece(buf, k);
        if (id >= 0) {
          out.push_back(id);
          took = len;
        }
      }
      pos += took ? took : 1;
    }
    if (subject && i - start >= 4)   // the longer words carry the subject
      for (size_t k = firstOfWord; k < out.size(); k++) subject->push_back(out[k]);
  }
}

void appendPiece(String &s, int id) {
  const char *p = pieceText + pieceOff[id];
  int len = pieceOff[id + 1] - pieceOff[id];
  if (id == S_TIME) s += " {time}";
  else if (id == S_PCT) s += " {pct}";
  else if (len == 1 && strchr(".,?!", p[0])) s += p[0];
  else if (p[0] == '_') {
    if (s.length()) s += ' ';
    for (int i = 1; i < len; i++) s += p[i];
  } else
    for (int i = 0; i < len; i++) s += p[i];
}

void step(int id, bool wantLogits) {
  mind_step(&model, h, id, wantLogits ? logits : nullptr, scratch);
  haveLogits = wantLogits;
  lifetime++;
  dirty = true;
}

void write(bool thought) {
  uint32_t t0 = micros();
  int made = 0;
  step(thought ? S_THOUGHT : S_WINK, true);
  int mood = mind_pick(logits, model.V, 0.6f, 11, allowMood, &rng);
  step(mood, true);
  String text;
  int last = -1, repeats = 0;
  for (int i = 0; i < 44; i++) {
    if (!thought)
      for (int v = S_TEXT0; v < model.V; v++)
        if (favour[v]) logits[v] += favour[v] * FAVOUR;
    int id = mind_pick(logits, model.V, thought ? 0.75f : TEMPERATURE, thought ? 20 : TOPK, allowText, &rng);
    favour[id] = favour[id] * 3 / 10;   // once it has used a word, the pull to use it again mostly goes
    if (id == last && ++repeats >= 2) id = S_END;   // it has got stuck on a piece: stop there
    else if (id != last) repeats = 0;
    last = id;
    step(id, true);
    made++;
    if (id == S_END) break;
    appendPiece(text, id);
    xSemaphoreTake(lock, portMAX_DELAY);
    partialText = text;
    xSemaphoreGive(lock);
    if ((i & 3) == 3) vTaskDelay(1);
  }
  if (made) msPerPiece = (micros() - t0) / 1000.0f / (made + 2);
  auto *r = new think::Result;
  r->thought = thought;
  r->text = text;
  r->mood = (Mood)constrain(mood - S_MOOD0, 0, MOOD_COUNT - 1);
  if (xQueueSend(outQ, &r, 0) != pdTRUE) delete r;
  xSemaphoreTake(lock, portMAX_DELAY);
  partialText = "";
  xSemaphoreGive(lock);
}

void task(void *) {
  for (;;) {
    Cmd c;
    if (xQueueReceive(cmdQ, &c, portMAX_DELAY) != pdTRUE) continue;
    working = true;
    if (c.kind == 0) {
      for (int i = 0; i < c.n; i++) {
        step(c.ids[i], false);
        if ((i & 3) == 3) vTaskDelay(1);
      }
      free(c.ids);
    } else {
      write(c.kind == 2);
    }
    working = uxQueueMessagesWaiting(cmdQ) > 0;
  }
}

void feed(std::vector<uint16_t> &ids) {
  if (!loaded || ids.empty()) return;
  Cmd c = {0, (uint16_t *)malloc(ids.size() * 2), (uint16_t)ids.size()};
  if (!c.ids) return;
  memcpy(c.ids, ids.data(), ids.size() * 2);
  working = true;
  if (xQueueSend(cmdQ, &c, 0) != pdTRUE) free(c.ids);
}
}  // namespace

bool think::begin() {
  if (loaded) return true;
  size_t n = wink_model_end - wink_model_start;
  if (mind_load(&model, wink_model_start, n)) return false;
  const uint8_t *p = wink_pieces_start;
  if (memcmp(p, "WPC1", 4)) return false;
  memcpy(&pieceCount, p + 4, 2);
  memcpy(&pieceLongest, p + 6, 2);
  pieceOff = (const uint16_t *)(p + 8);
  pieceSorted = pieceOff + pieceCount + 1;
  pieceText = (const char *)(pieceSorted + pieceCount);
  if (pieceCount != model.V) return false;

  h = (float *)calloc(model.H, sizeof(float));
  logits = (float *)malloc(model.V * sizeof(float));
  scratch = malloc(mind_scratch_bytes(&model));
  allowText = (uint8_t *)calloc(model.V, 1);
  allowMood = (uint8_t *)calloc(model.V, 1);
  favour = (uint8_t *)calloc(model.V, 1);
  if (!h || !logits || !scratch || !allowText || !allowMood || !favour) return false;
  for (int v = S_TEXT0; v < model.V; v++) allowText[v] = 1;
  allowText[S_END] = allowText[S_TIME] = allowText[S_PCT] = 1;
  for (int v = S_MOOD0; v < S_TEXT0; v++) allowMood[v] = 1;
  rng = esp_random();

  // which mind is this? its state only means something to the weights that made it
  stamp = (uint32_t)n;
  for (size_t i = 0; i < n; i += n / 251 + 1) stamp = stamp * 31 + wink_model_start[i];
  File f = LittleFS.open("/mind.state");
  if (f && f.size() == (size_t)(8 + model.H * 4)) {
    uint32_t head[2];
    f.read((uint8_t *)head, 8);
    if (head[0] == stamp) {
      f.read((uint8_t *)h, model.H * 4);
      lifetime = head[1];
    }
  }
  if (f) f.close();

  cmdQ = xQueueCreate(12, sizeof(Cmd));
  outQ = xQueueCreate(4, sizeof(think::Result *));
  lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "mind", 6144, nullptr, 1, nullptr, 0);
  loaded = true;
  return true;
}

bool think::ready() { return loaded; }

void think::hear(const String &text) {
  std::vector<uint16_t> ids = {S_YOU}, subject;
  cut(text.substring(0, 160), ids, &subject);
  if (loaded) {
    memset(favour, 0, model.V);
    for (uint16_t id : subject) favour[id] = 255;
  }
  feed(ids);
}

void think::said(const String &text, Mood mood) {
  std::vector<uint16_t> ids = {S_WINK, (uint16_t)(S_MOOD0 + mood)};
  cut(text.substring(0, 200), ids);
  ids.push_back(S_END);
  feed(ids);
}

void think::event(const char *name) {
  for (int i = 0; i < 16; i++)
    if (!strcmp(name, EVENT_NAMES[i])) {
      std::vector<uint16_t> ids = {(uint16_t)(S_EVENT0 + i)};
      feed(ids);
      if (strcmp(name, "quiet")) {   // quiet is the absence of events; it does not replace one
        lastEventSign = S_EVENT0 + i;
        lastEventAt = millis();
      }
      return;
    }
}

static void remind() {
  if (lastEventSign >= 0 && millis() - lastEventAt < REMIND_MS) {
    std::vector<uint16_t> ids = {(uint16_t)lastEventSign};
    feed(ids);
  }
}

void think::reply() {
  if (!loaded) return;
  remind();
  Cmd c = {1, nullptr, 0};
  working = true;
  xQueueSend(cmdQ, &c, 0);
}
void think::muse() {
  if (!loaded || working) return;
  remind();
  Cmd c = {2, nullptr, 0};
  working = true;
  xQueueSend(cmdQ, &c, 0);
}

bool think::poll(Result &r) {
  Result *p;
  if (!loaded || xQueueReceive(outQ, &p, 0) != pdTRUE) return false;
  r = *p;
  delete p;
  return true;
}

bool think::busy() { return working; }
String think::partial() {
  if (!loaded) return "";
  xSemaphoreTake(lock, portMAX_DELAY);
  String s = partialText;
  xSemaphoreGive(lock);
  return s;
}
const float *think::state() { return loaded ? h : nullptr; }
int think::size() { return loaded ? model.H : 0; }
uint32_t think::age() { return lifetime; }
float think::pace() { return msPerPiece; }

void think::save() {
  if (!loaded || !dirty || working) return;
  File f = LittleFS.open("/mind.state", FILE_WRITE);
  if (!f) return;
  uint32_t head[2] = {stamp, lifetime};
  f.write((uint8_t *)head, 8);
  f.write((uint8_t *)h, model.H * 4);
  f.close();
  dirty = false;
}

String think::about() {
  if (!loaded) return "no mind of its own loaded";
  char b[160];
  snprintf(b, sizeof(b), "%d pieces, a state of %d, %u weights; %u pieces lived; %.0f ms a piece", model.V, model.H,
           (unsigned)(model.V * model.E + 3 * model.H * (model.E + model.H) + model.E * model.H), (unsigned)lifetime, (float)msPerPiece);
  return b;
}
