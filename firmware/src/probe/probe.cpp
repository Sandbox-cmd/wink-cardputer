// Bring-up probe: says what this board is and exercises each part once.
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include "mbedtls/base64.h"
#include "samvoice.h"

static M5Canvas canvas(&M5Cardputer.Display);
static String line;
static String lastKeys;

static void shot() {
  const uint8_t *buf = (const uint8_t *)canvas.getBuffer();
  size_t total = canvas.width() * canvas.height() * 2;
  Serial.printf("#shot %d %d\n", canvas.width(), canvas.height());
  static unsigned char out[4100];
  for (size_t off = 0; off < total; off += 3000) {
    size_t n = min((size_t)3000, total - off), olen = 0;
    mbedtls_base64_encode(out, sizeof(out), &olen, buf + off, n);
    Serial.write(out, olen);
    Serial.write('\n');
  }
  Serial.println("#ok");
}

static uint8_t sbuf[3][1024];
static int sidx = 0, spos = 0;
static void sink(void *, unsigned char b) {
  sbuf[sidx][spos++] = b;
  if (spos == 1024) {
    while (M5Cardputer.Speaker.isPlaying(0) == 2) delay(1);
    M5Cardputer.Speaker.playRaw(sbuf[sidx], spos, SAM_RATE, false, 1, 0, false);
    sidx = (sidx + 1) % 3;
    spos = 0;
  }
}

static void draw(const char *msg) {
  canvas.fillScreen(TFT_BLACK);
  for (int i = 0; i < 240; i += 8) canvas.drawFastVLine(i, 0, 6, canvas.color565(i, 120, 255 - i));
  canvas.setTextColor(TFT_WHITE);
  canvas.setFont(&fonts::Font2);
  canvas.setCursor(4, 12);
  canvas.printf("probe: %s\n", msg);
  canvas.setCursor(4, 34);
  canvas.printf("keys: %s", lastKeys.c_str());
  canvas.setCursor(4, 56);
  canvas.printf("heap %u  bat %d%%", (unsigned)ESP.getFreeHeap(), M5Cardputer.Power.getBatteryLevel());
  canvas.pushSprite(0, 0);
}

static void handle(String c) {
  c.trim();
  if (c == "sys") {
    Serial.printf("board=%d (Cardputer=%d ADV=%d)\n", (int)M5.getBoard(), (int)m5::board_t::board_M5Cardputer,
#ifdef board_M5CardputerADV
                  (int)m5::board_t::board_M5CardputerADV
#else
                  -1
#endif
                  );
    Serial.printf("chip=%s rev=%d cores=%d flash=%u psram=%u\n", ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores(), (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getPsramSize());
    Serial.printf("heap free=%u min=%u maxblock=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
    Serial.printf("display=%dx%d bat=%d%% %dmV charging=%d\n", M5Cardputer.Display.width(),
                  M5Cardputer.Display.height(), M5Cardputer.Power.getBatteryLevel(),
                  M5Cardputer.Power.getBatteryVoltage(), (int)M5Cardputer.Power.isCharging());
    Serial.printf("spk enabled=%d mic enabled=%d imu=%d rtc=%d\n", (int)M5Cardputer.Speaker.isEnabled(),
                  (int)M5Cardputer.Mic.isEnabled(), (int)M5.Imu.isEnabled(), (int)M5.Rtc.isEnabled());
    Serial.println("#ok");
  } else if (c == "i2c") {
    for (int bus = 0; bus < 2; bus++) {
      auto &b = bus ? M5.Ex_I2C : M5.In_I2C;
      Serial.printf("bus %s enabled=%d:", bus ? "ext" : "int", (int)b.isEnabled());
      if (b.isEnabled()) {
        bool r[120];
        b.scanID(r);
        for (int i = 8; i < 120; i++)
          if (r[i]) Serial.printf(" 0x%02x", i);
      }
      Serial.println();
    }
    Serial.println("#ok");
  } else if (c == "shot") {
    shot();
  } else if (c == "tone") {
    M5Cardputer.Speaker.tone(880, 150);
    delay(200);
    M5Cardputer.Speaker.tone(1320, 150);
    Serial.println("#ok");
  } else if (c.startsWith("say ")) {
    uint32_t t0 = millis();
    SamParams p;
    spos = 0;
    bool ok = sam_speak(c.c_str() + 4, p, sink, nullptr);
    if (spos) {
      while (M5Cardputer.Speaker.isPlaying(0) == 2) delay(1);
      M5Cardputer.Speaker.playRaw(sbuf[sidx], spos, SAM_RATE, false, 1, 0, false);
      sidx = (sidx + 1) % 3;
    }
    Serial.printf("say ok=%d took=%ums heapmin=%u\n", (int)ok, (unsigned)(millis() - t0), (unsigned)ESP.getMinFreeHeap());
    Serial.println("#ok");
  } else if (c == "mic") {
    uint32_t t0 = micros();
    M5Cardputer.Speaker.end();
    uint32_t t1 = micros();
    bool mb = M5Cardputer.Mic.begin();
    uint32_t t2 = micros();
    static int16_t rec[2048];
    int32_t peak = 0;
    double sum = 0;
    int got = 0;
    for (int k = 0; k < 8; k++) {
      if (M5Cardputer.Mic.record(rec, 2048, 16000)) {
        while (M5Cardputer.Mic.isRecording()) delay(1);
        for (int i = 0; i < 2048; i++) {
          int v = abs(rec[i]);
          if (v > peak) peak = v;
          sum += (double)rec[i] * rec[i];
        }
        got += 2048;
      }
    }
    uint32_t t3 = micros();
    M5Cardputer.Mic.end();
    uint32_t t4 = micros();
    bool sb = M5Cardputer.Speaker.begin();
    uint32_t t5 = micros();
    Serial.printf("mic begin=%d samples=%d peak=%d rms=%.1f\n", (int)mb, got, (int)peak, got ? sqrt(sum / got) : 0.0);
    Serial.printf("timings us: spk.end=%u mic.begin=%u record=%u mic.end=%u spk.begin=%u (spk ok=%d)\n",
                  (unsigned)(t1 - t0), (unsigned)(t2 - t1), (unsigned)(t3 - t2), (unsigned)(t4 - t3),
                  (unsigned)(t5 - t4), (int)sb);
    M5Cardputer.Speaker.tone(660, 80);
    Serial.println("#ok");
  } else if (c == "sd") {
    SPI.begin(40, 39, 14, 12);
    bool ok = SD.begin(12, SPI, 25000000);
    Serial.printf("sd ok=%d", (int)ok);
    if (ok) Serial.printf(" type=%d size=%lluMB", (int)SD.cardType(), SD.cardSize() / (1024 * 1024));
    Serial.println();
    Serial.println("#ok");
  } else if (c.length()) {
    Serial.println("#err unknown");
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(120);
  canvas.setColorDepth(16);
  canvas.createSprite(240, 135);
  M5Cardputer.Speaker.setVolume(120);
  draw("hello");
}

void loop() {
  M5Cardputer.update();
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') {
      handle(line);
      line = "";
    } else if (ch != '\r')
      line += ch;
  }
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    lastKeys = "";
    for (auto ch : st.word) lastKeys += ch;
    if (st.enter) lastKeys += "<ent>";
    if (st.del) lastKeys += "<del>";
    if (st.fn) lastKeys += "<fn>";
    if (st.ctrl) lastKeys += "<ctrl>";
    if (st.opt) lastKeys += "<opt>";
    if (st.alt) lastKeys += "<alt>";
    if (st.tab) lastKeys += "<tab>";
    if (st.shift) lastKeys += "<shift>";
    Serial.printf("keys: %s\n", lastKeys.c_str());
  }
  if (M5Cardputer.BtnA.wasPressed()) Serial.println("btnA");
  static uint32_t last = 0;
  if (millis() - last > 200) {
    last = millis();
    draw("alive");
  }
}
