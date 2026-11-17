"""Turns text into the streams of piece numbers the mind is trained on.

  python prep.py pieces  <megabytes of stories> <how many pieces>   learn the pieces
  python prep.py stories <megabytes>                                general simple English
  python prep.py wink                                               the Wink corpus (train/corpus/*.jsonl + the kept lines)
"""
import glob
import json
import random
import sys
from collections import Counter

import numpy as np

from pieces import EVENTS, MOODS, Pieces, learn, words_of

import re

NOTHING = re.compile(r"no idea|nothing in my head|came before|from nothing|blank|know a thing", re.I)
HERE = __file__.rsplit("/", 1)[0]
STORIES = HERE + "/data/tinystories-train-part.txt"


def stories(megabytes):
    with open(STORIES, encoding="utf-8", errors="ignore") as f:
        text = f.read(int(megabytes * 1_000_000))
    return [s.strip() for s in text.split("<|endoftext|>") if s.strip()]


def episodes():
    """Every episode of the Wink corpus, plus the kept lines as one-line episodes."""
    eps = []
    for path in sorted(glob.glob(HERE + "/corpus/*.jsonl")):
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if not line:
                continue
            try:
                ep = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(ep.get("turns"), list) and ep["turns"]:
                # thoughts that claim it wakes knowing nothing are left out: what it keeps is in its Memory room
                turns = [t for t in ep["turns"] if not ("thought" in t and NOTHING.search(str(t["thought"])))]
                if turns:
                    # batch two is the voice he asked for on 17 November: it counts twice
                    for _ in range(2 if path.rsplit("/", 1)[-1].startswith("ep2-") else 1):
                        eps.append(turns)
    return eps


def encode_episode(pc, turns):
    ids = []
    for t in turns:
        mood = t.get("mood") if t.get("mood") in MOODS else "calm"
        if "event" in t and t["event"] in EVENTS:
            ids.append(pc.id[f"<e:{t['event']}>"])
        elif "you" in t:
            ids += [pc.id["<you>"]] + pc.encode(str(t["you"]))
        elif "wink" in t:
            ids += [pc.id["<wink>"], pc.id[f"<m:{mood}>"]] + pc.encode(str(t["wink"])) + [pc.id["<end>"]]
        elif "thought" in t:
            ids += [pc.id["<thought>"], pc.id[f"<m:{mood}>"]] + pc.encode(str(t["thought"])) + [pc.id["<end>"]]
    return ids


def main():
    what = sys.argv[1]
    if what == "pieces":
        mb, size = float(sys.argv[2]), int(sys.argv[3])
        counts = Counter()
        for s in stories(mb):
            counts.update(words_of(s))
        wink = Counter()
        for turns in episodes():
            for t in turns:
                for key in ("you", "wink", "thought"):
                    if key in t:
                        wink.update(words_of(str(t[key])))
        for line_set in json.load(open(HERE + "/../character/lines.json")).values():
            for l in line_set:
                wink.update(words_of(l["say"].replace("{name}", "wink")))
        for w, n in wink.items():   # Wink's own words count for much more than the stories'
            counts[w] += n * 200
        pc = Pieces(learn(counts, size))
        pc.save(HERE + "/pieces.json")
        print(f"{len(counts)} different words -> {len(pc.names)} pieces and signs; longest piece {pc.longest}")
        for probe in ["hello wink, what are you thinking about?", "im soooo tierd today lol", "the afternoon remembers nothing"]:
            ids = pc.encode(probe)
            print(f"  {probe!r} -> {len(ids)} pieces: {' '.join(pc.names[i] for i in ids)}")
    elif what == "stories":
        pc = Pieces.load(HERE + "/pieces.json")
        story, end = pc.id["<story>"], pc.id["<end>"]
        ids = []
        for s in stories(float(sys.argv[2])):
            ids += [story] + pc.encode(s) + [end]
        arr = np.array(ids, dtype=np.int16)
        np.save(HERE + "/data/stories.npy", arr)
        print(f"{len(arr):,} pieces of general English")
    elif what == "dialog":
        # everyday conversations, read as the person and Wink taking turns, so the mind learns to answer
        pc = Pieces.load(HERE + "/pieces.json")
        feel = {"0": "calm", "1": "alert", "2": "calm", "3": "alert", "4": "happy", "5": "sad", "6": "surprised"}
        base = HERE + "/data/dailydialog/train_x/train/"
        ids = []
        talks = open(base + "dialogues_train.txt", encoding="utf-8").read().split("\n")
        feels = open(base + "dialogues_emotion_train.txt", encoding="utf-8").read().split("\n")
        for talk, fl in zip(talks, feels):
            turns = [t.strip() for t in talk.split("__eou__") if t.strip()]
            marks = fl.split()
            for i, t in enumerate(turns):
                if i % 2 == 0:
                    ids += [pc.id["<you>"]] + pc.encode(t)
                else:
                    mood = feel.get(marks[i] if i < len(marks) else "0", "calm")
                    ids += [pc.id["<wink>"], pc.id[f"<m:{mood}>"]] + pc.encode(t) + [pc.id["<end>"]]
        np.save(HERE + "/data/dialog.npy", np.array(ids, dtype=np.int16))
        print(f"{len(ids):,} pieces of everyday conversation in {len(talks):,} talks")
    elif what == "wink":
        pc = Pieces.load(HERE + "/pieces.json")
        eps = episodes()
        random.Random(7).shuffle(eps)
        held = max(20, len(eps) // 20)
        streams = {"wink-valid": eps[:held], "wink-train": eps[held:]}
        for name, part in streams.items():
            ids = []
            for turns in part:
                ids += encode_episode(pc, turns)
            np.save(f"{HERE}/data/{name}.npy", np.array(ids, dtype=np.int16))
            print(f"{name}: {len(part)} episodes, {len(ids):,} pieces")


if __name__ == "__main__":
    main()
