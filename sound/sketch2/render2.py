# Render the version 2 hum (sound/sketch/hum.py) into sound/sketch2/, and measure it.
# Never writes into sound/sketch/ or sound/scratch/, which hold the approved version 1.
# Run: train/.venv/bin/python -B sound/sketch2/render2.py   (from the project folder)
import glob, math, os, sys
sys.dont_write_bytecode = True
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
SOUND = os.path.dirname(HERE)
PROJ = os.path.dirname(SOUND)
sys.path.insert(0, os.path.join(SOUND, 'sketch'))
import hum
from render import LINES, MOOD_LINE, ulaw     # the same example lines as version 1

PICS = os.path.join(HERE, 'pics')
DEV = os.path.join(HERE, 'as-device')
SAM = os.path.join(PROJ, 'bridge', 'voice-cache', 'Samantha')
V1 = os.path.join(SOUND, 'sketch')
V1_MOODS = os.path.join(SOUND, 'scratch', 'measure.txt')

def pitch_track(x, hop=160, win=640, fmin=180.0, fmax=1000.0):
    """compare.py's tracker: the first autocorrelation peak within 90% of the best"""
    SR = hum.SR; out = []
    lo, hi = int(SR / fmax), int(SR / fmin) + 1
    w = np.hanning(win)
    for s in range(0, len(x) - win, hop):
        fr = x[s:s + win].astype(np.float64) * w
        if np.sqrt((fr ** 2).mean()) < 10 ** (-42 / 20): out.append(0.0); continue
        ac = np.fft.irfft(np.abs(np.fft.rfft(fr, 2 * win)) ** 2)[:win]
        seg = ac[lo:hi]; best = seg.max()
        if best < 0.3 * ac[0]: out.append(0.0); continue
        k = next((j for j in range(1, len(seg) - 1) if seg[j] >= 0.9 * best and seg[j] >= seg[j - 1] and seg[j] >= seg[j + 1]), int(np.argmax(seg)))
        out.append(SR / (lo + k))
    return np.array(out)

def measure(name, x, ev=None):
    sp = hum.speaker(x).astype(np.float32)
    b = hum.bands(x)
    row = dict(name=name, dur=len(x) / hum.SR, peak_db=20 * math.log10(float(np.abs(x).max()) + 1e-12),
               clipped=int((np.abs(np.round(x * 32767)) >= 32767).sum()),
               spk_rms_db=hum.active_rms_db(sp), lo=b[0], mid=b[2], hi=b[3] + b[4])
    X = np.abs(np.fft.rfft(sp)) ** 2; fq = np.fft.rfftfreq(len(sp), 1 / hum.SR)
    row['centroid_hz'] = float((X * fq).sum() / X.sum())
    if ev is not None:
        tr = pitch_track(sp); pl = hum.planned_f0(ev, len(tr))
        both = (tr > 0) & (pl > 0)
        err = 12 * np.log2(tr[both] / pl[both])
        row['pitch_within_1st'] = float((np.abs(err) < 1.0).mean()) if both.any() else 0.0
        loud = []
        for e in ev:
            a = int(e['t'] * hum.SR / 1000); z = int((e['t'] + e['ms']) * hum.SR / 1000)
            loud.append(float(np.abs(x[a:z]).max()) if z > a else 0.0)
        row['loudest_event'] = int(np.argmax(loud))
        st = [v for e in ev for v in (e['p0'], e['p1'])]
        row['centre_st'] = float(np.median(st)); row['max_st'] = float(max(st))
        row['max_hz'] = hum.C4 * 2 ** (row['max_st'] / 12)
        row['syll_per_s'] = len(ev) / (ev[-1]['t'] / 1000 + ev[-1]['ms'] / 1000)   # as version 1's render.py counts it
        row['chirps'] = sum(1 for e in ev if e.get('console') == 0.5)
    return row

def spectro(wav, png):
    os.system(f'ffmpeg -hide_banner -loglevel error -y -i "{wav}" -lavfi "showspectrumpic=s=1000x300:legend=1:scale=log:fscale=lin:color=intensity" "{png}"')

def save(name, x, pic=True):
    p = os.path.join(HERE, name + '.wav')
    hum.write_wav(p, x)
    if pic: spectro(p, os.path.join(PICS, name + '-spec.png'))
    return p

def read16(path):
    return np.frombuffer(open(path, 'rb').read()[44:], dtype='<i2').astype(np.float32) / 32768

def main():
    os.makedirs(PICS, exist_ok=True); os.makedirs(DEV, exist_ok=True)
    fs = sorted(glob.glob(os.path.join(SAM, '*.ul')))[:40]
    sam_spk = float(np.median([hum.active_rms_db(hum.speaker(ulaw(f)).astype(np.float32)) for f in fs]))
    rows = []
    gap = np.zeros(int(0.6 * hum.SR), np.float32)
    for name in ('calm', 'question', 'cheeky', 'sleepy'):
        text, mood = LINES[name]
        ev, end = hum.plan(text, mood, start_ms=60)
        x = hum.render(ev, end + 200, seed=hum.h32(text))
        save(name, x)
        r = measure(name, x, ev); r['mood'] = mood; rows.append(r)
        # version 1 then version 2, for hearing the move in one file
        save('v1-then-v2-' + name, np.concatenate([read16(os.path.join(V1, name + '.wav')), gap, x]), pic=False)
    allx = []
    for mood in hum.MOODS:
        ev, end = hum.plan(MOOD_LINE, mood, start_ms=60)
        x = hum.render(ev, end + 150, seed=hum.h32(MOOD_LINE))
        save('mood-' + mood, x, pic=False)
        r = measure('mood:' + mood, x, ev); r['mood'] = mood; rows.append(r)
        allx += [x, np.zeros(int(0.5 * hum.SR), np.float32)]
    save('moods', np.concatenate(allx))
    g = hum.gesture_open(60)
    gx = hum.render(g, 60 + 200 + 330 + 150, seed=7)
    sam = ulaw(os.path.join(SAM, '210a481f.ul'))      # "That's the Mac. Now we can really talk."
    save('open', np.concatenate([gx, np.zeros(int(0.25 * hum.SR), np.float32), sam]))
    rows.append(measure('open-gesture', gx))
    sam2 = ulaw(os.path.join(SAM, 'afca6d76.ul'))     # "The link is down..."
    c = hum.gesture_close(60)
    text = "Just me again. Ask away."
    ev, end = hum.plan(text, 'calm', start_ms=60 + 420 + 90 + 200)
    cx = hum.render(c + ev, end + 200, seed=hum.h32(text))
    save('close', np.concatenate([sam2, np.zeros(int(0.6 * hum.SR), np.float32), cx]))
    rows.append(measure('close+line', cx))
    # hush and joining mid-reply, as version 1 checked them
    text, mood = LINES['calm']
    ev, end = hum.plan(text, mood, start_ms=60)
    base = hum.render(ev, end + 200, seed=hum.h32(text))
    hs_ms = ev[2]['t'] + ev[2]['ms'] * 0.5
    hx = hum.render(ev, end + 200, seed=hum.h32(text), hush_ms=hs_ms)
    hs = int(hs_ms * hum.SR / 1000)
    step = lambda x: float(np.abs(np.diff(x)).max())
    extra = [f'Samantha (40 cached lines) through the speaker model: {sam_spk:.2f} dBFS',
             f'hush: largest step after hush {step(hx[hs:hs + 800]):.4f}; loudest 30 ms after {20 * math.log10(float(np.abs(hx[hs + 480:]).max()) + 1e-12):.1f} dBFS; largest step in the plain render {step(base):.4f}']
    # copies through the speaker model, one gain for all
    ys = {n: hum.speaker(read16(os.path.join(HERE, n + '.wav'))).astype(np.float32) for n in ('calm', 'question', 'cheeky', 'sleepy', 'open', 'close')}
    gdev = min(10 ** (6 / 20), 10 ** (-1 / 20) / max(float(np.abs(y).max()) for y in ys.values()))
    for n, y in ys.items(): hum.write_wav(os.path.join(DEV, n + '.wav'), y * gdev)
    extra.append(f'as-device copies: speaker model, then one gain of {20 * math.log10(gdev):+.1f} dB for all six')
    keys = ['name', 'mood', 'dur', 'peak_db', 'clipped', 'spk_rms_db', 'lo', 'mid', 'hi', 'centroid_hz', 'pitch_within_1st',
            'loudest_event', 'centre_st', 'max_st', 'max_hz', 'syll_per_s', 'chirps']
    L = ['  '.join(f'{k}={r[k]:.3f}' if isinstance(r.get(k), float) else f'{k}={r.get(k)}' for k in keys if k in r) for r in rows]
    L += extra
    out = '\n'.join(L) + '\n'
    with open(os.path.join(HERE, 'measure.txt'), 'w') as f: f.write(out)
    print(out)

if __name__ == '__main__':
    main()
