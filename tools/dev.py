#!/usr/bin/env python3
"""Eyes and hands for the Cardputer over its USB cable.

Run with PlatformIO's Python (it has pyserial):
  ~/.platformio/penv/bin/python tools/dev.py <step> [<step> ...]

Steps, run in order on one open connection:
  cmd:<line>        send a console line, print the reply up to its "#ok"/"#err"
  shot:<file.png>   ask for the screen and save it (3x enlarged) as a PNG
  wait:<ms>         pause
  mon:<seconds>     print whatever the device says for that long
  type:<text>       shorthand for cmd:key <text>
  put:<local>:<remote>   send a file to the device (remote under /sd is the memory card)
"""
import base64, struct, sys, time, zlib, glob, os

import serial  # from PlatformIO's environment

SCALE = 3


def find_port():
    env = os.environ.get("CARDPUTER_PORT")
    if env:
        return env
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port: is the Cardputer plugged in and switched on?")
    return ports[0]


def open_port():
    s = serial.Serial()
    s.port = find_port()
    s.baudrate = 115200
    s.timeout = 0.2
    # The chip's USB port treats "DTR low while RTS high" as a reset. Opening raises both,
    # which is harmless; they are then lowered RTS first so that state never occurs.
    s.open()
    try:
        import termios
        attrs = termios.tcgetattr(s.fd)
        attrs[2] &= ~termios.HUPCL  # do not drop the lines (and reset) on close
        termios.tcsetattr(s.fd, termios.TCSANOW, attrs)
    except Exception:
        pass
    s.rts = False
    s.dtr = False
    return s


def read_until_done(s, timeout=8.0, echo=True):
    """Collect lines until one starts with #ok or #err."""
    out, buf, end = [], b"", time.time() + timeout
    while time.time() < end:
        chunk = s.read(4096)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").rstrip("\r")
            out.append(text)
            if text.startswith("#ok") or text.startswith("#err"):
                if echo:
                    print("\n".join(out))
                return out
    if echo:
        print("\n".join(out))
        print("(timed out waiting for #ok)")
    return out


def png(path, w, h, rgb):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def shot(s, path):
    s.reset_input_buffer()
    s.write(b"shot\n")
    lines = read_until_done(s, timeout=10, echo=False)
    w = h = 0
    b64 = []
    grabbing = False
    for ln in lines:
        if ln.startswith("#shot "):
            _, ws, hs = ln.split()[:3]
            w, h = int(ws), int(hs)
            grabbing = True
        elif ln.startswith("#"):
            grabbing = False
        elif grabbing:
            b64.append(ln.strip())
    if not w:
        print("no screenshot came back:", lines[-3:])
        return
    data = base64.b64decode("".join(b64))
    if len(data) < w * h * 2:
        print(f"short screenshot: {len(data)} of {w*h*2} bytes")
        return
    rgb = bytearray(w * SCALE * h * SCALE * 3)
    for y in range(h):
        row = bytearray(w * SCALE * 3)
        for x in range(w):
            v = (data[(y * w + x) * 2] << 8) | data[(y * w + x) * 2 + 1]
            r = ((v >> 11) & 31) * 255 // 31
            g = ((v >> 5) & 63) * 255 // 63
            b = (v & 31) * 255 // 31
            for k in range(SCALE):
                o = (x * SCALE + k) * 3
                row[o], row[o + 1], row[o + 2] = r, g, b
        for k in range(SCALE):
            o = (y * SCALE + k) * w * SCALE * 3
            rgb[o:o + len(row)] = row
    png(path, w * SCALE, h * SCALE, bytes(rgb))
    print(f"saved {path} ({w}x{h}, shown {SCALE}x)")


def wait_line(s, timeout=10.0):
    buf, end = b"", time.time() + timeout
    while time.time() < end:
        c = s.read(1)
        if not c:
            continue
        if c == b"\n":
            line = buf.decode("utf-8", "replace").strip()
            if line.startswith("#"):
                return line
            buf = b""
        else:
            buf += c
    return ""


def put(s, local, remote):
    data = open(local, "rb").read()
    s.reset_input_buffer()
    s.write(f"put {remote} {len(data)}\n".encode())
    first = wait_line(s)
    if first != "#ready":
        print("the device would not take the file:", first)
        return
    t0, sent = time.time(), 0
    while sent < len(data):
        part = data[sent:sent + 2048]
        s.write(part)
        if wait_line(s) != "#k":
            print(f"stopped at {sent} of {len(data)} bytes")
            return
        sent += len(part)
    print(f"sent {local} -> {remote}: {len(data)} bytes in {time.time() - t0:.0f}s, device says {wait_line(s)}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    s = open_port()
    time.sleep(0.15)
    s.reset_input_buffer()
    for step in sys.argv[1:]:
        kind, _, arg = step.partition(":")
        if kind == "cmd":
            s.write(arg.encode() + b"\n")
            read_until_done(s)
        elif kind == "type":
            s.write(b"key " + arg.encode() + b"\n")
            read_until_done(s, echo=False)
        elif kind == "shot":
            shot(s, arg)
        elif kind == "put":
            local, _, remote = arg.partition(":")
            put(s, local, remote)
        elif kind == "wait":
            time.sleep(int(arg) / 1000)
        elif kind == "mon":
            end = time.time() + float(arg)
            while time.time() < end:
                d = s.read(4096)
                if d:
                    sys.stdout.write(d.decode("utf-8", "replace"))
                    sys.stdout.flush()
        else:
            print("unknown step", step)
    s.close()


if __name__ == "__main__":
    main()
