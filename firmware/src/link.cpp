// Wi-Fi and the line to the Mac. One background task owns the radio and the socket.
// The device speaks only to the Mac that holds its token, on the local network.
//
// The line is text: one JSON object per line. Sound and screenshots follow a header
// line that says how many raw bytes come next.
#include "link.h"
#include "kernel.h"
#include "face.h"
#include "voice.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>

fs::FS *pathFs(String &path);   // shell.cpp

namespace {

QueueHandle_t evQ, outQ;
SemaphoreHandle_t sockLock;
WiFiClient client;

enum State : uint8_t { S_IDLE, S_JOINING, S_ONLINE, S_RETRY };
volatile State state = S_IDLE;
volatile bool wifiOk = false, linkOk = false, restart = false;
volatile bool pttWant = false, pttSending = false;
volatile uint32_t pttEndAt = 0;
volatile bool scanAsk = false;
volatile int scanN = 0;
String scanNames[12];
int scanRssis[12];
String peerAddr;
bool failedOnce = false;

volatile uint32_t audioLeft = 0;   // bytes of sound still to come before the next line of text

void post(reach::Ev type, const String &text = String(), Mood mood = CALM, bool aloud = true, uint32_t stream = 0, uint32_t bytes = 0) {
  auto *e = new reach::Event;
  e->type = type;
  e->text = text;
  e->mood = mood;
  e->aloud = aloud;
  e->stream = stream;
  e->bytes = bytes;
  if (xQueueSend(evQ, &e, 0) != pdTRUE) delete e;
}

void sendLine(const String &s) {
  xSemaphoreTake(sockLock, portMAX_DELAY);
  client.print(s);
  client.print('\n');
  xSemaphoreGive(sockLock);
}

void queueLine(const String &s) {
  auto *p = new String(s);
  if (xQueueSend(outQ, &p, 0) != pdTRUE) delete p;
}

// a file sent by the Mac: recordings for the card, and the two lists it keeps in step
File blobFile;
String blobPath;
volatile uint32_t blobLeft = 0;

void endBlob(bool ok) {
  if (blobFile) blobFile.close();
  queueLine(String("{\"t\":\"blobbed\",\"ok\":") + (ok ? "true" : "false") + ",\"path\":\"" + blobPath + "\"}");
  if (ok && blobPath == "/memory.txt") LittleFS.remove("/struck.txt");   // the Mac has heard about every strike
}

void beginBlob(const char *path, uint32_t n) {
  blobPath = path;
  blobLeft = n;
  bool allowed = blobPath.startsWith("/sd/wink/") || blobPath == "/memory.txt" || blobPath == "/learned.tsv";
  String p = blobPath;
  fs::FS *fs = allowed ? pathFs(p) : nullptr;
  if (fs) {
    int slash = p.lastIndexOf('/');
    if (slash > 0 && !fs->exists(p.substring(0, slash))) fs->mkdir(p.substring(0, slash));
    blobFile = fs->open(p, FILE_WRITE);
  }
  if (!n) endBlob((bool)blobFile);
}

String statusJson() {
  JsonDocument d;
  d["t"] = "status";
  d["name"] = cfg.name;
  d["battery"] = M5Cardputer.Power.getBatteryLevel();
  d["charging"] = (int)M5Cardputer.Power.isCharging() == 1;
  d["mood"] = moodName(face::mood());
  d["asleep"] = face::asleep();
  d["room"] = os::current() ? os::current()->title() : "";
  d["uptime_s"] = os::uptimeSec();
  d["idle_s"] = os::idleMs() / 1000;
  d["heap"] = ESP.getFreeHeap();
  d["rssi"] = WiFi.RSSI();
  d["volume"] = cfg.volume;
  d["speaks"] = cfg.speak;
  d["boots"] = cfg.boots;
  d["fw"] = OS_VERSION;
  String out;
  serializeJson(d, out);
  return out;
}

void dropLink() {
  client.stop();
  audioLeft = 0;
  if (blobLeft) {
    blobLeft = 0;
    if (blobFile) blobFile.close();
  }
  if (linkOk) {
    linkOk = false;
    pttSending = false;
    post(reach::E_LINK_DOWN);
  }
}

// plain letters only: the screen font and the voice know nothing else
String plain(const char *s) {
  String out;
  if (!s) return out;
  for (; *s; s++) {
    unsigned char c = *s;
    if (c == '\n' || (c >= 32 && c < 127)) out += (char)c;
  }
  return out;
}

void handleLine(const String &line) {
  JsonDocument d;
  if (deserializeJson(d, line)) return;
  const char *t = d["t"] | "";
  if (!strcmp(t, "say")) {
    uint32_t bytes = d["audio"] | 0;
    uint32_t id = bytes ? voice::streamOpen() : 0;
    audioLeft = bytes;
    post(reach::E_REPLY, plain(d["text"] | ""), moodFromName(d["mood"] | "calm"), d["aloud"] | true, id, bytes);
  } else if (!strcmp(t, "mood")) {
    post(reach::E_MOOD, String(), moodFromName(d["mood"] | "calm"));
  } else if (!strcmp(t, "heard")) {
    String text = plain(d["text"] | "");
    post(text.length() ? reach::E_HEARD : reach::E_EMPTY, text, CALM, d["vox"] | false);   // aloud here means: it was called by name
  } else if (!strcmp(t, "ack")) {
    post(reach::E_ACK);
  } else if (!strcmp(t, "look")) {
    post(reach::E_LOOK);
  } else if (!strcmp(t, "chime")) {
    post(reach::E_CHIME);
  } else if (!strcmp(t, "ping")) {
    queueLine("{\"t\":\"pong\"}");
  } else if (!strcmp(t, "status")) {
    queueLine(statusJson());
  } else if (!strcmp(t, "do")) {
    post(reach::E_DO, line);
  } else if (!strcmp(t, "blob")) {
    beginBlob(d["path"] | "", d["n"] | 0);
  } else if (!strcmp(t, "time")) {
    os::setEpoch(d["epoch"] | 0, d["tz"] | 0);
  }
}

bool readLine(String &out, uint32_t timeoutMs) {
  uint32_t end = millis() + timeoutMs;
  out = "";
  while ((int32_t)(end - millis()) > 0) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') return true;
      if (out.length() < 4096) out += c;
    }
    if (!client.connected()) return false;
    vTaskDelay(5);
  }
  return false;
}

void tryLink() {
  IPAddress ip;
  uint16_t port = cfg.port;
  if (cfg.host.length()) {
    if (!ip.fromString(cfg.host) && !WiFi.hostByName(cfg.host.c_str(), ip)) return;
  } else {
    int n = MDNS.queryService("winkmind", "tcp");
    if (n <= 0) return;
    ip = MDNS.IP(0);
    port = MDNS.port(0);
  }
  if (!client.connect(ip, port, 2500)) return;
  client.setNoDelay(true);
  JsonDocument hello;
  hello["t"] = "hello";
  hello["token"] = cfg.token;
  hello["name"] = cfg.name;
  hello["fw"] = OS_VERSION;
  File pendingStrikes = LittleFS.open("/struck.txt");   // lines struck while the Mac was out of reach
  if (pendingStrikes) {
    JsonArray ids = hello["struck"].to<JsonArray>();
    while (pendingStrikes.available() && ids.size() < 40) {
      String id = pendingStrikes.readStringUntil('\n');
      id.trim();
      if (id.length()) ids.add(id);
    }
    pendingStrikes.close();
  }
  String h;
  serializeJson(hello, h);
  sendLine(h);
  String line;
  if (!readLine(line, 4000)) {
    client.stop();
    return;
  }
  JsonDocument w;
  if (deserializeJson(w, line) || strcmp(w["t"] | "", "welcome")) {
    client.stop();
    return;
  }
  uint32_t epoch = w["epoch"] | 0;
  if (epoch) os::setEpoch(epoch, w["tz"] | 0);
  peerAddr = ip.toString() + ":" + String(port);
  linkOk = true;
  post(reach::E_LINK_UP);
}

void serviceLink() {
  static String rx;
  static uint32_t lastRx = 0, lastPing = 0, lastStatus = 0;
  static bool fresh = true;
  uint32_t now = millis();
  if (fresh) {
    lastRx = lastPing = now;
    lastStatus = 0;
    rx = "";
    fresh = false;
  }
  while (client.available()) {
    if (blobLeft) {
      static uint8_t bbuf[1024];
      int n = client.read(bbuf, min((size_t)blobLeft, sizeof(bbuf)));
      if (n <= 0) break;
      if (blobFile) blobFile.write(bbuf, n);
      blobLeft -= n;
      lastRx = now;
      if (!blobLeft) endBlob((bool)blobFile);
      continue;
    }
    if (audioLeft) {   // sound: hand it to the voice as fast as it can take it
      static uint8_t abuf[1024];
      size_t room = voice::streamSpace();
      if (!room) break;
      int n = client.read(abuf, min((size_t)audioLeft, min(room, sizeof(abuf))));
      if (n <= 0) break;
      voice::streamFeed(abuf, n);
      audioLeft -= n;
      lastRx = now;
      continue;
    }
    char c = client.read();
    lastRx = now;
    if (c == '\n') {
      handleLine(rx);
      rx = "";
    } else if (rx.length() < 4096) rx += c;
  }
  if (audioLeft) lastRx = now;   // sound waiting its turn is not silence
  if (!client.connected() || now - lastRx > 20000) {
    dropLink();
    fresh = true;
    return;
  }
  String *s;
  while (xQueueReceive(outQ, &s, 0) == pdTRUE) {
    sendLine(*s);
    delete s;
  }

  // your voice: while the button is held, or when it heard someone speak
  static bool sendingVox = false;
  if (!pttSending && (pttWant || voice::voxActive())) {
    sendingVox = !pttWant;
    sendLine(sendingVox ? "{\"t\":\"ptt\",\"on\":true,\"vox\":true,\"rate\":16000}" : "{\"t\":\"ptt\",\"on\":true,\"rate\":16000}");
    pttSending = true;
  }
  if (pttSending) {
    static uint8_t buf[2048];
    size_t n;
    while ((n = voice::pttRead(buf, sizeof(buf))) > 0) {
      xSemaphoreTake(sockLock, portMAX_DELAY);
      client.printf("{\"t\":\"audio\",\"n\":%u}\n", (unsigned)n);
      client.write(buf, n);
      xSemaphoreGive(sockLock);
    }
    bool over = sendingVox ? !voice::voxActive() : (!pttWant && now > pttEndAt);
    if (over) {
      if (!sendingVox) voice::pttStop();
      while ((n = voice::pttRead(buf, sizeof(buf))) > 0) {
        xSemaphoreTake(sockLock, portMAX_DELAY);
        client.printf("{\"t\":\"audio\",\"n\":%u}\n", (unsigned)n);
        client.write(buf, n);
        xSemaphoreGive(sockLock);
      }
      JsonDocument end;
      end["t"] = "ptt";
      end["on"] = false;
      ops::situation(end);
      String endLine;
      serializeJson(end, endLine);
      sendLine(endLine);
      pttSending = false;
    }
  }
  if (now - lastPing > 5000) {
    lastPing = now;
    sendLine("{\"t\":\"ping\"}");
  }
  if (now - lastStatus > 30000) {
    lastStatus = now;
    sendLine(statusJson());
  }
}

void doScan() {
  scanN = -1;
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  int kept = 0;
  for (int i = 0; i < n && kept < 12; i++) {
    String name = WiFi.SSID(i);
    if (!name.length()) continue;
    bool dup = false;
    for (int k = 0; k < kept; k++)
      if (scanNames[k] == name) dup = true;
    if (dup) continue;
    scanNames[kept] = name;
    scanRssis[kept] = WiFi.RSSI(i);
    kept++;
  }
  WiFi.scanDelete();
  scanN = kept;
}

void task(void *) {
  uint32_t joinStart = 0, retryAt = 0, nextTry = 0;
  for (;;) {
    uint32_t now = millis();
    if (restart) {
      restart = false;
      dropLink();
      WiFi.disconnect(true);
      wifiOk = false;
      failedOnce = false;
      state = S_IDLE;
      vTaskDelay(200);
    }
    if (scanAsk) {
      scanAsk = false;
      doScan();
    }
    switch (state) {
      case S_IDLE:
        if (cfg.wifiSsid.length()) {
          WiFi.mode(WIFI_STA);
          WiFi.setHostname("wink-cardputer");
          WiFi.setAutoReconnect(true);
          WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
          joinStart = now;
          state = S_JOINING;
        }
        break;
      case S_JOINING:
        if (WiFi.status() == WL_CONNECTED) {
          wifiOk = true;
          failedOnce = false;
          MDNS.begin("wink-cardputer");
          post(reach::E_WIFI_UP);
          state = S_ONLINE;
          nextTry = now + 300;
        } else if (now - joinStart > 20000) {
          if (!failedOnce) post(reach::E_WIFI_FAIL);
          failedOnce = true;
          WiFi.disconnect(true);
          retryAt = now + 45000;
          state = S_RETRY;
        }
        break;
      case S_RETRY:
        if (now > retryAt) state = S_IDLE;
        break;
      case S_ONLINE:
        if (WiFi.status() != WL_CONNECTED) {
          wifiOk = false;
          dropLink();
          MDNS.end();
          post(reach::E_WIFI_DOWN);
          WiFi.disconnect(true);
          retryAt = now + 5000;
          state = S_RETRY;
        } else if (!linkOk) {
          if (cfg.token.length() && now > nextTry) {
            tryLink();
            nextTry = millis() + 6000;
          }
        } else {
          serviceLink();
        }
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(linkOk ? 8 : 60));
  }
}
}  // namespace

void reach::begin() {
  evQ = xQueueCreate(16, sizeof(Event *));
  outQ = xQueueCreate(16, sizeof(String *));
  sockLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "link", 8192, nullptr, 2, nullptr, 0);
}

bool reach::poll(Event &e) {
  Event *p;
  if (xQueueReceive(evQ, &p, 0) != pdTRUE) return false;
  e = *p;
  delete p;
  return true;
}

bool reach::wifiUp() { return wifiOk; }
bool reach::up() { return linkOk; }
void reach::wifiRestart() { restart = true; }

void reach::send(const String &text, const char *via) {
  JsonDocument d;
  d["t"] = "said";
  d["text"] = text;
  d["via"] = via;
  ops::situation(d);
  ops::noteSaid();
  String out;
  serializeJson(d, out);
  queueLine(out);
}

void reach::sendLine(const String &json) {
  if (linkOk) queueLine(json);
}

void reach::pttBegin() {
  if (!voice::pttActive()) voice::pttStart();   // usually already recording since the button went down
  pttWant = true;
}
void reach::pttEnd() {
  pttEndAt = millis() + 220;   // catch the tail of the last word
  pttWant = false;
}

void reach::sendShot(const uint8_t *buf, size_t n, int w, int h) {
  if (!linkOk) return;
  xSemaphoreTake(sockLock, portMAX_DELAY);
  client.printf("{\"t\":\"shot\",\"w\":%d,\"h\":%d,\"n\":%u}\n", w, h, (unsigned)n);
  client.write(buf, n);
  xSemaphoreGive(sockLock);
}

String reach::wifiState() {
  if (!cfg.wifiSsid.length()) return "off";
  switch (state) {
    case S_ONLINE: return "on " + cfg.wifiSsid;
    case S_JOINING: return "joining " + cfg.wifiSsid;
    case S_RETRY: return "could not join " + cfg.wifiSsid;
    default: return "starting";
  }
}
String reach::ip() { return wifiOk ? WiFi.localIP().toString() : String(); }
int reach::rssi() { return wifiOk ? WiFi.RSSI() : 0; }
String reach::peer() { return linkOk ? peerAddr : String(); }

void reach::scanStart() {
  scanN = -1;
  scanAsk = true;
}
int reach::scanCount() { return scanN; }
String reach::scanName(int i) { return (i >= 0 && i < scanN) ? scanNames[i] : String(); }
int reach::scanRssi(int i) { return (i >= 0 && i < scanN) ? scanRssis[i] : 0; }
