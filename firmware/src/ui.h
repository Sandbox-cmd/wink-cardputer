// Small shared pieces of every room: type, wrapped text, a header, a list.
#pragma once
#include "os.h"
#include <functional>
#include <vector>

namespace ui {
extern const lgfx::IFont *fBody;    // words the creature and you say
extern const lgfx::IFont *fList;    // lists and labels
extern const lgfx::IFont *fSmall;   // marks and hints
static const int TEXT_ROW = 14;
static const int LIST_ROW = 16;
static const int HEAD_ROW = 15;

struct Span {
  int start, end;   // character offsets of one wrapped line
};
std::vector<Span> wrap(Gfx &g, const String &text, int width);   // uses the font set on g

void header(Gfx &g, const char *title, const char *right = nullptr);
void hint(Gfx &g, const char *text);
void battery(Gfx &g, int x, int y, uint16_t col);
void background(Gfx &g);

struct List {
  int sel = 0, top = 0;
  bool key(const Key &k, int count);   // true when it moved
  void draw(Gfx &g, int y, int rows, int count, const std::function<String(int)> &label,
            const std::function<String(int)> &value = nullptr);
};

// one line of text being typed
struct Field {
  String text;
  size_t maxLen = 200;
  bool key(const Key &k);   // true when the text changed
  void draw(Gfx &g, int x, int y, int width, uint16_t col, bool caret = true, const char *prefix = nullptr);
};
}
