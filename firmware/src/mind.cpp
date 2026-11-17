// The small mind. It matches words and picks from lines packed ahead of time
// (character/lines.json). It does not understand; it says so when a question is past it.
#include "mind.h"
#include "lines_data.h"
#include "voice.h"
#include <LittleFS.h>

namespace {

uint32_t used[sizeof(LINE_SETS) / sizeof(LINE_SETS[0])];   // which lines of each situation have been said
int8_t lastPick[sizeof(LINE_SETS) / sizeof(LINE_SETS[0])];

int findSet(const char *key) {
  for (int i = 0; i < LINE_SET_COUNT; i++)
    if (!strcmp(LINE_SETS[i].key, key)) return i;
  return -1;
}

struct Rule {
  const char *key;
  const char *phrases;   // separated by |, matched as whole words
};

// first match wins, so the narrow ones come first
const Rule RULES[] = {
    {"love", "i love you|love you|luv you|love u|i like you|like you a lot"},
    {"feel.down", "i am sad|im sad|i feel sad|feeling sad|so tired|i am tired|im tired|stressed|depressed|lonely|anxious|bad day|exhausted|i feel bad|feeling down|i am down|worn out|burnt out|burned out"},
    {"who_are_you", "who are you|your name|whats your name|what is your name|what are you called|who is this|who r u|introduce yourself"},
    {"what_are_you", "what are you|where are you|are you alive|are you real|are you claude|are you an ai|are you a robot|what is this|are you a computer|are you human|how do you work|where do you live|are you conscious"},
    {"how_are_you", "how are you|how r u|hows it going|how is it going|you ok|you okay|how do you feel|how you doing|how are things|whats up|what is up|sup|how have you been"},
    {"help", "help|what can you do|what do you do|commands|how do i|menu|what now|instructions|how does this work"},
    {"joke", "joke|jokes|make me laugh|something funny|tell me something funny"},
    {"sing", "sing|song|music|tune|hum"},
    {"story", "story|tale|once upon"},
    {"time", "time|clock|what hour|the hour"},
    {"battery", "battery|power|charge|energy|juice|hungry"},
    {"morning", "good morning|morning"},
    {"night", "good night|goodnight|night night"},
    {"bye", "bye|goodbye|see you|see ya|later|cya|farewell|gotta go|i am off|im off"},
    {"thanks", "thanks|thank you|thx|cheers|shukran|ty|much appreciated"},
    {"sorry", "sorry|my bad|apologies|i apologise|i apologize|forgive me"},
    {"rude", "stupid|dumb|idiot|shut up|hate you|useless|you suck|bad robot|annoying|boring|ugly|moron|fool|trash|garbage|worthless|go away|fuck|shit|damn you|screw you"},
    {"praise", "good job|nice|well done|clever|smart|cute|cool|awesome|great|good boy|good girl|you rock|brilliant|amazing|love it|nice one|good one|impressive|wow|bravo|perfect|excellent|funny"},
    {"laugh", "haha|hahaha|lol|lmao|hehe|ha ha|rofl|heh"},
    {"greet", "hello|hi|hey|yo|hiya|howdy|hola|salam|marhaba|ahlan|good afternoon|good evening|greetings|hello there|hi there"},
    {"yes", "yes|yeah|yep|yup|ok|okay|sure|alright|right|indeed|correct"},
    {"no", "no|nope|nah|never|not really"},
};

bool hasPhrase(const String &padded, const char *phrases) {
  const char *p = phrases;
  while (*p) {
    const char *bar = strchr(p, '|');
    size_t n = bar ? (size_t)(bar - p) : strlen(p);
    String needle = " " + String(p).substring(0, n) + " ";
    if (padded.indexOf(needle) >= 0) return true;
    p += n + (bar ? 1 : 0);
  }
  return false;
}

// "12 * 3", "what is 7 plus 9", "100 divided by 8"
bool sum(const String &norm, double &result) {
  String s = " " + norm + " ";
  s.replace(" plus ", " + ");
  s.replace(" minus ", " - ");
  s.replace(" times ", " * ");
  s.replace(" multiplied by ", " * ");
  s.replace(" x ", " * ");
  s.replace(" divided by ", " / ");
  s.replace(" over ", " / ");
  int i = 0, n = s.length();
  auto number = [&](double &out) {
    while (i < n && s[i] == ' ') i++;
    int start = i;
    if (i < n && s[i] == '-') i++;
    bool digits = false;
    while (i < n && (isdigit((unsigned char)s[i]) || s[i] == '.')) {
      if (s[i] != '.') digits = true;
      i++;
    }
    if (!digits) return false;
    out = s.substring(start, i).toDouble();
    return true;
  };
  while (i < n && !isdigit((unsigned char)s[i])) i++;   // skip "what is"
  if (i > 0 && s[i - 1] == '-') i--;
  double a, b;
  if (!number(a)) return false;
  bool any = false;
  for (;;) {
    while (i < n && s[i] == ' ') i++;
    if (i >= n) break;
    char op = s[i];
    if (op != '+' && op != '-' && op != '*' && op != '/') break;
    i++;
    if (!number(b)) return false;
    if (op == '+') a += b;
    else if (op == '-') a -= b;
    else if (op == '*') a *= b;
    else {
      if (b == 0) return false;
      a /= b;
    }
    any = true;
  }
  result = a;
  return any;
}

String numberSpoken(double v) {
  long whole = (long)v;
  if (fabs(v - whole) < 0.0005) return voice::words(whole);
  long tenths = lround(fabs(v - whole) * 100);
  String out = voice::words(whole) + " point";
  out += " " + voice::words(tenths / 10);
  if (tenths % 10) out += " " + voice::words(tenths % 10);
  return out;
}

// "the time is 16:10", "it is 4 10", "set time 9:05"
bool toldTime(const String &norm) {
  if (norm.indexOf("time is ") < 0 && norm.indexOf("set time") < 0 && norm.indexOf("it is ") != 0 && norm.indexOf("its ") != 0) return false;
  int i = 0, n = norm.length();
  while (i < n && !isdigit((unsigned char)norm[i])) i++;
  if (i >= n) return false;
  int h = 0, m = 0;
  while (i < n && isdigit((unsigned char)norm[i])) h = h * 10 + (norm[i++] - '0');
  while (i < n && !isdigit((unsigned char)norm[i]) && norm[i] != 'a' && norm[i] != 'p') i++;
  while (i < n && isdigit((unsigned char)norm[i])) m = m * 10 + (norm[i++] - '0');
  if (norm.indexOf("pm") > 0 && h < 12) h += 12;
  if (h > 23 || m > 59) return false;
  os::setClock(h, m);
  return true;
}
}  // namespace

bool mind::has(const char *key) { return findSet(key) >= 0; }

Line mind::pick(const char *key) {
  int s = findSet(key);
  if (s < 0) return {"...", CALM};
  const LineSet &set = LINE_SETS[s];
  uint32_t all = set.count >= 32 ? 0xFFFFFFFFu : ((1u << set.count) - 1);
  if ((used[s] & all) == all) used[s] = set.count > 1 ? (1u << lastPick[s]) : 0;   // start over, but not with the last one
  int choice;
  do {
    choice = random(set.count);
  } while (used[s] & (1u << (choice & 31)));
  used[s] |= 1u << (choice & 31);
  lastPick[s] = choice;
  return set.lines[choice];
}

String mind::fill(const char *tmpl, long n) {
  String s = tmpl;
  if (s.indexOf('{') < 0) return s;
  s.replace("{name}", cfg.name);
  s.replace("{time}", os::spokenTime());
  s.replace("{pct}", voice::words(max(0, (int)M5Cardputer.Power.getBatteryLevel())));
  s.replace("{hours}", voice::words(n));
  s.replace("{n}", voice::words(n));
  return s;
}

mind::Answer mind::answer(const String &typed) {
  Answer a;
  String norm;
  for (size_t i = 0; i < typed.length(); i++) {
    char c = tolower((unsigned char)typed[i]);
    if (isalnum((unsigned char)c) || c == '+' || c == '*' || c == '/' || c == '.' || c == '-') norm += c;
    else if (c == '\'') continue;
    else if (c == ':') norm += ' ';
    else norm += ' ';
  }
  while (norm.indexOf("  ") >= 0) norm.replace("  ", " ");
  norm.trim();
  while (norm.endsWith(".")) norm.remove(norm.length() - 1);
  String padded = " " + norm + " ";

  auto use = [&](const char *key) {
    Line l = pick(key);
    a.say = fill(l.say);
    a.mood = l.mood;
  };

  double v;
  if (sum(norm, v) && fabs(v) < 1e6) {
    Line l = pick("sum");
    a.say = l.say;
    a.say.replace("{n}", numberSpoken(v));
    a.say = fill(a.say.c_str());
    a.mood = l.mood;
    a.grounded = true;
    return a;
  }
  if (toldTime(norm)) {
    use("time");
    a.grounded = true;
    return a;
  }

  // keys hammered at random: one long run with hardly a vowel in it
  if (norm.length() >= 7 && norm.indexOf(' ') < 0) {
    int vowels = 0;
    for (size_t i = 0; i < norm.length(); i++)
      if (strchr("aeiouy", norm[i])) vowels++;
    if (vowels * 5 < (int)norm.length()) {
      use("touch.mash");
      return a;
    }
  }

  // what the whole mind packed for the trip comes before the general lines
  File learned = LittleFS.open("/learned.tsv");
  while (learned && learned.available()) {
    String row = learned.readStringUntil('\n');
    int t1 = row.indexOf('\t'), t2 = row.indexOf('\t', t1 + 1), t3 = row.indexOf('\t', t2 + 1);
    if (t1 <= 0 || t2 < 0) continue;
    if (!hasPhrase(padded, row.substring(0, t1).c_str())) continue;
    a.say = row.substring(t1 + 1, t2);
    a.mood = moodFromName(row.substring(t2 + 1, t3 < 0 ? row.length() : t3).c_str());
    a.grounded = true;
    return a;
  }

  for (auto &r : RULES) {
    if (!hasPhrase(padded, r.phrases)) continue;
    const char *key = r.key;
    if (!strcmp(key, "time") && !os::timeKnown()) key = "time.unknown";
    if (!strcmp(key, "battery")) {
      int pct = M5Cardputer.Power.getBatteryLevel();
      if (pct >= 0 && pct <= 7) key = "battery.critical";
      else if (pct >= 0 && pct <= 20) key = "battery.low";
    }
    use(key);
    a.sing = !strcmp(key, "sing");
    a.grounded = !strncmp(key, "time", 4) || !strncmp(key, "battery", 7);
    a.quiet = !strcmp(key, "feel.down") || !strcmp(key, "love");
    return a;
  }
  use("unknown");
  a.unknown = true;
  return a;
}
