// Settings kept on the chip across power-offs.
#include "os.h"
#include <Preferences.h>

Settings cfg;
static Preferences prefs;

static const char *MOOD_NAMES[MOOD_COUNT] = {"calm", "happy", "delighted", "curious", "thinking", "sleepy",
                                             "surprised", "sad", "cheeky", "tender", "alert"};
const char *moodName(Mood m) { return m < MOOD_COUNT ? MOOD_NAMES[m] : "calm"; }
Mood moodFromName(const char *s, Mood fallback) {
  if (!s) return fallback;
  for (int i = 0; i < MOOD_COUNT; i++)
    if (!strcasecmp(s, MOOD_NAMES[i])) return (Mood)i;
  return fallback;
}

void Settings::load() {
  prefs.begin("os", true);
  name = prefs.getString("name", "Wink");
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  token = prefs.getString("token", "");
  host = prefs.getString("host", "");
  port = prefs.getUShort("port", 8765);
  volume = prefs.getUChar("vol", 7);
  brightness = prefs.getUChar("bright", 7);
  voiceSpeed = prefs.getUChar("vspeed", 72);
  voicePitch = prefs.getUChar("vpitch", 64);
  chatty = prefs.getUChar("chatty", 1);
  turn = prefs.getUChar("turn", 0);
  speak = prefs.getBool("speak", true);
  listen = prefs.getBool("listen", true);
  hearName = prefs.getBool("hearname", true);
  ownMind = prefs.getBool("ownmind", true);
  boots = prefs.getUInt("boots", 0);
  prefs.end();
}

void Settings::save() {
  prefs.begin("os", false);
  prefs.putString("name", name);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putString("token", token);
  prefs.putString("host", host);
  prefs.putUShort("port", port);
  prefs.putUChar("vol", volume);
  prefs.putUChar("bright", brightness);
  prefs.putUChar("vspeed", voiceSpeed);
  prefs.putUChar("vpitch", voicePitch);
  prefs.putUChar("chatty", chatty);
  prefs.putUChar("turn", turn);
  prefs.putBool("speak", speak);
  prefs.putBool("listen", listen);
  prefs.putBool("hearname", hearName);
  prefs.putBool("ownmind", ownMind);
  prefs.putUInt("boots", boots);
  prefs.end();
}

void Settings::reset() {
  prefs.begin("os", false);
  prefs.clear();
  prefs.end();
  load();
}
