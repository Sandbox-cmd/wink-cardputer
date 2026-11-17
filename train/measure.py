"""Measures three things about a trained mind that were promised or feared.

  python measure.py runs/wink.bin

1. Does what you type change what it says?   (a babbler ignores its input)
2. How long does something that happened keep mattering?   (the reach of its state)
3. Does it stay sane after a very long life?   (100,000 pieces without a reset)
"""
import sys

import numpy as np

from pieces import EVENTS
from talk import Wn


def dist(w):
    l = np.array([float(v) for v in w._ask("q").split()])
    l -= l.max()
    p = np.exp(l)
    return p / p.sum()


def js(p, q):
    m = (p + q) / 2
    kl = lambda a, b: float(np.sum(a * np.log((a + 1e-12) / (b + 1e-12))))
    return (kl(p, m) + kl(q, m)) / 2 / np.log(2)   # 0 identical .. 1 nothing in common


model = sys.argv[1]
w = Wn(model, seed=3)
pc = w.pc

# a settled starting state: a little ordinary talk
w.reply("hello")
w.reply("how are you")
w._ask("w /tmp/m-base")

print("1. Does what you type change what it says?")
inputs = ["hello", "do you like rain", "im so tired today", "tell me a joke", "are you alive", "you are stupid",
          "good night", "i got the job", "my dog died", "what are you thinking about", "its raining outside", "sing me a song"]
firsts = []
for said in inputs:
    w._ask("l /tmp/m-base")
    w.feed([pc.id["<you>"]] + pc.encode(said) + [pc.id["<wink>"]])
    firsts.append(dist(w))   # how it would choose its mood
moods = [pc.names[23 + int(np.argmax(p[23:34]))][3:-1] for p in firsts]
pair = [js(firsts[i], firsts[j]) for i in range(len(inputs)) for j in range(i)]
print(f"   difference between its openings for different messages: mean {np.mean(pair):.3f} (0 = it ignores you, 1 = nothing in common)")
print("   the mood it would open with:", ", ".join(f"{s!r}: {m}" for s, m in zip(inputs, moods)))
echo = 0
for said in inputs:
    w._ask("l /tmp/m-base")
    _, words = w.reply(said, temp=0.5)
    content = [x for x in said.split() if len(x) > 3]
    echo += any(x in words for x in content)
print(f"   replies that pick up a word from the message: {echo} of {len(inputs)}")

print("2. How long does something that happened keep mattering?")
filler = pc.encode("it is a quiet day . nothing much is going on . the light is the same . ") * 40
for ev in ["shake", "night", "mac_leaves"]:
    reach = []
    for gap in [0, 5, 10, 20, 40, 80, 160, 320]:
        w._ask("l /tmp/m-base")
        w.feed(filler[:gap] + [pc.id["<thought>"]])
        plain = dist(w)
        w._ask("l /tmp/m-base")
        w.feed([pc.id[f"<e:{ev}>"]] + filler[:gap] + [pc.id["<thought>"]])
        reach.append((gap, js(plain, dist(w))))
    print(f"   <{ev}>: " + "  ".join(f"{g} pieces later {d:.3f}" for g, d in reach))

print("3. Does it stay sane after a very long life?")
w._ask("l /tmp/m-base")
seen, lengths, stuck = set(), [], 0
state_abs = []
total = 0
events = [f"<e:{e}>" for e in EVENTS]
rng = np.random.default_rng(5)
while total < 100000:
    r = rng.random()
    if r < 0.2:
        w.feed([pc.id[events[int(rng.integers(len(events)))]]])
        total += 1
    said = inputs[int(rng.integers(len(inputs)))] if r < 0.6 else None
    mood, words = w.reply(said, sign="<wink>" if said else "<thought>")
    n = len(pc.encode(words)) + 3 + (len(pc.encode(said)) if said else 0)
    total += n
    lengths.append(len(words.split()))
    seen.add(words)
    stuck += (len(words.split()) >= 30)
    if len(lengths) % 500 == 0:
        st = w.state()
        state_abs.append(float(np.mean(np.abs(st) > 0.98)))
        print(f"   after {total:6d} pieces: last line [{mood}] {words[:70]!r}; state units pinned at the limit {100 * state_abs[-1]:.0f}%")
print(f"   {len(lengths)} lines, {len(seen)} different, mean {np.mean(lengths):.1f} words, {stuck} ran on to the limit")
