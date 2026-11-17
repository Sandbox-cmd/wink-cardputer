# How clear is the childlike Samantha? Word accuracy by whisper, plain against treated,
# straight, through a crude model of the device's speaker, and through the speaker with
# noise added (a harder listen, to find where the margin runs out), over a ladder of strengths.
#
#   train/.venv/bin/python -B sound/childlike_check.py            the ladder and the tables (about 8 min)
#   train/.venv/bin/python -B sound/childlike_check.py --render   only the WAVs for listening
#
# Writes the working files in sound/sketch2/voice-work/ and the tables in
# sound/sketch2/voice-results.md; with --render, sound/sketch2/voice-*.wav (five sentences:
# plain, childlike at the chosen strength, one step milder and one stronger).
# whisper is a floor, not an ear: a treatment that loses words fails; one that keeps them
# may still sound wrong, which only listening can tell.
import os, re, subprocess, sys, time
sys.dont_write_bytecode = True
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, 'sketch'))
import childlike, hum

OUT = os.path.join(HERE, 'sketch2')
WORK = os.path.join(OUT, 'voice-work')
MODEL = os.path.join(PROJ, 'bridge', 'models', 'ggml-base.en.bin')
PY = sys.executable

SENTENCES = [
    "That's the Mac. Now we can really talk.",
    "I found some songs you might like for the drive home.",
    "Your next meeting starts soon, so grab some water first.",
    "The weather looks clear tonight, perfect for a short walk outside.",
    "I'm not sure about that one, but I can ask the whole mind for help.",
    "Remember to call your sister before the bakery closes.",
    "The battery is getting low, so please plug me in soon.",
    "Somebody left the kitchen window open again this morning.",
    "We could try the puzzle once more after dinner, if you want.",
    "Thank you for showing me the garden, the roses look lovely.",
]

def say(text, path):
    if not os.path.exists(path):
        subprocess.run(['say', '-v', 'Samantha', '-o', path, text], check=True, timeout=30)

def plain_ul(aiff, out):          # bridge/voice.mjs, exactly (trim off)
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', aiff, '-af', 'highpass=f=160,alimiter=limit=0.95',
                    '-ar', '16000', '-ac', '1', '-f', 'mulaw', out], check=True)

def tape_ul(aiff, out, st):       # the comparator: pitch and formants moved together (a faster tape)
    r = 2 ** (st / 12)
    af = f'asetrate={22050 * r:.1f},aresample=22050,atempo={1 / r * 1.0 / 0.94:.4f},highpass=f=160,{childlike.BRIGHT},alimiter=limit=0.95'
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', aiff, '-af', af, '-ar', '16000', '-ac', '1', '-f', 'mulaw', out], check=True)

def ulaw(path):
    b = np.fromfile(path, dtype=np.uint8)
    u = ~b; s = u & 0x80; e = (u >> 4) & 7; m = u & 0x0f
    x = (((m.astype(np.int32) << 3) + 0x84) << e) - 0x84
    return (np.where(s, -x, x) / 32768.0).astype(np.float32)

def norm_peak(x):
    return x * (10 ** (-1 / 20) / max(1e-6, float(np.abs(x).max())))

def words(s):
    return re.sub(r"[^a-z' ]+", ' ', s.lower().replace('’', "'")).split()

def accuracy(ref, hyp):
    r, h = words(ref), words(hyp)
    d = np.zeros((len(r) + 1, len(h) + 1), int)
    d[:, 0] = range(len(r) + 1); d[0, :] = range(len(h) + 1)
    for i in range(1, len(r) + 1):
        for j in range(1, len(h) + 1):
            d[i, j] = min(d[i - 1, j] + 1, d[i, j - 1] + 1, d[i - 1, j - 1] + (r[i - 1] != h[j - 1]))
    return int(d[-1, -1]), len(r)

def whisper(paths):
    subprocess.run(['whisper-cli', '-m', MODEL, '-nt', '-np', '-l', 'en', '-ng', '-otxt'] + paths,
                   check=True, capture_output=True, timeout=600)
    return [open(p + '.txt').read().strip() for p in paths]

def active_db(x):
    return hum.active_rms_db(x)

def condition(name, make):
    """make(aiff, out_ul) for every sentence; returns per-path errors and transcripts"""
    straight, spk, noisy = [], [], []
    levels = []
    for i, text in enumerate(SENTENCES):
        aiff = os.path.join(WORK, f's{i + 1}.aiff')
        ul = os.path.join(WORK, f'{name}-s{i + 1}.ul')
        make(aiff, ul)
        x = ulaw(ul)
        sp = hum.speaker(x).astype(np.float32)
        levels.append(active_db(sp))
        a = os.path.join(WORK, f'{name}-s{i + 1}.wav'); b = os.path.join(WORK, f'{name}-s{i + 1}-spk.wav')
        hum.write_wav(a, norm_peak(x)); hum.write_wav(b, norm_peak(sp))
        # noise shaped by the same speaker model, 6 dB under the speech's active level
        nz = hum.speaker(np.random.default_rng(1000 + i).standard_normal(len(x)).astype(np.float32)).astype(np.float32)
        nz *= 10 ** ((active_db(sp) - 6.0) / 20) / float(np.sqrt((nz ** 2).mean()))
        c_ = os.path.join(WORK, f'{name}-s{i + 1}-noise.wav')
        hum.write_wav(c_, norm_peak(sp + nz))
        straight.append(a); spk.append(b); noisy.append(c_)
    res = {}
    for key, paths in (('straight', straight), ('speaker', spk), ('noisy', noisy)):
        hyp = whisper(paths)
        errs = [accuracy(t, h) for t, h in zip(SENTENCES, hyp)]
        e = sum(x for x, _ in errs); n = sum(y for _, y in errs)
        res[key] = dict(err=e, n=n, acc=100.0 * (1 - e / n), err5=sum(x for x, _ in errs[:5]), n5=sum(y for _, y in errs[:5]),
                        hyp=hyp)
    res['spk_db'] = float(np.median(levels))
    return res

def timing():
    """the added time per second of speech: in-process, and as the bridge would run it"""
    out = {}
    secs = 0.0; t_treat = 0.0
    for i in range(len(SENTENCES)):
        snd = childlike.load(os.path.join(WORK, f's{i + 1}.aiff'))
        secs += snd.duration
        t0 = time.perf_counter(); childlike.smaller(snd); t_treat += time.perf_counter() - t0
    out['praat_ms_per_s'] = 1000 * t_treat / secs
    # whole process, as treat() runs it, against the plain ffmpeg call
    reps = 5
    aiff = os.path.join(WORK, 's5.aiff'); dur = childlike.load(aiff).duration
    t0 = time.perf_counter()
    for _ in range(reps): plain_ul(aiff, os.path.join(WORK, 't-plain.ul'))
    t_plain = (time.perf_counter() - t0) / reps
    t0 = time.perf_counter()
    for _ in range(reps):
        subprocess.run([PY, '-I', os.path.join(HERE, 'childlike.py'), aiff, os.path.join(WORK, 't-child.ul')], check=True)
    t_child = (time.perf_counter() - t0) / reps
    t0 = time.perf_counter()
    for _ in range(reps): subprocess.run([PY, '-I', '-c', 'import numpy, parselmouth'], check=True)
    t_start = (time.perf_counter() - t0) / reps
    out.update(plain_s=t_plain, child_s=t_child, start_s=t_start, sentence_s=dur)
    return out

def main():
    os.makedirs(WORK, exist_ok=True)
    for i, text in enumerate(SENTENCES): say(text, os.path.join(WORK, f's{i + 1}.aiff'))
    conds = [('plain', 'as bridge/voice.mjs today', plain_ul)]
    conds.append(('bright', 'only the +3 dB top shelf', lambda a, o: childlike.write_ulaw(childlike.load(a), o)))
    for k, p in childlike.LADDER.items():
        conds.append((f'child{k}', f'formants x{p[0]:.2f}, pitch x{p[1]:.2f} ({12 * np.log2(p[1]):+.0f} st), range x{p[2]:.2f}, length x{p[3]:.2f}',
                      lambda a, o, k=k: childlike.write_ulaw(childlike.smaller(childlike.load(a), k), o)))
    for st in (2, 3, 4):
        conds.append((f'tape{st}', f'pitch and formants together {st:+d} st (asetrate), length x0.94', lambda a, o, st=st: tape_ul(a, o, st)))
    rows = []
    for name, what, make in conds:
        print('...', name, flush=True)
        r = condition(name, make); r['name'] = name; r['what'] = what; rows.append(r)
    tm = timing()
    base = rows[0]
    L = ['# The childlike voice: what whisper heard', '',
         f'Ten Samantha sentences ({base["straight"]["n"]} words), each made by `say`, treated, written as 16 kHz mu-law, decoded, and given to whisper-cli (ggml-base.en, CPU) one file a sentence.',
         '"Speaker" runs the decoded audio through the crude device-speaker model first (hum.speaker: high-pass 400 Hz twice, low-pass 5 kHz). Both paths are peak-normalised to -1 dBFS before whisper.',
         'Accuracy is 1 - (substitutions + deletions + insertions) / words, on lowercased words without punctuation. One word is about 1 point.', '',
         '"Noisy" adds noise, shaped by the same speaker model, 6 dB under the speech: a harder listen, there to show how much margin each strength leaves.', '',
         '| condition | what it is | straight: errors (acc %) | speaker: errors (acc %) | speaker + noise: errors (acc %) | first five only, speaker: errors / words | level through the speaker (dBFS, median) |',
         '|---|---|---|---|---|---|---|']
    for r in rows:
        L.append(f"| {r['name']} | {r['what']} | {r['straight']['err']} ({r['straight']['acc']:.1f}) | {r['speaker']['err']} ({r['speaker']['acc']:.1f}) | {r['noisy']['err']} ({r['noisy']['acc']:.1f}) | {r['speaker']['err5']} / {r['speaker']['n5']} | {r['spk_db']:.1f} |")
    L += ['', 'Time added (this Mac):',
          f"- Praat's change alone: {tm['praat_ms_per_s']:.1f} ms per second of speech.",
          f"- Whole call on a {tm['sentence_s']:.1f} s sentence: plain ffmpeg {1000 * tm['plain_s']:.0f} ms; childlike.py {1000 * tm['child_s']:.0f} ms; of which starting python and importing parselmouth {1000 * tm['start_s']:.0f} ms.", '',
          'What whisper wrote, per condition (speaker path):', '']
    for r in rows:
        L.append(f"- **{r['name']}**: " + ' / '.join(r['speaker']['hyp']))
    L += ['', 'And with noise:', '']
    for r in rows:
        L.append(f"- **{r['name']}**: " + ' / '.join(r['noisy']['hyp']))
    with open(os.path.join(OUT, 'voice-results.md'), 'w') as f: f.write('\n'.join(L) + '\n')
    print('\n'.join(L[:len(rows) + 12]))

def render_voices():
    """the five sentences the owner hears: plain, the chosen strength, one milder; and one A/B file"""
    os.makedirs(os.path.join(OUT, 'as-device'), exist_ok=True)
    gap = np.zeros(int(0.5 * hum.SR), np.float32)
    ab, ab_dev = [], []
    for i, text in enumerate(SENTENCES[:5]):
        aiff = os.path.join(WORK, f's{i + 1}.aiff'); say(text, aiff)
        got = {}
        for tag, make in (('plain', plain_ul),
                          ('childlike', lambda a, o: childlike.write_ulaw(childlike.smaller(childlike.load(a), childlike.CHOSEN), o)),
                          ('milder', lambda a, o: childlike.write_ulaw(childlike.smaller(childlike.load(a), childlike.CHOSEN - 1), o)),
                          ('stronger', lambda a, o: childlike.write_ulaw(childlike.smaller(childlike.load(a), childlike.CHOSEN + 1), o))):
            ul = os.path.join(WORK, f'render-{tag}-{i + 1}.ul'); make(aiff, ul)
            x = ulaw(ul); got[tag] = x
            hum.write_wav(os.path.join(OUT, f'voice-{tag}-{i + 1}.wav'), x)
        ab += [got['plain'], gap, got['childlike'], gap, gap]
    x = np.concatenate(ab)
    hum.write_wav(os.path.join(OUT, 'voice-plain-then-childlike.wav'), x)
    hum.write_wav(os.path.join(OUT, 'as-device', 'voice-plain-then-childlike.wav'), hum.speaker(x).astype(np.float32) * 10 ** (3.5 / 20))
    print('rendered the five sentences at strengths', childlike.CHOSEN - 1, childlike.CHOSEN, childlike.CHOSEN + 1)

if __name__ == '__main__':
    if sys.argv[1:] == ['--render']: render_voices()
    else: main()
