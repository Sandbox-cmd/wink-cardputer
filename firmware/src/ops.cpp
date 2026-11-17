// What the whole mind can do with its body. Each request from the Mac is one small
// act: set a timer, open a room, read or add to a file, put a card on the screen.
// It can make and add to files. It cannot delete or overwrite yours.
#include "kernel.h"
#include "face.h"
#include "link.h"
#include "voice.h"
#include "think.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

fs::FS *pathFs(String &path);   // shell.cpp

namespace {
const char *lastEvent = "";
uint32_t lastEventAt = 0, lastMovedAt = 0, lastSaidAt = 0;

App *roomByName(const String &n) {
  return n == "talk" || n == "home" ? apps::home() : n == "rooms" ? apps::rooms() : n == "terminal" ? apps::terminal()
       : n == "notes" ? apps::notes() : n == "files" ? apps::files() : n == "clock" ? apps::clockRoom()
       : n == "echo" || n == "game" ? apps::game() : n == "body" ? apps::body() : n == "settings" ? apps::settings()
       : n == "memory" ? apps::memory() : nullptr;
}
}  // namespace

void ops::noteEvent(const char *name) {
  lastEvent = name;
  lastEventAt = millis();
  // its own mind feels it too
  think::event(!strcmp(name, "picked up") ? "pickup" : !strcmp(name, "shaken") ? "shake" : !strcmp(name, "laid face down") ? "facedown" : "loud");
}
void ops::noteMoved() { lastMovedAt = millis(); }
void ops::noteSaid() { lastSaidAt = millis(); }

// what is going on around the device, for the whole mind to know before it answers
void ops::situation(JsonDocument &d) {
  uint32_t now = millis();
  d["room"] = os::current() ? os::current()->title() : "";
  d["clock"] = os::clockText();
  d["battery"] = M5Cardputer.Power.getBatteryLevel();
  d["charging"] = (int)M5Cardputer.Power.isCharging() == 1;
  d["in_hand"] = lastMovedAt && now - lastMovedAt < 8000;
  d["quiet_before_s"] = lastSaidAt ? (now - lastSaidAt) / 1000 : os::uptimeSec();
  d["awake_s"] = os::uptimeSec();
  if (lastEventAt && now - lastEventAt < 600000) {
    d["last_event"] = lastEvent;
    d["last_event_s"] = (now - lastEventAt) / 1000;
  }
  int left = apps::timerLeft();
  if (left >= 0) d["timer_left_s"] = left;
  d["room_sound"] = (int)(voice::micLevel() * 100);
}

String ops::run(const String &line) {
  JsonDocument d, r;
  r["t"] = "done";
  if (deserializeJson(d, line)) {
    r["ok"] = false;
    r["error"] = "could not read the request";
    String out;
    serializeJson(r, out);
    return out;
  }
  r["id"] = d["id"] | 0;
  String op = d["op"] | "";
  String err;
  os::touched();
  if (face::asleep()) face::setAsleep(false);

  if (op == "timer") {
    float minutes = d["minutes"] | 0.0f;
    if (minutes <= 0 || minutes > 24 * 60) err = "minutes must be between 0 and 1440";
    else apps::setTimer((uint32_t)(minutes * 60000.0f), d["say"] | "");
  } else if (op == "timer_cancel") {
    apps::setTimer(0, "");
  } else if (op == "room") {
    App *a = roomByName(d["name"] | "");
    if (a) os::open(a);
    else err = "no such room";
  } else if (op == "set") {
    String key = d["key"] | "";
    int v = d["value"] | 0;
    if (key == "volume") cfg.volume = constrain(v, 0, 10);
    else if (key == "brightness") cfg.brightness = constrain(v, 1, 10);
    else if (key == "talks_unprompted") cfg.chatty = constrain(v, 0, 2);
    else if (key == "speaks_aloud") cfg.speak = v != 0;
    else if (key == "hears_the_room") cfg.listen = v != 0;
    else if (key == "answers_to_name") cfg.hearName = v != 0;
    else err = "no such setting";
    if (!err.length()) {
      cfg.save();
      os::applyBrightness();
    }
  } else if (op == "card") {
    apps::showCard(d["title"] | "", d["kind"] | "text", d["body"] | "");
  } else if (op == "ls") {
    String p = d["path"] | "/";
    fs::FS *fs = pathFs(p);
    File dir = fs ? fs->open(p.length() ? p : "/") : File();
    if (!dir || !dir.isDirectory()) err = "no such folder";
    else {
      JsonArray arr = r["entries"].to<JsonArray>();
      int n = 0;
      for (File f = dir.openNextFile(); f && n < 40; f = dir.openNextFile(), n++) {
        JsonObject e = arr.add<JsonObject>();
        e["name"] = String(f.name()) + (f.isDirectory() ? "/" : "");
        if (!f.isDirectory()) e["bytes"] = f.size();
      }
    }
  } else if (op == "notes") {
    JsonArray arr = r["notes"].to<JsonArray>();
    File dir = LittleFS.open("/notes");
    int n = 0;
    for (File f = dir.openNextFile(); f && n < 30; f = dir.openNextFile(), n++) {
      if (f.isDirectory()) continue;
      JsonObject e = arr.add<JsonObject>();
      e["file"] = String("/notes/") + f.name();
      String first = f.readStringUntil('\n');
      first.trim();
      e["first_line"] = first.substring(0, 60);
      e["bytes"] = f.size();
    }
  } else if (op == "read") {
    String p = d["path"] | "";
    fs::FS *fs = pathFs(p);
    File f = fs ? fs->open(p) : File();
    if (!f || f.isDirectory()) err = "no such file";
    else {
      String text;
      while (f.available() && text.length() < 2400) {
        char c = f.read();
        if (c == '\n' || (c >= 32 && c < 127)) text += c;
      }
      r["text"] = text;
      r["more"] = f.available() > 0;
    }
  } else if (op == "append" || op == "create") {
    String p = d["path"] | "";
    String text = d["text"] | "";
    if (op == "create" && !p.length()) {   // a new note, numbered after the ones that exist
      for (int i = 1; i < 500; i++) {
        p = "/notes/" + String(i) + ".txt";
        if (!LittleFS.exists(p)) break;
      }
    }
    String shown = p;
    fs::FS *fs = pathFs(p);
    if (!fs) err = "no card";
    else if (op == "create" && fs->exists(p)) err = "that file exists already; it can be added to, not replaced";
    else {
      File f = fs->open(p, op == "create" ? FILE_WRITE : FILE_APPEND);
      if (!f) err = "could not open the file";
      else {
        if (op == "append" && f.size() > 0) f.print('\n');
        f.print(text);
        f.close();
        r["path"] = shown;
      }
    }
  } else if (op == "state") {
    ops::situation(r);
  } else {
    err = "no such act";
  }
  r["ok"] = !err.length();
  if (err.length()) r["error"] = err;
  String out;
  serializeJson(r, out);
  return out;
}
