# Check the C port of the hum (firmware/lib/hum) against its reference (sound/sketch/hum.py,
# version 2 since 17 November 2026). Builds the harness, renders every case both ways, and
# measures. Version 1 of this file, which checked against the first approved WAVs in
# sound/sketch/, is kept in sound/port/v1/compare.py.
#
# Run from anywhere:
#   train/.venv/bin/python -B sound/port/compare.py
# Writes only inside sound/port/scratch/ (renders, the Python cache, results.md).
import hashlib, math, os, subprocess, sys
sys.dont_write_bytecode = True
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SOUND = os.path.dirname(HERE)
PROJ = os.path.dirname(SOUND)
SKETCH = os.path.join(SOUND, 'sketch')
SKETCH2 = os.path.join(SOUND, 'sketch2')    # version 2's renders (render2.py, start_ms = 60)
LIB = os.path.join(PROJ, 'firmware', 'lib', 'hum')
SCRATCH = os.path.join(HERE, 'scratch')
BIN = os.path.join(SCRATCH, 'humwav')
sys.path.insert(0, SKETCH)
import hum                         # the reference
from render import LINES, MOOD_LINE   # the designer's example lines (main() is never called)

SR = hum.SR

# one long two-sentence reply per mood, written to reach the habits, digits, apostrophes,
# '!', '...', a wh-question and a yes/no question
LONG = {
    'calm':      "Most stars you can see tonight are bigger than the sun, and none of them is in a hurry. We can watch them for a while before bed.",
    'happy':     "You remembered my name today! That makes the whole morning feel brighter, and I want to hum about it.",
    'delighted': "Oh, the parcel came at last! I have been waiting for it since Tuesday, and it is even better than I hoped.",
    'curious':   "What is that small blue light on the shelf? It blinks every 3 seconds, as if it wants to tell us something.",
    'thinking':  "Hmm, the numbers do not add up yet. Let me count them again, slowly, and see where the 12 went...",
    'sleepy':    "It is very late now, and the house has gone quiet. We can finish the rest of the list tomorrow morning.",
    'surprised': "Wait, you finished the whole book already? I thought it would take you another month at least!",
    'sad':       "I'm sorry the plants didn't make it through the cold week. You looked after them carefully, and that still counts.",
    'cheeky':    "I saw you take the last biscuit, don't pretend. I will not tell anyone, but I will remember it forever.",
    'tender':    "You've done enough for today, and it's alright to rest. I'll keep the light on for a little while.",
    'alert':     "The battery is down to 15 percent, so plug me in soon. Is the charger still in the drawer by the door?",
}
FOUR = ("It rained all morning. Then the sun came out, slowly. The cat went to the window and sat there. "
        "I think it liked the warmth, and so did I.")

def build():
    os.makedirs(SCRATCH, exist_ok=True)
    cmd = ['cc', '-O2', '-std=c99', '-Wall', '-Wextra', '-Wdouble-promotion', '-I', LIB,
           os.path.join(HERE, 'humwav.c'), os.path.join(LIB, 'hum.c'), '-lm', '-o', BIN]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or r.stderr.strip():
        print(r.stderr); sys.exit('build failed or warned')

def read_wav(path):
    b = open(path, 'rb').read()
    return np.frombuffer(b[44:], dtype='<i2').astype(np.float32) / 32767.0

def run_c(name, text, mood, hush=None):
    wav = os.path.join(SCRATCH, f'c_{name}.wav'); ev = os.path.join(SCRATCH, f'c_{name}.ev'); sh = os.path.join(SCRATCH, f'c_{name}.shown')
    args = [BIN, '-s', sh] + (['-e', ev] if mood not in ('open', 'close') else []) + (['-H', str(hush)] if hush is not None else []) + [text, mood, wav]
    subprocess.run(args, check=True)
    shown = np.loadtxt(sh, ndmin=2)
    evs = np.loadtxt(ev, ndmin=2) if mood not in ('open', 'close') else None
    return read_wav(wav), evs, shown

REF_SRC = hashlib.sha1(open(os.path.join(SKETCH, 'hum.py'), 'rb').read()).hexdigest()[:10]

def py_case(name, text, mood, hush=None):
    """the reference's plan and render, from t = 0 (as the C starts), cached"""
    if mood == 'open':
        ev, end, seed = hum.gesture_open(0.0), 200 + 330 + 150.0, 7
    elif mood == 'close':
        ev, end, seed = hum.gesture_close(0.0), 420 + 150.0, 7
    else:
        ev, end = hum.plan(text, mood, start_ms=0.0)
        seed = hum.h32(text)
    key = hashlib.sha1(f'{REF_SRC}|{text}|{mood}|{hush}'.encode()).hexdigest()[:12]
    cache = os.path.join(SCRATCH, f'py_{name}_{key}.npy')
    if os.path.exists(cache):
        x = np.load(cache)
    else:
        x = hum.render(ev, end + 200, seed=seed, hush_ms=None if hush is None else hush / 16.0)
        np.save(cache, x)
    return ev, end, x

# ------------------------------------------------------------------ measures
def frames(x, n=320):
    m = len(x) // n
    return x[:m * n].reshape(m, n)

def envelope(x):                       # 20 ms windows
    return np.sqrt((frames(x).astype(np.float64) ** 2).mean(1))

def pitch_track(x, hop=160, win=640, fmin=180.0, fmax=1000.0):
    """autocorrelation per 40 ms frame; the first peak within 90% of the best, parabolic"""
    out = []
    lo, hi = int(SR / fmax), int(SR / fmin) + 1
    w = np.hanning(win)
    for s in range(0, len(x) - win, hop):
        fr = x[s:s + win].astype(np.float64) * w
        if np.sqrt((fr ** 2).mean()) < 10 ** (-42 / 20):
            out.append(0.0); continue
        F = np.fft.rfft(fr, 2 * win)
        ac = np.fft.irfft(np.abs(F) ** 2)[:win]
        seg = ac[lo:hi]
        best = seg.max()
        if best < 0.3 * ac[0]:
            out.append(0.0); continue
        # first local maximum reaching 90% of the best: avoids picking a multiple of the period
        k = None
        for j in range(1, len(seg) - 1):
            if seg[j] >= 0.9 * best and seg[j] >= seg[j - 1] and seg[j] >= seg[j + 1]:
                k = j; break
        if k is None: k = int(np.argmax(seg))
        if 0 < k < len(seg) - 1:
            a, b, c = seg[k - 1], seg[k], seg[k + 1]
            d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        else:
            d = 0.0
        out.append(SR / (lo + k + d))
    return np.array(out)

def active_extent(x, thr=1e-3):
    idx = np.nonzero(np.abs(x) > thr)[0]
    return (idx[0], idx[-1]) if len(idx) else (0, 0)

def planned_reveal(ev, text_len, block_ends):
    starts = [(math.ceil(e['t'] * 16 - 1e-9), e.get('reveal_end', e.get('reveal'))) for e in ev]
    out = []
    for S in block_ends:
        shown = 0
        for st, rv in starts:
            if st <= S - 1 and rv is not None: shown = max(shown, rv)
        out.append(shown)
    return out

def plan_diff(ev, cev):
    """largest difference per field between hum.plan and the C planner"""
    if cev is None: return None
    if len(cev) != len(ev): return dict(count=(len(ev), len(cev)))
    cols = dict(t=0, ms=1, p0=2, p1=3, amp=4, fc=5, attack=6, release=7, floor=8, hard=9, scoop=10, reveal=11, word_start=12, vib=13, vib_hz=14, breath=15, console=18)
    d = {}
    for f, c in cols.items():
        py = []
        for e in ev:
            if f == 'reveal': v = e.get('reveal_end', e.get('reveal')); v = -1 if v is None else v
            elif f == 'floor': v = e.get('floor', 0.0)
            elif f == 'release': v = e.get('release', 30.0)
            elif f == 'hard' or f == 'word_start': v = 1.0 if e[f] else 0.0
            else: v = e[f]
            py.append(float(v))
        d[f] = float(np.abs(np.array(py) - cev[:, c]).max())
    return d

def compare(name, text, mood, lead_ms=0.0, ref=None, hush=None):
    ev, end, px = py_case(name, text, mood, hush)
    cx, cev, shown = run_c(name, text, mood, hush)
    if ref is not None:                # a version 2 WAV from sketch2/, made with start_ms = 60
        px = ref
        cx = np.concatenate([np.zeros(int(lead_ms * SR / 1000), np.float32), cx])
    row = dict(name=name, mood=mood, text=text)
    row['len_c_ms'] = len(cx) * 1000 / SR
    row['planned_end_ms'] = end + lead_ms
    if mood in ('open', 'close') or hush is not None:
        row['dur_diff_ms'] = None      # a gesture has no planned end; see the active extent
    else:
        row['dur_diff_ms'] = row['len_c_ms'] - row['planned_end_ms'] if ref is None else None
    a0, a1 = active_extent(px); b0, b1 = active_extent(cx)
    row['active_py_ms'] = (a1 - a0) * 1000 / SR; row['active_c_ms'] = (b1 - b0) * 1000 / SR
    row['active_diff_ms'] = row['active_c_ms'] - row['active_py_ms']
    n = min(len(px), len(cx))
    p, c = px[:n], cx[:n]
    ep, ec = envelope(p), envelope(c)
    row['env_corr'] = float(np.corrcoef(ep, ec)[0, 1])
    tp, tc = pitch_track(p), pitch_track(c)
    both = (tp > 0) & (tc > 0)
    err = 12 * np.log2(tc[both] / tp[both])
    row['pitch_frames'] = int(both.sum())
    row['pitch_mad_st'] = float(np.abs(err).mean()) if both.any() else float('nan')
    row['pitch_within_half'] = float((np.abs(err) < 0.5).mean()) if both.any() else float('nan')
    row['peak_py_db'] = 20 * math.log10(float(np.abs(px).max()) + 1e-12)
    row['peak_c_db'] = 20 * math.log10(float(np.abs(cx).max()) + 1e-12)
    row['peak_diff_db'] = row['peak_c_db'] - row['peak_py_db']
    row['clip_c'] = int((np.abs(np.round(cx * 32767)) >= 32767).sum())
    row['clip_py'] = int((np.abs(np.round(px * 32767)) >= 32767).sum())
    row['residual_db'] = 20 * math.log10(np.sqrt(((p - c).astype(np.float64) ** 2).mean()) / np.sqrt((p.astype(np.float64) ** 2).mean()))
    if ref is None:
        row['plan'] = plan_diff(ev, cev)
        if mood not in ('open', 'close') and hush is None:
            exp = planned_reveal(ev, len(text), shown[:, 0].astype(int))
            got = shown[:, 1].astype(int)
            # the last block: the C shows all of the text once finished
            exp[-1] = len(text)
            row['reveal_mismatch'] = int((np.array(exp) != got).sum())
            row['reveal_blocks'] = len(got)
    row['pass'] = ((row['dur_diff_ms'] is None or abs(row['dur_diff_ms']) <= 20) and abs(row['active_diff_ms']) <= 20
                   and row['env_corr'] >= 0.95 and row['pitch_mad_st'] <= 0.5 and abs(row['peak_diff_db']) <= 1.5
                   and row['clip_c'] == 0)
    return row

def speed():
    text, mood = LONG['sad'], 'sad'
    r = subprocess.run([BIN, '-b', '200', text, mood, os.path.join(SCRATCH, 'bench.wav')], capture_output=True, text=True, check=True)
    z = subprocess.run([BIN, '-z'], capture_output=True, text=True, check=True)
    return r.stdout.strip(), z.stdout.strip()

def fmt(v, f='{:.3f}'):
    return '-' if v is None else f.format(v)

def main():
    build()
    cases = []
    for name in ('calm', 'question', 'cheeky', 'sleepy'):
        text, mood = LINES[name]
        cases.append((name, text, mood))
    for mood, text in LONG.items():
        assert len(text.encode()) <= 240, mood
        cases.append(('long-' + mood, text, mood))
    for mood in hum.MOODS:             # v2: the mood line in every mood (delighted's chirp is in it)
        cases.append(('line-' + mood, MOOD_LINE, mood))
    cases += [('open', '', 'open'), ('close', '', 'close')]
    rows = []
    for name, text, mood in cases:
        print('...', name, flush=True)
        rows.append(compare(name, text, mood))
    # four sentences: the capped sentence drop (pitch -1.8 st, -3 dB) through the render
    rows.append(compare('four-sentences', FOUR, 'tender'))
    # hush halfway through the third pulse of the calm line, at the block boundary after it
    text, mood = LINES['calm']
    ev, _ = hum.plan(text, mood, start_ms=0.0)
    at = int(math.ceil((ev[2]['t'] + ev[2]['ms'] * 0.5) * 16 / 512)) * 512
    r = compare('hush', text, mood, hush=at); r['mood'] = f'calm, hush at {at / 16:.0f} ms'
    n0 = at + int(0.030 * SR)
    cx = read_wav(os.path.join(SCRATCH, 'c_hush.wav'))
    r['name'] = 'hush'
    r['hush_tail_c'] = 20 * math.log10(float(np.abs(cx[n0:]).max()) + 1e-12) if len(cx) > n0 else -120.0
    r['hush_step_c'] = float(np.abs(np.diff(cx[at:at + 800])).max())
    rows.append(r)
    approved = []
    for name in ('calm', 'question', 'cheeky', 'sleepy'):
        text, mood = LINES[name]
        ref = read_wav(os.path.join(SKETCH2, name + '.wav'))
        approved.append(compare(name, text, mood, lead_ms=60.0, ref=ref))
    sp, sz = speed()

    L = []
    L.append('| case | mood | planned end (ms) | C length − planned end (ms) | active Py / C (ms) | active diff (ms) | envelope corr | pitch |Δ| (st, frames) | within 0.5 st | peak Py / C (dBFS) | peak diff (dB) | clipped C / Py | residual (dB) | reveal mismatches | pass |')
    L.append('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for r in rows:
        L.append(f"| {r['name']} | {r['mood']} | {r['planned_end_ms']:.1f} | {fmt(r['dur_diff_ms'], '{:+.2f}')} | {r['active_py_ms']:.1f} / {r['active_c_ms']:.1f} | {r['active_diff_ms']:+.2f} | "
                 f"{r['env_corr']:.4f} | {r['pitch_mad_st']:.3f} ({r['pitch_frames']}) | {100 * r['pitch_within_half']:.1f}% | {r['peak_py_db']:.2f} / {r['peak_c_db']:.2f} | {r['peak_diff_db']:+.2f} | "
                 f"{r['clip_c']} / {r['clip_py']} | {r['residual_db']:.1f} | {('%d of %d' % (r['reveal_mismatch'], r['reveal_blocks'])) if 'reveal_mismatch' in r else '-'} | {'yes' if r['pass'] else 'NO'} |")
    L.append('')
    L.append('Against the version 2 WAVs the owner will hear (sketch2/*.wav from render2.py, made with start_ms = 60; the C render is preceded by 60 ms of silence; the jitter and noise sequences are offset by those 960 samples):')
    L.append('')
    L.append('| file | active Py / C (ms) | active diff (ms) | envelope corr | pitch |Δ| (st, frames) | peak Py / C (dBFS) | peak diff (dB) | clipped C | pass |')
    L.append('|---|---|---|---|---|---|---|---|---|')
    for r in approved:
        L.append(f"| sketch2/{r['name']}.wav | {r['active_py_ms']:.1f} / {r['active_c_ms']:.1f} | {r['active_diff_ms']:+.2f} | {r['env_corr']:.4f} | {r['pitch_mad_st']:.3f} ({r['pitch_frames']}) | "
                 f"{r['peak_py_db']:.2f} / {r['peak_c_db']:.2f} | {r['peak_diff_db']:+.2f} | {r['clip_c']} | {'yes' if r['pass'] else 'NO'} |")
    L.append('')
    L.append('Planner against hum.plan (largest absolute difference per field over all events of each case):')
    L.append('')
    fields = ['t', 'ms', 'p0', 'p1', 'amp', 'fc', 'attack', 'release', 'floor', 'hard', 'scoop', 'reveal', 'word_start', 'vib', 'vib_hz', 'breath', 'console']
    L.append('| case | events | ' + ' | '.join(fields) + ' |')
    L.append('|---|---|' + '---|' * len(fields))
    for r in rows:
        d = r.get('plan')
        if d is None: continue
        if 'count' in d:
            L.append(f"| {r['name']} | COUNT DIFFERS py {d['count'][0]} c {d['count'][1]} |"); continue
        ne = len(np.loadtxt(os.path.join(SCRATCH, f"c_{r['name']}.ev"), ndmin=2))
        L.append(f"| {r['name']} | {ne} | " + ' | '.join(f'{d[f]:.1e}' if d[f] else '0' for f in fields) + ' |')
    h = next(r for r in rows if r['name'] == 'hush')
    L.append('')
    L.append(f"hush (C): loudest sample from 30 ms after the hush on: {h['hush_tail_c']:.1f} dBFS; largest sample step in the 50 ms after it: {h['hush_step_c']:.4f}")
    L.append('')
    L.append(sz)
    L.append(sp + ' (one core of this Mac, -O2)')
    out = '\n'.join(L) + '\n'
    with open(os.path.join(SCRATCH, 'results.md'), 'w') as f: f.write(out)
    print(out)
    print('all pass' if all(r['pass'] for r in rows + approved) else 'SOME FAIL')

if __name__ == '__main__':
    main()
