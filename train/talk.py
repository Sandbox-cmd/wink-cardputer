"""Talks to a trained mind through the chip's own code (wn.c), on the Mac.

  python talk.py runs/wink.bin                     type to it; a blank line makes it think to itself
  python talk.py runs/wink.bin --probe             a fixed set of checks, printed for reading
  python talk.py runs/base.bin --story "once upon a time"    let the base mind continue some words
"""
import argparse
import subprocess
import sys

import numpy as np

from pieces import EVENTS, Pieces, words_of

here = __file__.rsplit("/", 1)[0]


class Wn:
    favour = 2.0   # how strongly it is drawn to the words just said to it

    def __init__(self, model, seed=1):
        self.pc = Pieces.load(here + "/pieces.json")
        self.p = subprocess.Popen([here + "/wn", model, str(seed)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def _ask(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        return self.p.stdout.readline().strip()

    def feed(self, ids):
        if ids:
            self._ask("f " + " ".join(map(str, ids)))

    def event(self, name):
        self.feed([self.pc.id[f"<e:{name}>"]])

    def reply(self, text=None, sign="<wink>", temp=0.55, topk=12, limit=40):
        """Feeds what was said, then lets it answer. Returns (mood, words)."""
        if text is not None:
            self.feed([self.pc.id["<you>"]] + self.pc.encode(text))
            # favour the words just said to it (the longer ones: those carry the subject)
            said = [i for w in words_of(text) if len(w) >= 4 for i in self.pc.cut(w)]
            self._ask(f"b {self.favour if said else 0} " + " ".join(map(str, said)))
        else:
            self._ask("b 0")
        self.feed([self.pc.id[sign]])
        mood = self.pc.names[int(self._ask(f"m {temp}"))][3:-1]
        ids = [int(i) for i in self._ask(f"g {temp} {topk} {limit}").split()]
        return mood, self.pc.decode([i for i in ids if i != 0])

    def state(self):
        return np.array([float(v) for v in self._ask("s").split()])

    def reset(self):
        self._ask("r")

    def loss(self, path, count):
        return float(self._ask(f"n {path} {count}"))


def probe(w):
    print("== does the reply change when the words do? (each asked from the same starting state)")
    w.reply("hello")
    w._ask("w /tmp/wn-state")
    for said in ["hello", "do you like rain", "im so tired today", "tell me a joke", "what is the capital of france",
                 "are you alive", "you are stupid", "good night wink", "asdfghjkl", "why", "what time is it",
                 "i got the job", "my dog died", "whats your name", "can you hear me"]:
        w._ask("l /tmp/wn-state")
        mood, words = w.reply(said)
        print(f"  you: {said:32s} wink [{mood}]: {words}")
    print("== the same words three times (it should not say one fixed thing, nor wander off)")
    for _ in range(3):
        w._ask("l /tmp/wn-state")
        print("  you: how are you                    wink [%s]: %s" % w.reply("how are you"))
    print("== things that happen to it")
    for e in ["pickup", "shake", "loud", "night", "mac_leaves", "long_gap"]:
        w._ask("l /tmp/wn-state")
        w.event(e)
        print(f"  <{e}>".ljust(38) + "wink [%s]: %s" % w.reply())
    print("== left alone: what it thinks")
    w._ask("l /tmp/wn-state")
    for e in ["quiet", "quiet", "night", "quiet"]:
        w.event(e)
        print("  thought [%s]: %s" % w.reply(sign="<thought>", temp=0.8))
    print("== a conversation, the state carried through")
    for said in ["hey", "i had a long day", "my boss was shouting", "anyway. what are you up to", "ha", "ok bye"]:
        print(f"  you: {said:32s} wink [%s]: %s" % w.reply(said))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--probe", action="store_true")
    ap.add_argument("--story", default=None)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    w = Wn(a.model, a.seed)
    if a.story is not None:
        w.feed([w.pc.id["<story>"]] + w.pc.encode(a.story))
        ids = [int(i) for i in w._ask("g 0.7 20 120").split()]
        print(a.story + " " + w.pc.decode([i for i in ids if i != 0]))
    elif a.probe:
        probe(w)
    else:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                w.event("quiet")
                print("  (thinks) [%s] %s" % w.reply(sign="<thought>", temp=0.8))
            elif line.startswith("<") and line[1:-1] in EVENTS:
                w.event(line[1:-1])
                print("  wink [%s]: %s" % w.reply())
            else:
                print("  wink [%s]: %s" % w.reply(line))
