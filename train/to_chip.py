"""Puts a trained mind where the firmware build picks it up.

  python to_chip.py runs/wink.bin

Writes firmware/model/wink.bin (the weights) and firmware/model/pieces.bin (the pieces
it reads and writes): "WPC1", count u16, longest u16, then offsets u16[count+1] into the
text that follows, then the ids sorted by their text u16[count], then the text itself.
"""
import shutil
import struct
import sys

from pieces import Pieces

here = __file__.rsplit("/", 1)[0]
pc = Pieces.load(here + "/pieces.json")
names = [n.encode("ascii") for n in pc.names]
offsets, text = [0], b""
for n in names:
    text += n
    offsets.append(len(text))
order = sorted(range(len(names)), key=lambda i: names[i])
blob = b"WPC1" + struct.pack("<HH", len(names), pc.longest) + struct.pack(f"<{len(offsets)}H", *offsets) + struct.pack(f"<{len(order)}H", *order) + text
open(here + "/../firmware/model/pieces.bin", "wb").write(blob)
shutil.copyfile(sys.argv[1], here + "/../firmware/model/wink.bin")
print(f"{len(names)} pieces ({len(blob):,} bytes) and {sys.argv[1]} -> firmware/model/")
