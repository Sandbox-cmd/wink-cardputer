"""Trains Wink's mind.

  python train.py --data data/stories.npy --minutes 30 --out runs/base.pt
  python train.py --data data/wink-train.npy --mix data/stories.npy --init runs/base.pt --minutes 15 --lr 6e-4 --out runs/wink.pt

The data is one long stream of pieces. It is cut into B parallel streams that are each
read in order, and the state is carried from one batch to the next and never reset, so
the mind learns to live in an unbroken stream, the way it will on the device.
"""
import argparse
import math
import time

import numpy as np
import torch
import torch.nn.functional as F

from mindlib import Mind, export
from pieces import Pieces

p = argparse.ArgumentParser()
p.add_argument("--data", required=True)
p.add_argument("--mix", default=None, help="a second stream; every third batch comes from it, to keep general English")
p.add_argument("--valid", default=None)
p.add_argument("--init", default=None)
p.add_argument("--out", required=True)
p.add_argument("--minutes", type=float, default=20)
p.add_argument("--lr", type=float, default=2e-3)
p.add_argument("--E", type=int, default=128)
p.add_argument("--H", type=int, default=512)
p.add_argument("--B", type=int, default=64)
p.add_argument("--T", type=int, default=96)
p.add_argument("--drop", type=float, default=0.1)
args = p.parse_args()

here = __file__.rsplit("/", 1)[0]
pc = Pieces.load(here + "/pieces.json")
V = len(pc.names)
dev = "mps" if torch.backends.mps.is_available() else "cpu"


class Stream:
    def __init__(self, path, B, T):
        data = torch.from_numpy(np.load(path).astype(np.int64))
        n = (len(data) - 1) // B
        self.x = data[: n * B].view(B, n)
        self.y = data[1: n * B + 1].view(B, n)
        self.n, self.T, self.pos, self.h = n, T, 0, None

    def next(self):
        if self.pos + self.T > self.n:
            self.pos, self.h = 0, None   # the stream has been read once through
        s = slice(self.pos, self.pos + self.T)
        self.pos += self.T
        return self.x[:, s].to(dev), self.y[:, s].to(dev)


model = Mind(V, args.E, args.H, args.drop).to(dev)
if args.init:
    model.load_state_dict(torch.load(args.init, map_location=dev)["model"])
main, mix = Stream(args.data, args.B, args.T), Stream(args.mix, args.B, args.T) if args.mix else None
opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
n_params = sum(q.numel() for q in model.parameters())
print(f"{V} pieces, state of {args.H}, {n_params:,} weights, on {dev}; {main.n * args.B:,} pieces to learn from", flush=True)


def valid_loss(path):
    model.eval()
    s = Stream(path, 32, args.T)
    total, count, h = 0.0, 0, None
    with torch.no_grad():
        for _ in range(min(60, s.n // args.T)):
            x, y = s.next()
            logits, h = model(x, h)
            total += F.cross_entropy(logits.reshape(-1, V), y.reshape(-1)).item()
            count += 1
    model.train()
    return total / max(count, 1)


best, best_state = 1e9, None
start, step, run = time.time(), 0, 0.0
budget = args.minutes * 60
while time.time() - start < budget:
    src = mix if (mix and step % 3 == 2) else main
    x, y = src.next()
    frac = (time.time() - start) / budget
    for g in opt.param_groups:
        g["lr"] = args.lr * (min(1.0, step / 200) * (0.05 + 0.95 * 0.5 * (1 + math.cos(math.pi * frac))))
    logits, h = model(x, src.h)
    src.h = h.detach()
    loss = F.cross_entropy(logits.reshape(-1, V), y.reshape(-1))
    opt.zero_grad(set_to_none=True)
    loss.backward()
    torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
    opt.step()
    run = loss.item() if step == 0 else 0.98 * run + 0.02 * loss.item()
    step += 1
    if step % 200 == 0:
        note = ""
        if args.valid:   # keep the mind as it was when it did best on episodes it has never seen
            v = valid_loss(args.valid)
            note = f"  held-out {v:.3f}"
            if v < best:
                best, best_state = v, {k: t.detach().cpu().clone() for k, t in model.state_dict().items()}
                note += " (best)"
        print(f"step {step:6d}  {100 * frac:5.1f}%  loss {run:.3f}{note}", flush=True)

if best_state is not None:
    model.load_state_dict(best_state)
torch.save({"model": model.state_dict(), "E": args.E, "H": args.H, "V": V}, args.out)
size = export(model.cpu(), args.out.replace(".pt", ".bin"))
model.to(dev)
print(f"done: {step} steps, loss {run:.3f}" + (f", best held-out loss {best:.3f}" if args.valid else "") + f"; chip file {size:,} bytes", flush=True)
