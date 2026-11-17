// The command language. The Terminal room and the USB cable both speak it.
#include "kernel.h"
#include "face.h"
#include "link.h"
#include "mind.h"
#include "voice.h"
#include "think.h"
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>

namespace {

bool sdReady = false, sdTried = false;

bool sd() {
  if (sdTried) return sdReady;
  sdTried = true;
  SPI.begin(40, 39, 14, 12);
  sdReady = SD.begin(12, SPI, 25000000);
  return sdReady;
}

String word(const String &line, int n) {   // nth space-separated word
  int i = 0, len = line.length();
  for (int w = 0;; w++) {
    while (i < len && line[i] == ' ') i++;
    int start = i;
    while (i < len && line[i] != ' ') i++;
    if (w == n) return line.substring(start, i);
    if (i >= len) return "";
  }
}
String rest(const String &line, int n) {   // everything from the nth word on
  int i = 0, len = line.length();
  for (int w = 0; w < n; w++) {
    while (i < len && line[i] == ' ') i++;
    while (i < len && line[i] != ' ') i++;
  }
  while (i < len && line[i] == ' ') i++;
  return line.substring(i);
}

void listDir(fs::FS &fs, const String &path, Print &out) {
  File dir = fs.open(path.length() ? path : "/");
  if (!dir || !dir.isDirectory()) {
    out.println("no such folder");
    return;
  }
  int n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) out.printf("  %s/\n", f.name());
    else out.printf("  %-22s %u\n", f.name(), (unsigned)f.size());
    n++;
  }
  if (!n) out.println("  (empty)");
}
}  // namespace

// "/sd/..." is the card, everything else the chip's own storage
fs::FS *pathFs(String &path) {
  if (path.startsWith("/sd")) {
    if (!sd()) return nullptr;
    path = path.substring(3);
    if (!path.length()) path = "/";
    return &SD;
  }
  if (!path.startsWith("/")) path = "/" + path;
  return &LittleFS;
}

void shell::exec(const String &raw, Print &out) {
  String line = raw;
  line.trim();
  String cmd = word(line, 0);
  cmd.toLowerCase();
  String a1 = word(line, 1), a2 = word(line, 2);

  if (cmd == "help" || cmd == "?") {
    out.println("talk    say <words> | ask <words> | mood <name> | hush");
    out.println("files   ls [path] | cat <file> | write <file> <text> | rm <file> | df");
    out.println("        (paths under /sd are the card)");
    out.println("net     wifi | wifi scan | wifi set <name> <password> | wifi off");
    out.println("        link | link token <t> | link host <addr> | link forget");
    out.println("self    sys | time [HH:MM] | set <what> <value> | reboot");
    out.println("        set: name volume bright speed pitch chatty speak listen");
  } else if (cmd == "sys" || cmd == "body") {
    out.printf("%s, version %s, woken %u times\n", cfg.name.c_str(), OS_VERSION, (unsigned)cfg.boots);
    out.printf("board %d  chip %s  flash %uMB\n", (int)M5.getBoard(), ESP.getChipModel(), (unsigned)(ESP.getFlashChipSize() >> 20));
    out.printf("memory free %u  lowest %u  largest %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    out.printf("battery %d%%  %dmV  charging %d\n", M5Cardputer.Power.getBatteryLevel(), M5Cardputer.Power.getBatteryVoltage(), (int)M5Cardputer.Power.isCharging());
    out.printf("up %us  idle %us  %.0f frames/s\n", (unsigned)os::uptimeSec(), (unsigned)(os::idleMs() / 1000), os::fps());
    out.printf("motion %d  x %.2f y %.2f z %.2f\n", (int)sense::hasImu, sense::ax, sense::ay, sense::az);
    out.printf("room level %.2f  mood %s  whole mind %d  recordings %d\n", voice::micLevel(), moodName(face::mood()), (int)reach::up(), voice::packCount());
    out.printf("wifi %s %s  mac %s\n", reach::wifiState().c_str(), reach::ip().c_str(), reach::peer().c_str());
  } else if (cmd == "say") {
    os::say(rest(line, 1), face::mood());
  } else if (cmd == "ask") {
    os::ask(rest(line, 1), "keys");
  } else if (cmd == "line") {
    if (mind::has(a1.c_str())) os::sayLine(a1.c_str());
    else out.println("no such situation");
  } else if (cmd == "speakable") {
    out.println(voice::speakable(rest(line, 1)));
  } else if (cmd == "voice") {
    if (a1 == "reload") {
      voice::packReload();
      delay(600);
    }
    out.printf("%d recordings on the card%s; last spoke from: %s\n", voice::packCount(), voice::packCount() ? "" : " (the chip's own voice will be used)", voice::lastSource());
  } else if (cmd == "think") {
    // the mind of its own: think hear <words> | think event <name> | think muse | think (facts)
    if (a1 == "hear") {
      think::hear(rest(line, 2));
      think::reply();
    } else if (a1 == "event") {
      think::event(a2.c_str());
      think::reply();
    } else if (a1 == "muse") think::muse();
    if (a1.length()) {
      uint32_t end = millis() + 8000;
      think::Result r;
      while (millis() < end) {
        if (think::poll(r)) {
          out.printf("[%s] %s\n", moodName(r.mood), r.text.c_str());
          break;
        }
        delay(10);
      }
    }
    out.println(think::about());
    out.printf("memory free %u lowest %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  } else if (cmd == "ears") {
    out.println(voice::debug(a1.length() ? a1.toInt() : -1));
    out.printf("side button presses seen: %u\n", (unsigned)sense::buttonPresses);
  } else if (cmd == "turn") {
    if (a1.length()) {
      cfg.turn = a1.toInt() & 3;
      cfg.save();
      os::applyTurn();
    }
    out.printf("screen %dx%d, rotation %d, extra quarter turns %d\n", M5Cardputer.Display.width(), M5Cardputer.Display.height(), M5Cardputer.Display.getRotation(), cfg.turn);
  } else if (cmd == "hum") {   // hum a line, in the mood it wears
    os::hum(rest(line, 1), face::mood());
  } else if (cmd == "own") {   // answer from its own mind, even with the Mac in reach (for testing)
    os::askOwn(rest(line, 1));
  } else if (cmd == "hush") {
    os::quiet();
  } else if (cmd == "mood") {
    if (!a1.length()) {
      out.print("moods:");
      for (int i = 0; i < MOOD_COUNT; i++) out.printf(" %s", moodName((Mood)i));
      out.println();
    } else if (a1 == "asleep") face::setAsleep(true);
    else if (a1 == "awake") face::setAsleep(false);
    else if (a1 == "whole") face::setWhole(true);
    else if (a1 == "small") face::setWhole(false);
    else if (a1 == "thinking!") face::setThinking(a2 != "off");
    else if (a1 == "listening!") face::setListening(a2 != "off");
    else if (a1 == "wake") face::wake();
    else face::setMood(moodFromName(a1.c_str()));
  } else if (cmd == "chime") {
    voice::chime((Chime)a1.toInt());
  } else if (cmd == "ls") {
    String p = a1.length() ? a1 : "/";
    fs::FS *fs = pathFs(p);
    if (!fs) out.println("no card");
    else listDir(*fs, p, out);
  } else if (cmd == "cat") {
    String p = a1;
    fs::FS *fs = pathFs(p);
    File f = fs ? fs->open(p) : File();
    if (!f || f.isDirectory()) out.println("no such file");
    else {
      int n = 0;
      while (f.available() && n++ < 4000) out.write(f.read());
      out.println();
    }
  } else if (cmd == "write") {
    String p = a1;
    fs::FS *fs = pathFs(p);
    File f = fs ? fs->open(p, FILE_APPEND) : File();
    if (!f) out.println("could not open");
    else {
      f.println(rest(line, 2));
      f.close();
      out.println("written");
    }
  } else if (cmd == "rm") {
    String p = a1;
    fs::FS *fs = pathFs(p);
    out.println(fs && fs->remove(p) ? "gone" : "could not remove");
  } else if (cmd == "df") {
    out.printf("chip  %u of %u bytes used\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    if (sd()) out.printf("card  %lluMB of %lluMB used\n", SD.usedBytes() >> 20, SD.totalBytes() >> 20);
    else out.println("card  none");
  } else if (cmd == "wifi") {
    if (a1 == "set") {
      cfg.wifiSsid = a2;
      cfg.wifiPass = rest(line, 3);
      cfg.save();
      reach::wifiRestart();
      out.printf("joining %s\n", a2.c_str());
    } else if (a1 == "off") {
      cfg.wifiSsid = "";
      cfg.wifiPass = "";
      cfg.save();
      reach::wifiRestart();
      out.println("wifi off");
    } else if (a1 == "scan") {
      reach::scanStart();
      uint32_t end = millis() + 9000;
      while (reach::scanCount() < 0 && millis() < end) delay(50);
      for (int i = 0; i < reach::scanCount(); i++) out.printf("  %-24s %d\n", reach::scanName(i).c_str(), reach::scanRssi(i));
    } else {
      out.printf("%s %s %ddBm\n", reach::wifiState().c_str(), reach::ip().c_str(), reach::rssi());
    }
  } else if (cmd == "link") {
    if (a1 == "token") {
      cfg.token = a2;
      cfg.save();
      out.println("token kept");
    } else if (a1 == "host") {
      int colon = a2.indexOf(':');
      cfg.host = colon > 0 ? a2.substring(0, colon) : a2;
      if (colon > 0) cfg.port = a2.substring(colon + 1).toInt();
      cfg.save();
      reach::wifiRestart();
      out.println("host kept");
    } else if (a1 == "forget") {
      cfg.token = "";
      cfg.host = "";
      cfg.save();
      reach::wifiRestart();
      out.println("forgotten");
    } else {
      out.printf("whole mind %s %s  token %s  host %s\n", reach::up() ? "here" : "away", reach::peer().c_str(),
                 cfg.token.length() ? "set" : "none", cfg.host.length() ? cfg.host.c_str() : "(found by name)");
    }
  } else if (cmd == "time") {
    int colon = a1.indexOf(':');
    if (colon > 0) os::setClock(a1.substring(0, colon).toInt(), a1.substring(colon + 1).toInt());
    out.println(os::timeKnown() ? os::clockText() : String("not told the time yet"));
  } else if (cmd == "set") {
    int v = a2.toInt();
    if (a1 == "name" && a2.length()) cfg.name = a2.substring(0, 8);
    else if (a1 == "volume") cfg.volume = constrain(v, 0, 10);
    else if (a1 == "bright") cfg.brightness = constrain(v, 1, 10);
    else if (a1 == "speed") cfg.voiceSpeed = constrain(v, 40, 140);
    else if (a1 == "pitch") cfg.voicePitch = constrain(v, 20, 120);
    else if (a1 == "chatty") cfg.chatty = constrain(v, 0, 2);
    else if (a1 == "speak") cfg.speak = v != 0 || a2 == "on";
    else if (a1 == "listen") cfg.listen = v != 0 || a2 == "on";
    else {
      out.println("set what? name volume bright speed pitch chatty speak listen");
      return;
    }
    cfg.save();
    os::applyBrightness();
    out.println("kept");
  } else if (cmd == "room") {
    App *a = a1 == "home" ? apps::home() : a1 == "rooms" ? apps::rooms() : a1 == "terminal" ? apps::terminal()
             : a1 == "notes" ? apps::notes() : a1 == "files" ? apps::files() : a1 == "clock" ? apps::clockRoom()
             : a1 == "game" ? apps::game() : a1 == "body" ? apps::body() : a1 == "settings" ? apps::settings()
             : a1 == "memory" ? apps::memory() : nullptr;
    if (a) os::open(a);
    else out.println("rooms: home rooms terminal notes files clock game memory body settings");
  } else if (cmd == "reboot") {
    out.println("back in a moment");
    delay(200);
    ESP.restart();
  } else if (cmd == "forget-everything") {
    cfg.reset();
    out.println("settings cleared");
  } else if (cmd.length()) {
    out.printf("no such command: %s (try help)\n", cmd.c_str());
  }
}
