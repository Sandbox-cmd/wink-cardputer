"""Wink's mind as a trainable network, and its export to the chip's format.

One recurrent layer (a GRU). A word piece goes in as a vector of E numbers; the state
is H numbers; the next piece is scored by projecting the state back to E numbers and
comparing it with every piece's vector (the same table is used for reading and
writing). Weights are exported as 8-bit tables; firmware/lib/winknet/winknet.c is the
one piece of code that runs them, on the chip and on this Mac.
"""
import struct

import numpy as np
import torch
import torch.nn as nn


class Mind(nn.Module):
    def __init__(self, V, E=128, H=512, drop=0.1):
        super().__init__()
        self.V, self.E, self.H = V, E, H
        self.emb = nn.Embedding(V, E)
        self.gru = nn.GRU(E, H, batch_first=True)
        self.proj = nn.Linear(H, E)
        self.bo = nn.Parameter(torch.zeros(V))
        self.drop = nn.Dropout(drop)
        nn.init.normal_(self.emb.weight, std=0.1)

    def forward(self, x, h=None):
        e = self.drop(self.emb(x))
        o, h = self.gru(e, h)
        y = self.proj(self.drop(o))
        return y @ self.emb.weight.t() + self.bo, h


def _pad4(b):
    return b + b"\0" * ((4 - len(b) % 4) % 4)


def _q8(t, clip=0.9995):
    """8-bit table with one scale: the value of one step."""
    a = t.detach().cpu().numpy().astype(np.float32)
    top = float(np.quantile(np.abs(a), clip)) or 1e-6
    s = top / 127.0
    return np.clip(np.round(a / s), -127, 127).astype(np.int8), s


def export(model, path):
    """Writes the chip's file: "WNK2", V, E, H, then
    emb_s, emb[V*E]; wi_s, wi[3H*E]; wh_s, wh[3H*H]; bi[3H], bh[3H];
    p_s, proj[E*H]; pb[E]; bo[V]   (8-bit tables padded to four bytes, the rest float32)."""
    V, E, H = model.V, model.E, model.H
    sd = {k: v for k, v in model.state_dict().items()}
    out = [b"WNK2", struct.pack("<iii", V, E, H)]
    for name in ["emb.weight", "gru.weight_ih_l0", "gru.weight_hh_l0"]:
        q, s = _q8(sd[name])
        out += [struct.pack("<f", s), _pad4(q.tobytes())]
    out += [sd["gru.bias_ih_l0"].cpu().numpy().astype("<f4").tobytes(), sd["gru.bias_hh_l0"].cpu().numpy().astype("<f4").tobytes()]
    q, s = _q8(sd["proj.weight"])
    out += [struct.pack("<f", s), _pad4(q.tobytes())]
    out += [sd["proj.bias"].cpu().numpy().astype("<f4").tobytes(), sd["bo"].cpu().numpy().astype("<f4").tobytes()]
    blob = b"".join(out)
    open(path, "wb").write(blob)
    return len(blob)
