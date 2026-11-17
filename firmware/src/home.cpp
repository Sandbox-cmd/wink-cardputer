// Home: the face, what it says, and the line you are typing to it.
#include "kernel.h"
#include "face.h"
#include "link.h"
#include "mind.h"
#include "ui.h"
#include "voice.h"
#include "think.h"

namespace {

struct Home : App {
  ui::Field input;
  int g_lastCursorX = 14;
  String lastSent;
  String cachedText;
  std::vector<ui::Span> cachedLines;
  uint32_t lastKeyAt = 0, quietSince = 0;
  int keysInBurst = 0;

  const char *title() override { return "Talk"; }
  bool arrowsBare() override { return false; }   // here ; . , / are things you type

  void enter() override {
    input.maxLen = 160;
    face::repaint();   // the screen held another room: fresh paper under Wisp
  }

  void key(const Key &k) override {
    uint32_t now = millis();
    if (now - lastKeyAt > 1500) keysInBurst = 0;
    if (lastKeyAt && now - lastKeyAt > 30 * 60 * 1000UL && !os::speech().active) os::sayLine("touch.first_today", false);
    lastKeyAt = now;
    keysInBurst++;

    if (k.code == K_ESC) {
      if (input.text.length()) input.text = "";
      else os::quiet();
      return;
    }
    if (k.code == K_UP && !input.text.length()) {
      input.text = lastSent;
      return;
    }
    if (k.code == K_ENTER) {
      String t = input.text;
      t.trim();
      input.text = "";
      if (!t.length()) {
        if (os::speech().active) os::quiet();
        return;
      }
      lastSent = t;
      face::poke(SCREEN_W / 2, SCREEN_H - 10, 0.8f);
      for (int i = 0, words = 1; i < (int)t.length() && words < 6; i++)   // your words fly in, blue
        if (t[i] == ' ' || i == 0) { face::wordIn(false); words++; }
      os::ask(t, "keys");
      return;
    }
    if (input.key(k)) {
      if (os::speech().active && os::speech().doneAt) os::speech().active = false;   // you are talking now
      face::poke(20 + min(200, (int)input.text.length() * 7), SCREEN_H - 8, 0.35f);
      g_lastCursorX = 14 + min(200, (int)input.text.length() * 7);
      face::lookAt(g_lastCursorX, SCREEN_H - 8, 3000);
    }
  }

  void draw(Gfx &g) override {
    Speech &sp = os::speech();
    bool typing = input.text.length() > 0;
    bool wait = os::waiting() && strlen(os::waitText());
    String writing = os::ownThinking() ? think::partial() : String();
    float thoughtLife = 0;
    String thought = (!sp.active && !typing && !os::ownThinking()) ? os::thought(thoughtLife) : String();
    bool ptt = os::listeningPtt();
    String heard = os::heard();

    // what the creature is saying, wrapped once per text
    g.setFont(ui::fBody);
    g.setTextSize(1);
    int speechRows = 0, shown = 0, lastLine = 0;
    if (sp.active) {
      if (cachedText != sp.text) {
        cachedText = sp.text;
        cachedLines = ui::wrap(g, sp.text, SCREEN_W - 14);
      }
      shown = os::speechShown();
      for (size_t i = 0; i < cachedLines.size(); i++)
        if (cachedLines[i].start < shown || i == 0) lastLine = i;
      speechRows = min(typing ? 2 : 3, lastLine + 1);
    }
    int otherRows = (typing ? 1 : 0) + ((!sp.active && (wait || ptt || heard.length() || writing.length())) ? 1 : 0);
    int rows = speechRows + otherRows;
    face::setTextRows(rows ? rows * ui::TEXT_ROW + 5 : 0);
    face::draw(g);

    // small marks in the corners: the mind that is home, and energy
    if (reach::up()) g.fillCircle(7, 7, 3, face::tint());
    else {
      g.drawCircle(7, 7, 3, COL_FAINT);
      if (reach::wifiUp()) g.drawPixel(7, 7, COL_DIM);
    }
    ui::battery(g, SCREEN_W - 19, 5, COL_FAINT);

    int y = SCREEN_H - rows * ui::TEXT_ROW - 2;
    g.setFont(ui::fBody);
    g.setTextDatum(textdatum_t::top_left);
    if (speechRows) {
      int first = lastLine + 1 - speechRows;
      for (int i = 0; i < speechRows; i++) {
        const ui::Span &ln = cachedLines[first + i];
        int end = min(ln.end, max(ln.start, shown));
        String part = sp.text.substring(ln.start, end);
        int w = g.textWidth(sp.text.substring(ln.start, ln.end));
        g.setTextColor(COL_INK, COL_BAND);
        g.drawString(part, (SCREEN_W - w) / 2, y);
        y += ui::TEXT_ROW;
      }
    } else if (writing.length()) {   // its own mind, writing: the words as they come
      g.setTextColor(COL_DIM);
      g.setTextDatum(textdatum_t::top_center);
      String w = writing;
      while (w.length() > 4 && g.textWidth(w) > SCREEN_W - 12) w.remove(0, 1);
      g.drawString(w, SCREEN_W / 2, y);
      y += ui::TEXT_ROW;
    } else if (ptt) {
      g.setTextColor(COL_YOU);
      g.setTextDatum(textdatum_t::top_center);
      g.drawString("listening", SCREEN_W / 2, y);
      y += ui::TEXT_ROW;
    } else if (wait) {
      g.setTextColor(COL_DIM);
      g.setTextDatum(textdatum_t::top_center);
      g.drawString(os::waitText(), SCREEN_W / 2, y);
      y += ui::TEXT_ROW;
    } else if (heard.length() && !typing) {
      g.setTextColor(COL_YOU);
      g.setTextDatum(textdatum_t::top_center);
      String h = heard;
      while (h.length() > 4 && g.textWidth(h) > SCREEN_W - 12) h.remove(0, 1);
      g.drawString(h, SCREEN_W / 2, y);
      y += ui::TEXT_ROW;
    }
    g.setTextDatum(textdatum_t::top_left);
    if (typing) input.draw(g, 6, y, SCREEN_W - 8, COL_YOU, true, "> ");

    // a thought of its own, passing: small, quiet, born at its head, there if you look
    if (thought.length() && rows == 0) {
      float fade = thoughtLife < 0.12f ? thoughtLife / 0.12f : thoughtLife > 0.75f ? (1.0f - thoughtLife) / 0.25f : 1.0f;
      fade *= 0.8f;   // mixed against the paper here, never blended with the frame (it is not cleared)
      g.setFont(&fonts::DejaVu9);
      g.setTextColor(rgb((uint8_t)(246 + (120 - 246) * fade), (uint8_t)(235 + (70 - 235) * fade), (uint8_t)(220 + (58 - 220) * fade)));
      auto lines = ui::wrap(g, thought, 112);
      int n = min(3, (int)lines.size());
      float tx, ty;
      face::thoughtAt(thoughtLife * 9.0f, n, &tx, &ty);
      g.setTextDatum(textdatum_t::top_center);
      for (int i = 0; i < n; i++) g.drawString(thought.substring(lines[i].start, lines[i].end), (int)tx, (int)ty + i * 10);
      g.setTextDatum(textdatum_t::top_left);
    }

    // asleep: z marks rising from its head
    float zx[4], zy[4], za[4];
    uint8_t zp[4];
    int nz = face::zs(4, zx, zy, za, zp);
    for (int i = 0; i < nz; i++) {
      g.setFont(zp[i] > 9 ? &fonts::DejaVu12 : &fonts::DejaVu9);
      float a = za[i];
      g.setTextColor(rgb((uint8_t)(246 + (130 - 246) * a), (uint8_t)(235 + (90 - 235) * a), (uint8_t)(220 + (110 - 220) * a)));
      g.drawString("z", (int)zx[i], (int)zy[i]);
    }
  }
};

Home homeApp;
}  // namespace

App *apps::home() { return &homeApp; }
