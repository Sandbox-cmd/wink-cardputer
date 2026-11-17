// The creature's reach: Wi-Fi, and through it the whole mind on the Mac.
#pragma once
#include "os.h"

namespace reach {
enum Ev : uint8_t { E_NONE, E_WIFI_UP, E_WIFI_FAIL, E_WIFI_DOWN, E_LINK_UP, E_LINK_DOWN,
                    E_REPLY, E_SHOW, E_MOOD, E_HEARD, E_ACK, E_LOOK, E_EMPTY, E_CHIME, E_DO };
struct Event {
  Ev type = E_NONE;
  String text;
  Mood mood = CALM;
  bool aloud = true;
  uint32_t stream = 0, bytes = 0;   // sound for this reply is arriving as stream number `stream`
};
void begin();
bool poll(Event &e);                 // main loop drains what happened
bool wifiUp();
bool up();                           // the whole mind is reachable
void wifiRestart();                  // after the network name or password changed
void send(const String &text, const char *via);   // something you said, by "keys" or "voice"
void sendLine(const String &json);   // a line to the Mac, when in reach
void pttBegin();
void pttEnd();
void sendShot(const uint8_t *buf, size_t n, int w, int h);
String wifiState();                  // "off", "joining Home", "on Home", "could not join Home"
String ip();
int rssi();
String peer();                       // address of the Mac when linked

// scanning for networks happens in the background
void scanStart();
int scanCount();                     // -1 while scanning
String scanName(int i);
int scanRssi(int i);
}
