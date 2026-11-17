"""How Wink's mind reads and writes text: as word pieces.

Text is lowercased and split into words and punctuation marks. Each word is cut into
the longest pieces the mind knows, left to right; the first piece of a word carries a
leading "_". Every single character is itself a piece, so any word can be read, typos
included. The same cutting rule is implemented in C on the chip (firmware/src/think.cpp),
so what is trained here is what runs there.

Besides pieces there are a few signs the mind reads and writes whole: who is speaking,
what happened to the device, the mood of a line, and two blanks the device fills in.
"""
import json
import re
from collections import Counter

MOODS = ["calm", "happy", "delighted", "curious", "thinking", "sleepy", "surprised", "sad", "cheeky", "tender", "alert"]
EVENTS = ["wake", "pickup", "putdown", "shake", "facedown", "loud", "voices", "quiet", "morning", "night", "low",
          "plugged", "unplugged", "mac_arrives", "mac_leaves", "long_gap"]
SIGNS = ["<end>", "<you>", "<wink>", "<thought>", "<story>", "{time}", "{pct}"] + [f"<e:{e}>" for e in EVENTS] + [f"<m:{m}>" for m in MOODS]
CHARS = "abcdefghijklmnopqrstuvwxyz0123456789'-"
MARKS = ".,?!"

_word_re = re.compile(r"\{time\}|\{pct\}|[a-z0-9'\-]+|[.,?!]")


def words_of(text):
    """Lowercase, keep letters, digits, apostrophes, hyphens and . , ? !; everything else is a gap."""
    text = text.lower().replace("’", "'").replace("‘", "'").replace("“", " ").replace("”", " ")
    return _word_re.findall(text)


def learn(word_counts, size):
    """Byte-pair merging over a table of word counts, until there are `size` pieces."""
    vocab = set(MARKS)
    for c in CHARS:
        vocab.add(c)
        vocab.add("_" + c)
    words = {}
    for w, n in word_counts.items():
        if w in MARKS or w in ("{time}", "{pct}"):
            continue
        syms = tuple(["_" + w[0]] + list(w[1:]))
        words[syms] = words.get(syms, 0) + n
    while len(vocab) < size:
        pairs = Counter()
        for syms, n in words.items():
            for a, b in zip(syms, syms[1:]):
                pairs[(a, b)] += n
        if not pairs:
            break
        (a, b), n = pairs.most_common(1)[0]
        if n < 2:
            break
        merged = a + b
        vocab.add(merged)
        new_words = {}
        for syms, cnt in words.items():
            out, i = [], 0
            while i < len(syms):
                if i + 1 < len(syms) and syms[i] == a and syms[i + 1] == b:
                    out.append(merged)
                    i += 2
                else:
                    out.append(syms[i])
                    i += 1
            t = tuple(out)
            new_words[t] = new_words.get(t, 0) + cnt
        words = new_words
    return sorted(vocab)


class Pieces:
    def __init__(self, pieces):
        self.names = list(SIGNS) + list(pieces)
        self.id = {p: i for i, p in enumerate(self.names)}
        self.longest = max(len(p) for p in pieces)
        self._cache = {}

    @classmethod
    def load(cls, path):
        return cls(json.load(open(path))["pieces"])

    def save(self, path):
        json.dump({"signs": SIGNS, "pieces": self.names[len(SIGNS):]}, open(path, "w"))

    def cut(self, word):
        """The longest known piece, then the next, to the end of the word."""
        got = self._cache.get(word)
        if got is not None:
            return got
        if word in self.id and (word in MARKS or word.startswith("{")):
            out = [self.id[word]]
        else:
            out, i = [], 0
            while i < len(word):
                lead = "_" if i == 0 else ""
                for n in range(min(self.longest - len(lead), len(word) - i), 0, -1):
                    cand = lead + word[i:i + n]
                    if cand in self.id:
                        out.append(self.id[cand])
                        i += n
                        break
                else:
                    i += 1   # a character it has no piece for: skipped
        if len(self._cache) < 200000:
            self._cache[word] = out
        return out

    def encode(self, text):
        ids = []
        for w in words_of(text):
            ids.extend(self.cut(w))
        return ids

    def decode(self, ids):
        out = ""
        for i in ids:
            p = self.names[i]
            if p in MARKS:
                out += p
            elif p.startswith("_"):
                out += " " + p[1:]
            elif p.startswith("<") or p.startswith("{"):
                out += " " + p
            else:
                out += p
        return out.strip()
