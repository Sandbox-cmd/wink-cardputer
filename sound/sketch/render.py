# Render the examples VOICE.md describes, and measure them.
# Run: train/.venv/bin/python sound/sketch/render.py   (from the project folder)
import glob, math, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hum

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(os.path.dirname(HERE))
SCRATCH = os.path.join(PROJ, 'sound', 'scratch')
SAM = os.path.join(PROJ, 'bridge', 'voice-cache', 'Samantha')

# example lines: invented here to stand for what the small mind might generate
LINES = {
    'calm':     ("It is quiet in here. The good kind of quiet.", 'calm'),
    'question': ("Did you sleep at all, or is it coffee again?", 'curious'),
    'cheeky':   ("I saw that. I will say nothing, loudly.", 'cheeky'),
    'sleepy':   ("It is late. The numbers can wait until morning.", 'sleepy'),
    'long':     ("Most stars you can see tonight are bigger than the sun, and none of them is in a hurry.", 'calm'),
}
MOOD_LINE = "The kettle has gone quiet. Tea, then?"

def ulaw(path):
    b = np.fromfile(path, dtype=np.uint8)
    u = ~b; s = u & 0x80; e = (u >> 4) & 7; m = u & 0x0f
    x = (((m.astype(np.int32) << 3) + 0x84) << e) - 0x84
    return (np.where(s, -x, x) / 32768.0).astype(np.float32)

def samantha_ref():
    fs = sorted(glob.glob(os.path.join(SAM, '*.ul')))[:40]
    full, spk = [], []
    for f in fs:
        x = ulaw(f)
        full.append(hum.active_rms_db(x)); spk.append(hum.active_rms_db(hum.speaker(x).astype(np.float32)))
    return float(np.median(full)), float(np.median(spk))

def measure(name, x, ev=None):
    pk = float(np.abs(x).max())
    x16 = np.round(x * 32767)
    clipped = int((np.abs(x16) >= 32767).sum())
    sp = hum.speaker(x).astype(np.float32)
    b = hum.bands(x)
    m = hum.mouth(x)
    row = dict(name=name, dur=len(x) / hum.SR, peak_db=20 * math.log10(pk + 1e-12), clipped=clipped,
               rms_db=hum.active_rms_db(x), spk_rms_db=hum.active_rms_db(sp),
               lo=b[0], mid_lo=b[1], mid=b[2], hi=b[3], top=b[4])
    if ev is not None:
        tr = hum.pitch_track(sp)
        pl = hum.planned_f0(ev, len(tr))
        both = (tr > 0) & (pl > 0)
        err = 12 * np.log2(tr[both] / pl[both])
        row['pitch_frames'] = int(both.sum())
        row['pitch_within_1st'] = float((np.abs(err) < 1.0).mean()) if both.any() else 0.0
        row['pitch_octave_err'] = float((np.abs(np.abs(err) - 12) < 1.5).mean()) if both.any() else 0.0
        # the loudest syllable: is it the nuclear accent of the first sentence?
        loud = []
        for e in ev:
            a = int(e['t'] * hum.SR / 1000); z = int((e['t'] + e['ms']) * hum.SR / 1000)
            loud.append(float(np.abs(x[a:z]).max()) if z > a else 0.0)
        row['loudest_event'] = int(np.argmax(loud))
        # the mouth: how far it closes in the gaps between words
        gaps = []
        for e0, e1 in zip(ev, ev[1:]):
            g0 = e0['t'] + e0['ms'] + 25; g1 = e1['t']
            if g1 - g0 >= 15:
                a = int(g0 / 10); z = int(g1 / 10)
                if z > a: gaps.append(float(m[a:z].min()))
        row['mouth_gap_max'] = max(gaps) if gaps else None
        row['mouth_peak'] = float(m.max())
        row['syll_per_s'] = len(ev) / (ev[-1]['t'] / 1000 + ev[-1]['ms'] / 1000)
    return row

def spectro(name):
    w = os.path.join(SCRATCH, name + '.wav'); png = os.path.join(SCRATCH, name + '-spec.png')
    wav = os.path.join(SCRATCH, name + '-wave.png')
    os.system(f'ffmpeg -hide_banner -loglevel error -y -i "{w}" -lavfi "showspectrumpic=s=1000x300:legend=1:scale=log:fscale=lin:color=intensity" "{png}"')
    os.system(f'ffmpeg -hide_banner -loglevel error -y -i "{w}" -lavfi "showwavespic=s=1000x160:split_channels=0" "{wav}"')

def save(name, x, out_dir):
    hum.write_wav(os.path.join(out_dir, name + '.wav'), x)
    if out_dir != SCRATCH: hum.write_wav(os.path.join(SCRATCH, name + '.wav'), x)
    spectro(name)

def main():
    os.makedirs(SCRATCH, exist_ok=True)
    rows = []
    sam_full, sam_spk = samantha_ref()
    print(f'Samantha (40 lines): active RMS {sam_full:.1f} dBFS; through the speaker model {sam_spk:.1f} dBFS')
    texts = {}
    for name, (text, mood) in LINES.items():
        ev, end = hum.plan(text, mood, start_ms=60)
        x = hum.render(ev, end + 200, seed=hum.h32(text))
        out = HERE if name in ('calm', 'question', 'cheeky', 'sleepy') else SCRATCH
        save(name, x, out)
        r = measure(name, x, ev); r['mood'] = mood; rows.append(r)
        texts[name] = (text, ev)
    # the eleven moods on one line
    allx = []
    for mood in hum.MOODS:
        ev, end = hum.plan(MOOD_LINE, mood, start_ms=60)
        x = hum.render(ev, end + 150, seed=hum.h32(MOOD_LINE))
        r = measure('mood:' + mood, x, ev); r['mood'] = mood
        st = []
        for e in ev: st += [e['p0'], e['p1']]
        r['centre_st'] = float(np.median(st)); r['range_st'] = float(np.percentile(st, 95) - np.percentile(st, 5))
        sp = hum.speaker(x)
        X = np.abs(np.fft.rfft(sp)) ** 2; fq = np.fft.rfftfreq(len(sp), 1 / hum.SR)
        r['centroid_hz'] = float((X * fq).sum() / X.sum())
        rows.append(r); allx += [x, np.zeros(int(0.5 * hum.SR), np.float32)]
    save('moods', np.concatenate(allx), SCRATCH)
    # transitions: the hum opens into Samantha; and it closes back into the hum
    g = hum.gesture_open(60)
    gx = hum.render(g, 60 + 200 + 330 + 150, seed=7)
    sam = ulaw(os.path.join(SAM, '210a481f.ul'))      # "That's the Mac. Now we can really talk."
    seam = np.concatenate([gx, np.zeros(int(0.25 * hum.SR), np.float32), sam])
    save('open', seam, HERE)
    rows.append(measure('open-gesture', gx, g))
    sam2 = ulaw(os.path.join(SAM, 'afca6d76.ul'))     # "The link is down..." (what plays today)
    c = hum.gesture_close(60)
    text = "Just me again. Ask away."
    ev, end = hum.plan(text, 'calm', start_ms=60 + 420 + 90 + 200)
    cx = hum.render(c + ev, end + 200, seed=hum.h32(text))
    save('close', np.concatenate([sam2, np.zeros(int(0.6 * hum.SR), np.float32), cx]), HERE)
    rows.append(measure('close+line', cx, c + ev))
    # silent start, sound taken in mid-way: the volume comes up during word 5; the hum
    # joins at the next word's onset, never inside a syllable
    text, mood = LINES['calm']
    ev, end = hum.plan(text, mood, start_ms=60)
    base = hum.render(ev, end + 200, seed=hum.h32(text))
    joined = [dict(e) for e in ev]
    first = next(i for i, e in enumerate(joined) if i >= 5 and e['word_start'])
    for e in joined[:first]: e['amp'] = 0.0
    jx = hum.render(joined, end + 200, seed=hum.h32(text))
    hum.write_wav(os.path.join(SCRATCH, 'join.wav'), jx)
    hx = hum.render(ev, end + 200, seed=hum.h32(text), hush_ms=ev[2]['t'] + ev[2]['ms'] * 0.5)
    hum.write_wav(os.path.join(SCRATCH, 'hush.wav'), hx)
    step = lambda x: float(np.abs(np.diff(x)).max())
    js = int(joined[first]['t'] * hum.SR / 1000)
    hs = int((ev[2]['t'] + ev[2]['ms'] * 0.5) * hum.SR / 1000)
    pre_join = float(np.abs(jx[:js]).max())
    hush_tail = float(np.abs(hx[hs + int(0.030 * hum.SR):]).max())
    lines_extra = [f'join: silent before the join max={pre_join:.6f}; largest step at join {step(jx[js:js+800]):.4f} vs largest step anywhere in the normal render {step(base):.4f}',
                   f'hush: largest step after hush {step(hx[hs:hs+800]):.4f}; max 30 ms after hush {hush_tail:.6f}']
    # copies through the speaker model, to judge balance on a Mac's speakers
    dev = os.path.join(HERE, 'as-device'); os.makedirs(dev, exist_ok=True)
    ys = {}
    for name in ('calm', 'question', 'cheeky', 'sleepy', 'open', 'close'):
        src = os.path.join(SCRATCH, name + '.wav')
        raw = np.frombuffer(open(src, 'rb').read()[44:], dtype='<i2').astype(np.float32) / 32768
        ys[name] = hum.speaker(raw).astype(np.float32)
    # one gain for every file, so the balance between them is kept: the loudest at -1 dBFS, at most +6 dB
    g = min(10 ** (6 / 20), 10 ** (-1 / 20) / max(float(np.abs(y).max()) for y in ys.values()))
    for name, y in ys.items():
        hum.write_wav(os.path.join(dev, name + '.wav'), y * g)
    lines_extra.append(f'as-device copies: speaker model, then one gain of {20 * math.log10(g):+.1f} dB for all six')
    # report
    keys =['name', 'mood', 'dur', 'peak_db', 'clipped', 'rms_db', 'spk_rms_db', 'lo', 'mid_lo', 'mid', 'hi', 'top',
            'pitch_within_1st', 'pitch_octave_err', 'loudest_event', 'mouth_gap_max', 'mouth_peak', 'syll_per_s',
            'centre_st', 'range_st', 'centroid_hz']
    lines = []
    for r in rows:
        lines.append('  '.join(f'{k}={r[k]:.3f}' if isinstance(r.get(k), float) else f'{k}={r.get(k)}' for k in keys if k in r))
    lines.insert(0, f'samantha_rms_db={sam_full:.2f} samantha_spk_rms_db={sam_spk:.2f}')
    lines += lines_extra
    for name, (text, ev) in texts.items():
        lines.append(f'{name} events: ' + ' | '.join(f"{e['t']:.0f}+{e['ms']:.0f} {e['p0']:.1f}>{e['p1']:.1f} a{e['amp']:.2f}" for e in ev))
    with open(os.path.join(SCRATCH, 'measure.txt'), 'w') as f: f.write('\n'.join(lines) + '\n')
    print('\n'.join(lines[:len(rows) + 1]))

if __name__ == '__main__':
    main()
