// The other rooms: the menu, a terminal, notes, files, a clock, a game, the body, settings.
#include "kernel.h"
#include "face.h"
#include "link.h"
#include "mind.h"
#include "ui.h"
#include "voice.h"
#include <LittleFS.h>
#include <SD.h>

fs::FS *pathFs(String &path);   // shell.cpp

namespace {

// ============================================================ the rooms ====
struct RoomEntry {
  const char *name, *what;
  App *(*get)();
};
const RoomEntry ROOMS[] = {
    {"Talk", "back to my face", apps::home},         {"Terminal", "commands", apps::terminal},
    {"Notes", "things to keep", apps::notes},        {"Files", "the chip and the card", apps::files},
    {"Clock", "time, timer, stopwatch", apps::clockRoom}, {"Echo", "a memory game", apps::game},
    {"Memory", "what I keep about you", apps::memory}, {"Body", "what I am made of", apps::body},
    {"Settings", "Wi-Fi, voice, screen", apps::settings},
};
const int ROOM_COUNT = sizeof(ROOMS) / sizeof(ROOMS[0]);

struct Rooms : App {
  ui::List list;
  const char *title() override { return "Rooms"; }
  void key(const Key &k) override {
    if (list.key(k, ROOM_COUNT)) return;
    if (k.code == K_ENTER || k.code == K_RIGHT) os::open(ROOMS[list.sel].get());
    else if (k.code == K_ESC || k.code == K_TAB || k.code == K_LEFT) os::home();
    else if (k.code == K_CHAR)
      for (int i = 0; i < ROOM_COUNT; i++)
        if (tolower(ROOMS[i].name[0]) == tolower(k.ch)) list.sel = i;
  }
  void draw(Gfx &g) override {
    ui::background(g);
    ui::header(g, cfg.name.c_str());
    list.draw(g, ui::HEAD_ROW + 4, 6, ROOM_COUNT, [](int i) { return String(ROOMS[i].name); },
              [](int i) { return String(ROOMS[i].what); });
    ui::hint(g, "; up   . down   enter open   ` back");
  }
} roomsApp;

// ============================================================ terminal =====
struct Terminal : App {
  std::vector<String> lines;
  ui::Field input;
  String last;
  int scroll = 0;
  struct Out : Print {
    Terminal *t;
    String cur;
    size_t write(uint8_t c) override {
      if (c == '\r') return 1;
      if (c == '\n' || cur.length() >= 39) {
        t->push(cur);
        cur = "";
        if (c == '\n') return 1;
      }
      cur += (char)c;
      return 1;
    }
    void flush() {
      if (cur.length()) t->push(cur);
      cur = "";
    }
  } out;

  Terminal() { out.t = this; }
  void push(const String &s) {
    lines.push_back(s);
    if (lines.size() > 90) lines.erase(lines.begin());
  }
  const char *title() override { return "Terminal"; }
  bool arrowsBare() override { return false; }
  void enter() override {
    if (lines.empty()) {
      push(cfg.name + " " + OS_VERSION + ". Type help.");
    }
    scroll = 0;
  }
  void key(const Key &k) override {
    if (k.code == K_ESC) {
      os::home();
      return;
    }
    if (k.code == K_UP) {
      if (!input.text.length()) input.text = last;
      else scroll = min(scroll + 4, max(0, (int)lines.size() - 11));
      return;
    }
    if (k.code == K_DOWN) {
      scroll = max(0, scroll - 4);
      return;
    }
    if (k.code == K_ENTER) {
      String c = input.text;
      c.trim();
      input.text = "";
      scroll = 0;
      if (!c.length()) return;
      last = c;
      push("> " + c);
      if (c == "clear") lines.clear();
      else if (c == "exit") os::home();
      else {
        shell::exec(c, out);
        out.flush();
      }
      return;
    }
    input.key(k);
  }
  void draw(Gfx &g) override {
    g.fillScreen(COL_BG);
    ui::header(g, "Terminal");
    g.setFont(ui::fSmall);
    g.setTextDatum(textdatum_t::top_left);
    const int rows = 11;
    int first = max(0, (int)lines.size() - rows - scroll);
    for (int i = 0; i < rows && first + i < (int)lines.size(); i++) {
      const String &l = lines[first + i];
      g.setTextColor(l.startsWith("> ") ? COL_YOU : COL_INK);
      g.drawString(l, 3, ui::HEAD_ROW + 3 + i * 9);
    }
    g.drawFastHLine(0, SCREEN_H - 16, SCREEN_W, COL_RULE);
    input.draw(g, 3, SCREEN_H - 15, SCREEN_W - 6, COL_YOU, true, "> ");
  }
} terminalApp;

// ============================================================ notes ========
struct Notes : App {
  std::vector<String> names, titles;
  ui::List list;
  bool editing = false;
  String text, path;
  int armedDelete = -1;

  const char *title() override { return "Notes"; }
  bool arrowsBare() override { return !editing; }
  void load() {
    names.clear();
    titles.clear();
    File dir = LittleFS.open("/notes");
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (f.isDirectory()) continue;
      names.push_back(String("/notes/") + f.name());
      String first = f.readStringUntil('\n');
      first.trim();
      titles.push_back(first.length() ? first.substring(0, 30) : String("(empty)"));
    }
  }
  void enter() override {
    editing = false;
    armedDelete = -1;
    load();
  }
  void save() {
    String t = text;
    t.trim();
    if (!t.length()) {
      if (path.length()) LittleFS.remove(path);
      return;
    }
    if (!path.length()) {
      for (int i = 1; i < 500; i++) {
        path = "/notes/" + String(i) + ".txt";
        if (!LittleFS.exists(path)) break;
      }
    }
    File f = LittleFS.open(path, FILE_WRITE);
    if (f) {
      f.print(text);
      f.close();
      os::sayLine("note.saved", false);
    }
  }
  void key(const Key &k) override {
    if (editing) {
      if (k.code == K_ESC) {
        save();
        editing = false;
        load();
      } else if (k.code == K_ENTER) text += '\n';
      else if (k.code == K_DEL) {
        if (text.length()) text.remove(text.length() - 1);
      } else if (k.code == K_CHAR && text.length() < 3000) text += k.ch;
      return;
    }
    int count = names.size() + 1;
    if (list.key(k, count)) {
      armedDelete = -1;
      return;
    }
    if (k.code == K_ESC || k.code == K_LEFT) os::open(apps::rooms());
    else if (k.code == K_ENTER || k.code == K_RIGHT) {
      text = "";
      path = "";
      if (list.sel > 0) {
        path = names[list.sel - 1];
        File f = LittleFS.open(path);
        if (f) text = f.readString();
      }
      editing = true;
    } else if (k.code == K_DEL && list.sel > 0) {
      if (armedDelete == list.sel) {
        LittleFS.remove(names[list.sel - 1]);
        armedDelete = -1;
        load();
      } else armedDelete = list.sel;
    }
  }
  void draw(Gfx &g) override {
    ui::background(g);
    if (!editing) {
      ui::header(g, "Notes");
      int count = names.size() + 1;
      list.draw(g, ui::HEAD_ROW + 4, 6, count, [&](int i) { return i == 0 ? String("+ new note") : titles[i - 1]; },
                [&](int i) { return i == armedDelete ? String("del again to erase") : String(); });
      ui::hint(g, "enter open   del erase   ` back");
      return;
    }
    ui::header(g, "Note", "` saves");
    g.setFont(ui::fBody);
    auto lines = ui::wrap(g, text, SCREEN_W - 10);
    const int rows = 8;
    int first = max(0, (int)lines.size() - rows);
    g.setTextColor(COL_INK);
    int y = ui::HEAD_ROW + 4, cx = 5, cy = y;
    for (int i = first; i < (int)lines.size(); i++) {
      String part = text.substring(lines[i].start, lines[i].end);
      g.drawString(part, 5, y);
      cx = 5 + g.textWidth(part);
      cy = y;
      y += ui::TEXT_ROW;
    }
    if (text.endsWith("\n")) {
      cx = 5;
      cy = y;
    }
    if ((millis() / 400) % 2 == 0) g.fillRect(cx + 1, cy + 1, 2, ui::TEXT_ROW - 3, COL_CORAL);
    face::lookAt(cx, cy, 300);
  }
} notesApp;

// ============================================================ files ========
struct Files : App {
  struct Entry {
    String name;
    bool dir;
    uint32_t size;
  };
  std::vector<Entry> entries;
  ui::List list;
  String path = "";   // "" is the top: chip and card
  bool viewing = false;
  String body;
  std::vector<ui::Span> bodyLines;
  int scroll = 0;

  const char *title() override { return "Files"; }
  void load() {
    entries.clear();
    list.sel = list.top = 0;
    if (!path.length()) {
      entries.push_back({"chip", true, 0});
      entries.push_back({"card", true, 0});
      return;
    }
    String p = path.startsWith("/card") ? "/sd" + path.substring(5) : path.substring(5);
    fs::FS *fs = pathFs(p);
    if (!fs) return;
    File dir = fs->open(p.length() ? p : "/");
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f && entries.size() < 120; f = dir.openNextFile())
      entries.push_back({String(f.name()), f.isDirectory(), (uint32_t)f.size()});
  }
  void enter() override {
    viewing = false;
    load();
  }
  void up() {
    if (!path.length()) {
      os::open(apps::rooms());
      return;
    }
    int slash = path.lastIndexOf('/');
    path = slash > 0 ? path.substring(0, slash) : String("");
    load();
  }
  void key(const Key &k) override {
    if (viewing) {
      if (k.code == K_ESC || k.code == K_LEFT) viewing = false;
      else if (k.code == K_DOWN) scroll = min(scroll + 3, max(0, (int)bodyLines.size() - 7));
      else if (k.code == K_UP) scroll = max(0, scroll - 3);
      return;
    }
    if (list.key(k, entries.size())) return;
    if (k.code == K_ESC || k.code == K_LEFT) up();
    else if ((k.code == K_ENTER || k.code == K_RIGHT) && entries.size()) {
      Entry &e = entries[list.sel];
      if (e.dir) {
        path += "/" + e.name;
        load();
      } else {
        String full = path + "/" + e.name;
        String p = full.startsWith("/card") ? "/sd" + full.substring(5) : full.substring(5);
        fs::FS *fs = pathFs(p);
        File f = fs ? fs->open(p) : File();
        body = "";
        while (f && f.available() && body.length() < 3000) {
          char c = f.read();
          body += (c == '\n' || (c >= 32 && c < 127)) ? c : '.';
        }
        os::canvas().setFont(ui::fBody);
        bodyLines = ui::wrap(os::canvas(), body, SCREEN_W - 10);
        scroll = 0;
        viewing = true;
      }
    }
  }
  void draw(Gfx &g) override {
    ui::background(g);
    if (viewing) {
      ui::header(g, entries[list.sel].name.c_str());
      g.setFont(ui::fBody);
      g.setTextColor(COL_INK);
      for (int i = 0; i < 8 && scroll + i < (int)bodyLines.size(); i++)
        g.drawString(body.substring(bodyLines[scroll + i].start, bodyLines[scroll + i].end), 5, ui::HEAD_ROW + 4 + i * ui::TEXT_ROW);
      return;
    }
    ui::header(g, path.length() ? path.c_str() : "Files");
    if (entries.empty()) {
      g.setFont(ui::fList);
      g.setTextColor(COL_DIM);
      g.drawString(path.startsWith("/card") ? "no card, or an empty one" : "nothing here yet", 10, ui::HEAD_ROW + 8);
    }
    list.draw(g, ui::HEAD_ROW + 4, 6, entries.size(), [&](int i) { return entries[i].name + (entries[i].dir ? "/" : ""); },
              [&](int i) { return entries[i].dir ? String() : String(entries[i].size); });
    ui::hint(g, "enter open   ` back");
  }
} filesApp;

// ============================================================ clock ========
uint32_t timerEndsAt = 0;     // millis; 0 when no timer runs
String timerSay;              // what to say when it ends, if the whole mind set it
uint32_t swStart = 0, swHeld = 0;
bool swRunning = false;

struct ClockRoom : App {
  int mode = 0;   // 0 clock, 1 timer, 2 stopwatch
  String digits;
  const char *title() override { return "Clock"; }
  void key(const Key &k) override {
    if (k.code == K_ESC) {
      os::open(apps::rooms());
      return;
    }
    if (k.code == K_LEFT) mode = (mode + 2) % 3, digits = "";
    else if (k.code == K_RIGHT) mode = (mode + 1) % 3, digits = "";
    else if (k.code == K_CHAR && isdigit((unsigned char)k.ch) && digits.length() < 4) digits += k.ch;
    else if (k.code == K_DEL) {
      if (digits.length()) digits.remove(digits.length() - 1);
      else if (mode == 2 && !swRunning) swHeld = 0;
      else if (mode == 1) timerEndsAt = 0;
    } else if (k.code == K_ENTER) {
      if (mode == 0 && digits.length() >= 3) {
        int v = digits.toInt();
        if (v / 100 < 24 && v % 100 < 60) os::setClock(v / 100, v % 100);
        digits = "";
      } else if (mode == 1) {
        if (digits.length()) {
          timerEndsAt = millis() + (uint32_t)digits.toInt() * 60000UL;
          if (timerEndsAt == 0) timerEndsAt = 1;
          digits = "";
        } else timerEndsAt = 0;
      } else if (mode == 2) {
        if (swRunning) swHeld += millis() - swStart;
        else swStart = millis();
        swRunning = !swRunning;
      }
    }
  }
  void big(Gfx &g, const String &s, uint16_t col) {
    g.setFont(&fonts::Font7);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(col);
    g.drawString(s, SCREEN_W / 2, 62);
    g.setTextDatum(textdatum_t::top_left);
  }
  void small(Gfx &g, const String &s) {
    g.setFont(ui::fList);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(COL_DIM);
    g.drawString(s, SCREEN_W / 2, 104);
    g.setTextDatum(textdatum_t::top_left);
  }
  void draw(Gfx &g) override {
    ui::background(g);
    const char *names[] = {"Clock", "Timer", "Stopwatch"};
    ui::header(g, names[mode], "");
    char b[16];
    if (mode == 0) {
      if (os::timeKnown()) {
        big(g, os::clockText(), COL_INK);
        small(g, digits.length() ? "set to " + digits : String("type 1610 + enter to set"));
      } else {
        big(g, "--:--", COL_FAINT);
        small(g, digits.length() ? "set to " + digits : String("I have not been told the time"));
      }
    } else if (mode == 1) {
      if (timerEndsAt) {
        uint32_t left = (timerEndsAt - millis()) / 1000;
        snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(left / 60), (unsigned)(left % 60));
        big(g, b, COL_CORAL);
        small(g, "enter stops it");
      } else {
        big(g, digits.length() ? digits : String("0"), digits.length() ? COL_INK : COL_FAINT);
        small(g, "minutes, then enter");
      }
    } else {
      uint32_t ms = swHeld + (swRunning ? millis() - swStart : 0);
      snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(ms / 60000), (unsigned)((ms / 1000) % 60));
      big(g, b, swRunning ? COL_CORAL : COL_INK);
      small(g, swRunning ? "enter stops" : "enter starts   del clears");
    }
    ui::hint(g, ", /  clock  timer  stopwatch");
  }
} clockApp;

// ============================================================ echo =========
// a memory game: it plays a run of four tones, you play it back
struct Game : App {
  enum { G_INTRO, G_SHOW, G_INPUT, G_OVER } st = G_INTRO;
  std::vector<uint8_t> seq;
  size_t pos = 0;
  uint32_t at = 0;
  int lit = -1;
  uint32_t litUntil = 0;
  int best = 0;
  const uint16_t tones[4] = {523, 659, 784, 1047};
  const char keys[4] = {'d', 'f', 'j', 'k'};

  const char *title() override { return "Echo"; }
  void start() {
    seq.clear();
    seq.push_back(random(4));
    pos = 0;
    st = G_INTRO;
    at = millis();
    os::sayLine("game.start");
  }
  void enter() override { start(); }
  void leave() override { os::quiet(); }
  void flash(int pad, int ms) {
    lit = pad;
    litUntil = millis() + ms;
    voice::tone(tones[pad], ms - 40);
    face::lookAt(40 + pad * 54, 150, ms);
  }
  void key(const Key &k) override {
    if (k.code == K_ESC) {
      os::open(apps::rooms());
      return;
    }
    if (st == G_OVER) {
      if (k.code == K_ENTER) start();
      return;
    }
    if (st != G_INPUT || k.code != K_CHAR) return;
    int pad = -1;
    for (int i = 0; i < 4; i++)
      if (tolower(k.ch) == keys[i]) pad = i;
    if (pad < 0) return;
    flash(pad, 220);
    if (pad != seq[pos]) {
      st = G_OVER;
      best = max(best, (int)seq.size() - 1);
      voice::chime(CH_LOSE);
      Line l = mind::pick("game.over");
      os::say(mind::fill(l.say, seq.size()), l.mood);
      return;
    }
    if (++pos == seq.size()) {
      face::flash(HAPPY, 900);
      if (seq.size() % 3 == 0) os::sayLine("game.good");
      seq.push_back(random(4));
      pos = 0;
      st = G_SHOW;
      at = millis() + 900;
    }
  }
  void tick(float) override {
    uint32_t now = millis();
    if (st == G_INTRO && now - at > 600 && !voice::busy() && !os::speech().active) {
      st = G_SHOW;
      pos = 0;
      at = now + 300;
    }
    if (st == G_SHOW && now >= at && !voice::speaking()) {
      if (pos < seq.size()) {
        flash(seq[pos], 380);
        pos++;
        at = now + 520;
      } else if (now >= litUntil + 150) {
        pos = 0;
        st = G_INPUT;
      }
    }
  }
  void draw(Gfx &g) override {
    ui::background(g);
    char b[24];
    snprintf(b, sizeof(b), "round %d", (int)seq.size());
    ui::header(g, "Echo", b);
    bool on = millis() < litUntil;
    const uint16_t cols[4] = {rgb(222, 112, 76), rgb(232, 160, 40), rgb(60, 120, 180), rgb(160, 100, 190)};
    for (int i = 0; i < 4; i++) {
      int x = 14 + i * 55, y = 32;
      bool hot = on && lit == i;
      // each pad is a small field of dots, like the face
      for (int dy = 0; dy < 9; dy++)
        for (int dx = 0; dx < 9; dx++) {
          int s = hot ? 4 : 2;
          g.fillRect(x + dx * 5 + (5 - s) / 2, y + dy * 5 + (5 - s) / 2, s, s, hot ? cols[i] : rgb(224, 204, 186));
        }
      g.setFont(ui::fList);
      g.setTextDatum(textdatum_t::top_center);
      g.setTextColor(hot ? COL_INK : COL_DIM);
      char label[2] = {keys[i], 0};
      g.drawString(label, x + 22, y + 50);
    }
    g.setTextDatum(textdatum_t::top_left);
    const char *msg = st == G_SHOW ? "listen" : st == G_INPUT ? "your turn" : st == G_OVER ? "enter to play again" : "";
    if (!os::speech().active) ui::hint(g, msg);
  }
} gameApp;

// ============================================================ body =========
struct Body : App {
  const char *title() override { return "Body"; }
  void key(const Key &k) override {
    if (k.code == K_ESC || k.code == K_LEFT) os::open(apps::rooms());
  }
  void row(Gfx &g, int i, const char *label, const String &value) {
    int y = ui::HEAD_ROW + 5 + i * 11;
    g.setTextColor(COL_DIM);
    g.drawString(label, 6, y);
    g.setTextColor(COL_INK);
    g.drawString(value, 78, y);
  }
  void draw(Gfx &g) override {
    ui::background(g);
    ui::header(g, "Body");
    g.setFont(ui::fSmall);
    g.setTextDatum(textdatum_t::top_left);
    char b[48];
    uint32_t up = os::uptimeSec();
    snprintf(b, sizeof(b), "%uh %02um, woken %u times", (unsigned)(up / 3600), (unsigned)((up / 60) % 60), (unsigned)cfg.boots);
    row(g, 0, "awake", b);
    snprintf(b, sizeof(b), "%d%%  %dmV", M5Cardputer.Power.getBatteryLevel(), M5Cardputer.Power.getBatteryVoltage());
    row(g, 1, "energy", b);
    snprintf(b, sizeof(b), "%u free, %u lowest", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
    row(g, 2, "memory", b);
    row(g, 3, "mind", reach::up() ? "whole, on the Mac at " + reach::peer() : String("small: the lines I packed"));
    row(g, 4, "Wi-Fi", reach::wifiState() + (reach::wifiUp() ? "  " + reach::ip() : String()));
    snprintf(b, sizeof(b), "%.0f frames a second", os::fps());
    row(g, 5, "face", b);
    snprintf(b, sizeof(b), "x %+.2f  y %+.2f  z %+.2f", sense::ax, sense::ay, sense::az);
    row(g, 6, "balance", sense::hasImu ? String(b) : String("no motion sensor"));
    row(g, 7, "hearing", cfg.listen ? String("") : String("off"));
    if (cfg.listen) {
      int w = (int)(voice::micLevel() * 140);
      g.drawRect(78, ui::HEAD_ROW + 5 + 7 * 11, 142, 7, COL_FAINT);
      g.fillRect(79, ui::HEAD_ROW + 6 + 7 * 11, w, 5, COL_CORAL);
    }
    row(g, 8, "time", os::timeKnown() ? os::clockText() : String("not told yet"));
  }
} bodyApp;

// ============================================================ settings =====
struct SettingsRoom : App {
  enum { MAIN, SCAN, PASS, NAME } st = MAIN;
  ui::List list, nets;
  ui::Field field;
  String chosen;
  int armedReset = 0;
  enum { I_WIFI, I_LINK, I_VOLUME, I_BRIGHT, I_TURN, I_SPEAK, I_CHATTY, I_LISTEN, I_HEARNAME, I_OWNMIND, I_NAME, I_TEST, I_RESET, I_COUNT };

  const char *title() override { return "Settings"; }
  bool arrowsBare() override { return st == MAIN || st == SCAN; }
  void enter() override {
    st = MAIN;
    armedReset = 0;
  }
  void leave() override { cfg.save(); }

  String label(int i) {
    static const char *n[] = {"Wi-Fi", "Whole mind", "Volume", "Brightness", "Turn the screen", "Speaks aloud", "Talks unprompted", "Hears the room", "Answers to its name", "Alone, it speaks from", "Name", "Hear the voice", "Forget everything"};
    return n[i];
  }
  String value(int i) {
    switch (i) {
      case I_WIFI: return reach::wifiState();
      case I_LINK: return reach::up() ? String("here") : cfg.token.length() ? String("away") : String("not paired");
      case I_VOLUME: return String(cfg.volume);
      case I_BRIGHT: return String(cfg.brightness);
      case I_TURN: return cfg.turn ? String(cfg.turn * 90) + " deg" : String("as built");
      case I_SPEAK: return cfg.speak ? "yes" : "no";
      case I_CHATTY: return cfg.chatty == 0 ? "never" : cfg.chatty == 1 ? "a little" : "often";
      case I_LISTEN: return cfg.listen ? "yes" : "no";
      case I_HEARNAME: return cfg.hearName ? "yes" : "no";
      case I_OWNMIND: return cfg.ownMind ? "its own mind" : "written lines";
      case I_NAME: return cfg.name;
      case I_TEST: return "enter";
      case I_RESET: return armedReset ? "enter again" : "";
    }
    return "";
  }
  void adjust(int i, int d) {
    switch (i) {
      case I_VOLUME: cfg.volume = constrain((int)cfg.volume + d, 0, 10); voice::chime(CH_TICK); break;
      case I_BRIGHT: cfg.brightness = constrain((int)cfg.brightness + d, 1, 10); os::applyBrightness(); break;
      case I_TURN: cfg.turn = (cfg.turn + 4 + d) & 3; os::applyTurn(); break;
      case I_SPEAK: cfg.speak = !cfg.speak; break;
      case I_CHATTY: cfg.chatty = (cfg.chatty + 3 + d) % 3; break;
      case I_LISTEN: cfg.listen = !cfg.listen; break;
      case I_HEARNAME: cfg.hearName = !cfg.hearName; break;
      case I_OWNMIND: cfg.ownMind = !cfg.ownMind; break;
    }
  }
  void key(const Key &k) override {
    if (st == MAIN) {
      if (list.key(k, I_COUNT)) {
        armedReset = 0;
        return;
      }
      int i = list.sel;
      if (k.code == K_ESC) os::open(apps::rooms());
      else if (k.code == K_LEFT) adjust(i, -1);
      else if (k.code == K_RIGHT) adjust(i, +1);
      else if (k.code == K_ENTER) {
        if (i == I_WIFI) {
          st = SCAN;
          nets.sel = nets.top = 0;
          reach::scanStart();
        } else if (i == I_NAME) {
          st = NAME;
          field.text = cfg.name;
          field.maxLen = 8;
        } else if (i == I_TEST) {
          os::sayLine("who_are_you");
        } else if (i == I_LINK) {
          os::say(reach::up() ? "The whole mind is here." : cfg.token.length() ? "The Mac is out of reach. Start my session there."
                                                          : "I am not paired with a Mac yet. That is done over the cable.",
                  CALM, false);
        } else if (i == I_RESET) {
          if (armedReset) {
            cfg.reset();
            reach::wifiRestart();
            armedReset = 0;
          } else armedReset = 1;
        } else adjust(i, +1);
      }
    } else if (st == SCAN) {
      int n = reach::scanCount();
      if (k.code == K_ESC || k.code == K_LEFT) st = MAIN;
      else if (n > 0 && nets.key(k, n + 1)) {
      } else if (k.code == K_ENTER && n >= 0) {
        if (nets.sel >= n) {   // the last row turns Wi-Fi off
          cfg.wifiSsid = "";
          cfg.wifiPass = "";
          cfg.save();
          reach::wifiRestart();
          st = MAIN;
        } else {
          chosen = reach::scanName(nets.sel);
          field.text = "";
          field.maxLen = 63;
          st = PASS;
        }
      }
    } else if (st == PASS) {
      if (k.code == K_ESC) st = SCAN;
      else if (k.code == K_ENTER) {
        cfg.wifiSsid = chosen;
        cfg.wifiPass = field.text;
        cfg.save();
        reach::wifiRestart();
        st = MAIN;
      } else field.key(k);
    } else if (st == NAME) {
      if (k.code == K_ESC) st = MAIN;
      else if (k.code == K_ENTER) {
        field.text.trim();
        if (field.text.length()) cfg.name = field.text;
        cfg.save();
        st = MAIN;
      } else field.key(k);
    }
  }
  void draw(Gfx &g) override {
    ui::background(g);
    if (st == MAIN) {
      ui::header(g, "Settings");
      list.draw(g, ui::HEAD_ROW + 4, 6, I_COUNT, [&](int i) { return label(i); }, [&](int i) { return value(i); });
      ui::hint(g, ", less   / more   enter choose   ` back");
    } else if (st == SCAN) {
      ui::header(g, "Wi-Fi");
      int n = reach::scanCount();
      if (n < 0) {
        g.setFont(ui::fList);
        g.setTextColor(COL_DIM);
        g.drawString("looking for networks...", 10, ui::HEAD_ROW + 8);
      } else {
        nets.draw(g, ui::HEAD_ROW + 4, 6, n + 1, [&](int i) { return i < n ? reach::scanName(i) : String("turn Wi-Fi off"); },
                  [&](int i) { return i < n ? String(reach::scanRssi(i)) : String(); });
      }
      ui::hint(g, "enter choose   ` back");
    } else {
      ui::header(g, st == PASS ? "Wi-Fi password" : "Name");
      g.setFont(ui::fList);
      g.setTextColor(COL_DIM);
      g.drawString(st == PASS ? chosen : String("up to eight letters"), 10, ui::HEAD_ROW + 10);
      g.drawRoundRect(6, 52, SCREEN_W - 12, 22, 4, COL_FAINT);
      field.draw(g, 12, 56, SCREEN_W - 24, COL_INK, true);
      ui::hint(g, "enter keeps it   ` cancels");
    }
  }
} settingsApp;

// ============================================================ card =========
// a screen the whole mind composes; it stays until you send it away
struct CardRoom : App {
  String heading, kind, body;
  std::vector<String> rows;
  int scroll = 0;
  App *back = nullptr;
  const char *title() override { return "Card"; }
  void set(const String &t, const String &k, const String &b) {
    heading = t;
    kind = k;
    body = b;
    scroll = 0;
    rows.clear();
    if (kind == "text") {
      os::canvas().setFont(ui::fBody);
      for (auto &sp : ui::wrap(os::canvas(), body, SCREEN_W - 12)) rows.push_back(body.substring(sp.start, sp.end));
    } else {
      int start = 0;
      while (start <= (int)body.length()) {
        int nl = body.indexOf('\n', start);
        if (nl < 0) nl = body.length();
        String row = body.substring(start, nl);
        row.trim();
        if (row.length()) rows.push_back(row);
        start = nl + 1;
      }
    }
  }
  int perPage() { return kind == "text" ? 8 : kind == "chart" ? 7 : 7; }
  void key(const Key &k) override {
    if (k.code == K_ESC || k.code == K_ENTER) os::open(back && back != this ? back : apps::home());
    else if (k.code == K_DOWN) scroll = min(scroll + 3, max(0, (int)rows.size() - perPage()));
    else if (k.code == K_UP) scroll = max(0, scroll - 3);
  }
  void draw(Gfx &g) override {
    ui::background(g);
    ui::header(g, heading.length() ? heading.c_str() : "Wink", "` closes");
    int y = ui::HEAD_ROW + 4;
    g.setTextDatum(textdatum_t::top_left);
    if (kind == "big" && rows.size()) {
      g.setFont(&fonts::FreeSansBold18pt7b);
      g.setTextColor(COL_INK);
      g.setTextDatum(textdatum_t::middle_center);
      g.drawString(rows[0], SCREEN_W / 2, 58);
      g.setFont(ui::fBody);
      g.setTextColor(COL_DIM);
      for (size_t i = 1; i < rows.size() && i < 4; i++) g.drawString(rows[i], SCREEN_W / 2, 84 + (i - 1) * ui::TEXT_ROW);
      g.setTextDatum(textdatum_t::top_left);
      return;
    }
    if (kind == "chart") {   // rows of "label=value"
      float top = 1;
      for (auto &r : rows) top = max(top, r.substring(r.indexOf('=') + 1).toFloat());
      g.setFont(ui::fSmall);
      for (int i = 0; i < 7 && scroll + i < (int)rows.size(); i++) {
        const String &r = rows[scroll + i];
        int eq = r.indexOf('=');
        String label = eq > 0 ? r.substring(0, eq) : r;
        float v = eq > 0 ? r.substring(eq + 1).toFloat() : 0;
        int ry = y + i * 16;
        g.setTextColor(COL_DIM);
        g.drawString(label.substring(0, 11), 4, ry + 4);
        int w = (int)(112 * v / top);
        for (int x = 0; x < w; x += 4) g.fillRect(72 + x, ry + 2, 3, 11, COL_CORAL);   // bars made of the same dots
        g.setTextColor(COL_INK);
        g.drawString(eq > 0 ? r.substring(eq + 1) : String(), 76 + w, ry + 4);
      }
      return;
    }
    g.setFont(ui::fBody);
    g.setTextColor(COL_INK);
    for (int i = 0; i < perPage() && scroll + i < (int)rows.size(); i++) {
      int ry = y + i * (kind == "list" ? 15 : ui::TEXT_ROW);
      if (kind == "list") {
        g.fillRect(6, ry + 5, 3, 3, COL_CORAL);
        g.drawString(rows[scroll + i], 14, ry);
      } else g.drawString(rows[scroll + i], 6, ry);
    }
    if ((int)rows.size() > perPage()) ui::hint(g, "; up   . down");
  }
} cardApp;

// ============================================================ memory =======
// everything Wink keeps about you, with when and how it got there; del twice strikes a line
struct Memory : App {
  struct Row {
    String id, date, how, text;
  };
  std::vector<Row> rows;
  ui::List list;
  int armed = -1;
  const char *title() override { return "Memory"; }
  void load() {
    rows.clear();
    String struck = "\n";
    File s = LittleFS.open("/struck.txt");
    if (s) struck += s.readString() + "\n";
    File f = LittleFS.open("/memory.txt");
    while (f && f.available() && rows.size() < 80) {
      String line = f.readStringUntil('\n');
      int a = line.indexOf('|'), b = line.indexOf('|', a + 1), c = line.indexOf('|', b + 1);
      if (a < 0 || b < 0 || c < 0) continue;
      Row r = {line.substring(0, a), line.substring(a + 1, b), line.substring(b + 1, c), line.substring(c + 1)};
      if (struck.indexOf("\n" + r.id + "\n") >= 0) continue;
      rows.push_back(r);
    }
  }
  void enter() override {
    armed = -1;
    load();
  }
  void key(const Key &k) override {
    if (list.key(k, rows.size())) {
      armed = -1;
      return;
    }
    if (k.code == K_ESC || k.code == K_LEFT) os::open(apps::rooms());
    else if (k.code == K_DEL && rows.size()) {
      if (armed == list.sel) {
        const String id = rows[list.sel].id;
        File s = LittleFS.open("/struck.txt", FILE_APPEND);
        if (s) {
          s.println(id);
          s.close();
        }
        if (reach::up()) reach::sendLine("{\"t\":\"struck\",\"id\":\"" + id + "\"}");
        armed = -1;
        load();
      } else armed = list.sel;
    }
  }
  void draw(Gfx &g) override {
    ui::background(g);
    char count[20];
    snprintf(count, sizeof(count), "%d kept", (int)rows.size());
    ui::header(g, "Memory", count);
    if (rows.empty()) {
      g.setFont(ui::fBody);
      g.setTextColor(COL_DIM);
      g.drawString("Nothing kept yet.", 8, ui::HEAD_ROW + 8);
      g.drawString("With the Mac in reach, say", 8, ui::HEAD_ROW + 8 + ui::TEXT_ROW * 2);
      g.drawString("\"remember that...\"", 8, ui::HEAD_ROW + 8 + ui::TEXT_ROW * 3);
      return;
    }
    list.draw(g, ui::HEAD_ROW + 4, 5, rows.size(), [&](int i) { return rows[i].text; });
    // the one under the cursor in full, with where it came from
    const Row &r = rows[list.sel];
    g.fillRect(0, SCREEN_H - 34, SCREEN_W, 34, COL_BAND);
    g.drawFastHLine(0, SCREEN_H - 34, SCREEN_W, COL_RULE);
    g.setFont(ui::fSmall);
    g.setTextColor(list.sel == armed ? COL_CORAL : COL_DIM);
    g.drawString(list.sel == armed ? String("del again to strike this line") : r.date + "  " + r.how, 5, SCREEN_H - 31);
    g.setTextColor(COL_INK);
    String t = r.text;
    g.drawString(t.substring(0, 39), 5, SCREEN_H - 21);
    if (t.length() > 39) g.drawString(t.substring(39, 78), 5, SCREEN_H - 11);
  }
} memoryApp;

}  // namespace

App *apps::memory() { return &memoryApp; }
void apps::showCard(const String &title, const String &kind, const String &body) {
  if (os::current() != &cardApp) cardApp.back = os::current();
  cardApp.set(title, kind, body);
  os::open(&cardApp);
}
void apps::setTimer(uint32_t ms, const String &say) {
  timerEndsAt = ms ? millis() + ms : 0;
  if (ms && timerEndsAt == 0) timerEndsAt = 1;
  timerSay = say;
}
int apps::timerLeft() { return timerEndsAt ? (int)((timerEndsAt - millis()) / 1000) : -1; }

App *apps::rooms() { return &roomsApp; }
App *apps::terminal() { return &terminalApp; }
App *apps::notes() { return &notesApp; }
App *apps::files() { return &filesApp; }
App *apps::clockRoom() { return &clockApp; }
App *apps::game() { return &gameApp; }
App *apps::body() { return &bodyApp; }
App *apps::settings() { return &settingsApp; }

// things that must go on whichever room is open
void apps::background() {
  if (timerEndsAt && (int32_t)(millis() - timerEndsAt) >= 0) {
    timerEndsAt = 0;
    os::touched();
    if (face::asleep()) face::setAsleep(false);
    voice::chime(CH_NOTE);
    if (timerSay.length()) os::say(timerSay, HAPPY);
    else os::sayLine("timer.done");
    timerSay = "";
    voice::chime(CH_NOTE);
  }
}
