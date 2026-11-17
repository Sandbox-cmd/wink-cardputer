#include "ui.h"
#include "face.h"
#include "link.h"

namespace ui {
const lgfx::IFont *fBody = &fonts::DejaVu12;
const lgfx::IFont *fList = &fonts::Font2;
const lgfx::IFont *fSmall = &fonts::Font0;

std::vector<Span> wrap(Gfx &g, const String &text, int width) {
  std::vector<Span> out;
  int n = text.length(), start = 0;
  while (start < n) {
    int end = start, lastSpace = -1;
    while (end < n && text[end] != '\n') {
      if (text[end] == ' ') lastSpace = end;
      if (g.textWidth(text.substring(start, end + 1)) > width) {
        if (lastSpace > start) end = lastSpace;
        break;
      }
      end++;
    }
    if (end == start && start < n && text[start] != '\n') end = start + 1;   // one char wider than the line
    out.push_back({start, end});
    start = end;
    while (start < n && text[start] == ' ') start++;
    if (start < n && text[start] == '\n') start++;
  }
  return out;
}

void background(Gfx &g) {
  g.fillScreen(COL_BG);
  // a few motes drift in every room, so it is all made of the same stuff as Wink
  uint32_t t = millis() / 40;
  for (int i = 0; i < 14; i++) {
    int x = (i * 53 + (int)(t * (1 + i % 3)) / 7) % SCREEN_W;
    int y = HEAD_ROW + 8 + (i * 37 + (int)(t / (3 + i % 4))) % (SCREEN_H - HEAD_ROW - 10);
    g.fillRect(x, y, 2, 2, rgb(236, 196, 176));
  }
}

void battery(Gfx &g, int x, int y, uint16_t col) {
  int pct = M5Cardputer.Power.getBatteryLevel();
  int lit = pct >= 0 ? (pct + 12) / 25 : 0;
  for (int i = 0; i < 4; i++) {
    if (i < lit) g.fillRect(x + i * 4, y, 3, 5, col);
    else g.drawRect(x + i * 4, y, 3, 5, COL_RULE);
  }
}

void header(Gfx &g, const char *title, const char *right) {
  g.fillRect(0, 0, SCREEN_W, HEAD_ROW, COL_BAND);
  g.drawFastHLine(0, HEAD_ROW, SCREEN_W, COL_RULE);
  face::drawMini(g, 5, 3);
  g.setFont(fSmall);
  g.setTextSize(1);
  g.setTextColor(COL_INK);
  g.setTextDatum(textdatum_t::middle_left);
  g.drawString(title, 30, HEAD_ROW / 2 + 1);
  String r = right ? String(right) : os::clockText();
  g.setTextColor(COL_DIM);
  g.setTextDatum(textdatum_t::middle_right);
  g.drawString(r, SCREEN_W - 26, HEAD_ROW / 2 + 1);
  battery(g, SCREEN_W - 20, 5, reach::up() ? COL_CORAL : COL_DIM);
  g.setTextDatum(textdatum_t::top_left);
}

void hint(Gfx &g, const char *text) {
  g.setFont(fSmall);
  g.setTextSize(1);
  g.setTextColor(COL_FAINT);
  g.setTextDatum(textdatum_t::bottom_center);
  g.drawString(text, SCREEN_W / 2, SCREEN_H - 1);
  g.setTextDatum(textdatum_t::top_left);
}

bool List::key(const Key &k, int count) {
  if (count <= 0) return false;
  if (k.code == K_UP) sel = (sel + count - 1) % count;
  else if (k.code == K_DOWN) sel = (sel + 1) % count;
  else return false;
  return true;
}

void List::draw(Gfx &g, int y, int rows, int count, const std::function<String(int)> &label,
                const std::function<String(int)> &value) {
  if (sel >= count) sel = count ? count - 1 : 0;
  if (sel < top) top = sel;
  if (sel >= top + rows) top = sel - rows + 1;
  if (top < 0) top = 0;
  g.setFont(fList);
  g.setTextSize(1);
  for (int i = 0; i < rows && top + i < count; i++) {
    int idx = top + i, ry = y + i * LIST_ROW;
    bool on = idx == sel;
    if (on) {
      g.fillRoundRect(3, ry, SCREEN_W - 6, LIST_ROW, 3, COL_SEL);
      g.fillRect(3, ry + 3, 2, LIST_ROW - 6, COL_CORAL);
    }
    g.setTextColor(on ? COL_INK : COL_DIM);
    g.setTextDatum(textdatum_t::top_left);
    g.drawString(label(idx), 10, ry);
    if (value) {
      g.setTextColor(on ? COL_CORAL : COL_FAINT);
      g.setTextDatum(textdatum_t::top_right);
      g.drawString(value(idx), SCREEN_W - 9, ry);
    }
  }
  g.setTextDatum(textdatum_t::top_left);
  if (count > rows) {   // where you are in a long list
    int trackH = rows * LIST_ROW, barH = max(6, trackH * rows / count);
    int barY = y + (trackH - barH) * top / max(1, count - rows);
    g.fillRect(SCREEN_W - 2, barY, 2, barH, COL_FAINT);
  }
}

bool Field::key(const Key &k) {
  if (k.code == K_CHAR && text.length() < maxLen) {
    text += k.ch;
    return true;
  }
  if (k.code == K_DEL && text.length()) {
    text.remove(text.length() - 1);
    return true;
  }
  return false;
}

void Field::draw(Gfx &g, int x, int y, int width, uint16_t col, bool caret, const char *prefix) {
  g.setFont(fBody);
  g.setTextSize(1);
  String shown = prefix ? String(prefix) + text : text;
  // keep the end in view
  while (shown.length() > 1 && g.textWidth(shown) > width - 6) shown.remove(0, 1);
  g.setTextColor(col);
  g.setTextDatum(textdatum_t::top_left);
  g.drawString(shown, x, y);
  if (caret && (millis() / 400) % 2 == 0) g.fillRect(x + g.textWidth(shown) + 1, y + 1, 2, TEXT_ROW - 3, col);
}
}  // namespace ui
