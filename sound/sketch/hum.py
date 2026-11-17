# Wink's own voice: a hum that carries the shape of the sentence.
# Written as the chip will run it: a planner turns text into syllable events, and a
# per-sample renderer (float32, a 1024-entry sine table, one-pole slews, blocks of 512)
# turns events into 16 kHz mono. Every random choice is seeded from a hash of the text,
# so the same line always hums the same way. VOICE.md is the recipe; this is its proof.
#
# Version 2 (17 November 2026): the same hum moved toward a small, bright, sing-song
# child of a machine. Everything is lifted a fourth (LIFT), the moods run a little quicker,
# stressed beats bounce higher and alternate (the lilt), happy, delighted and cheeky have
# wider contours, a little 25% pulse-wave colour is mixed into the harmonics (the console),
# delighted chirps once per reply, and a soft ceiling keeps it out of whistle range.
# The approved version 1 is kept whole in sound/port/v1/hum.py. VOICE.md section 7 lists
# each change.
import math, struct, sys
import numpy as np

SR = 16000
BLOCK = 512
C4 = 261.63          # pitches below are semitones above C4
F32 = np.float32

# ---------------------------------------------------------------- the moods
# Nine numbers and one habit each. Anchors (where a phrase lands) stay on notes of C major
# for every mood; a mood moves the line between the anchors, never the key.
# v2: tempo is v1's x 0.90 (sad, tender and sleepy keep v1's pace); happy, delighted and
# cheeky have a wider range; lilt, lean and console are new.
#            top  range tempo bright breath gain  lilt lean console quirk
MOODS = {
    'calm':      (5.0, 1.00, 0.90, 1.00, 0.04,  0.0, 0.40, 0.7, 0.22, None),
    'happy':     (6.5, 1.60, 0.83, 1.10, 0.03,  0.5, 0.60, 0.8, 0.28, 'scoop'),
    'delighted': (8.0, 1.90, 0.79, 1.18, 0.03,  1.0, 0.70, 0.8, 0.32, 'flick'),
    'curious':   (6.0, 1.20, 0.87, 1.06, 0.04,  0.0, 0.50, 0.7, 0.25, 'open-end'),
    'thinking':  (4.5, 0.45, 1.04, 0.92, 0.04, -1.0, 0.20, 0.6, 0.20, 'hold-first'),
    'sleepy':    (2.0, 0.50, 1.40, 0.80, 0.12, -3.0, 0.10, 0.5, 0.12, 'sag'),
    'surprised': (8.0, 1.40, 0.81, 1.14, 0.03,  1.0, 0.50, 0.7, 0.28, 'leap'),
    'sad':       (3.0, 0.60, 1.25, 0.85, 0.07, -3.0, 0.00, 0.5, 0.10, 'droop'),
    'cheeky':    (5.5, 1.80, 0.86, 1.10, 0.04,  0.0, 0.60, 0.8, 0.30, 'swing'),
    'tender':    (3.5, 0.80, 1.15, 0.84, 0.08, -2.5, 0.00, 0.5, 0.10, 'legato'),
    'alert':     (7.0, 1.00, 0.77, 1.16, 0.02,  1.0, 0.30, 0.7, 0.25, 'staccato'),
}

# v2: the whole voice sits a fourth higher. Home is F4 (349 Hz); the anchors become F4, G4,
# C5, D5: still notes of C major, and the continuation now lands on C5 itself.
# The names below keep version 1's roles (G4 is "the continuation note", A4 "the yes/no
# question note"); their values are lifted, so G4 now sounds C5 (12) and A4 sounds D5 (14).
LIFT = 5.0
HOME, D4, E4, G4, A4, C5 = (v + LIFT for v in (0.0, 2.0, 4.0, 7.0, 9.0, 12.0))
SMALL = 1.12         # v2: the moving colour sits 12% higher, as a smaller mouth's would
KNEE, PMAX = 16.0, 19.0   # v2: above E5 a pitch moves at half rate, and never past G5 (784 Hz)

def cap(p):
    return p if p <= KNEE else min(KNEE + (p - KNEE) * 0.5, PMAX)

FUNCTION = set('a an the to of and or but is it its in on at for as be are was were i you me my your we he she they them this that with so if not do does did am im its by from up out just'.split())
UNSTRESSED_PREFIX = ('be', 'de', 're', 'un', 'con', 'com', 'ex', 'pre', 'a')
WH = set('what where who whom whose why how when which'.split())
HARD = set('pbtdkgcqx')          # a hard onset: a click and a fast attack

def h32(s):
    h = 2166136261
    for ch in s.encode():
        h = ((h ^ ch) * 16777619) & 0xffffffff
    return h

class Rng:                                     # xorshift32, as on the chip
    def __init__(self, seed): self.s = seed or 1
    def u(self):
        s = self.s
        s ^= (s << 13) & 0xffffffff; s ^= s >> 17; s ^= (s << 5) & 0xffffffff
        self.s = s
        return s / 4294967296.0
    def pm(self): return self.u() * 2 - 1

def syllables(w):
    """vowel groups in the word; each gets its vowel letter (for colour)"""
    w = w.lower()
    groups, i = [], 0
    while i < len(w):
        if w[i] in 'aeiouy':
            j = i
            while j < len(w) and w[j] in 'aeiouy': j += 1
            groups.append((i, w[i]))
            i = j
        else:
            i += 1
    if len(groups) > 1 and w.endswith('e') and not w.endswith('le') and groups[-1][0] == len(w) - 1:
        groups.pop()                           # a silent final e
    if not groups: groups = [(0, 'a')]         # digits, initials
    return groups[:4]

VOWEL_FC = {'a': 950.0, 'o': 860.0, 'u': 820.0, 'e': 1180.0, 'i': 1420.0, 'y': 1380.0}

def plan(text, mood='calm', start_ms=0.0):
    """text -> list of syllable events and the reveal clock. Mirrors the C planner."""
    top, rng_, tempo, bright, breath, gain, lilt, lean, console, quirk = MOODS[mood]
    rnd = Rng(h32(text + '|' + mood))
    # tokens: words, and the punctuation that ends a phrase
    toks, cur, cs = [], '', 0
    for i, ch in enumerate(text):
        if ch.isalnum() or ch == "'":
            if not cur: cs = i
            cur += ch
        else:
            if cur: toks.append(('w', cur, cs, i)); cur = ''
            if ch in ',;:.?!': toks.append(('p', ch, i, i + 1))
    if cur: toks.append(('w', cur, cs, len(text)))
    # collapse '...' into one trail mark
    t2 = []
    for t in toks:
        if t[0] == 'p' and t[1] == '.' and t2 and t2[-1][0] == 'p' and t2[-1][1] in '.…':
            t2[-1] = ('p', '…', t2[-1][2], t[3])
        else:
            t2.append(t)
    toks = t2
    # phrases: runs of words closed by punctuation (or the end)
    phrases, words = [], []
    for t in toks:
        if t[0] == 'w': words.append(t)
        else:
            if words: phrases.append((words, t[1], t[3])); words = []
    if words: phrases.append((words, '.', words[-1][3]))

    ev = []                                    # the events
    t = start_ms
    sentence_index = 0
    first_word_of_sentence = True
    leapt = False                              # surprised: one leap per sentence
    chirped = False                            # delighted: one chirp per reply
    for pi, (words, mark, mark_end) in enumerate(phrases):
        nw = len(words)
        q_wh = False
        # a question that starts with a wh-word rises less
        if mark == '?':
            j = pi
            while j > 0 and phrases[j - 1][1] == ',': j -= 1
            q_wh = phrases[j][0][0][1].lower() in WH
        # stress per syllable
        sylls = []
        for wi, (_, w, a, b) in enumerate(words):
            lw = w.lower().replace("'", '')
            g = syllables(lw)
            if lw.isdigit(): g = [(k, 'e') for k in range(min(len(lw), 4))]
            func = lw in FUNCTION and len(g) == 1
            stress_at = -1 if func else (1 if (len(g) > 1 and lw.startswith(UNSTRESSED_PREFIX) and len(g) == 2 and len(lw) > 4) else 0)
            for si, (pos, v) in enumerate(g):
                sylls.append(dict(wi=wi, si=si, n=len(g), v=v, stressed=(si == stress_at), func=func,
                                  onset=(lw[pos - 1] if pos > 0 else lw[0]) if (si > 0 or pos > 0) else '',
                                  a=a, b=b, word=lw))
        stressed_idx = [k for k, s in enumerate(sylls) if s['stressed']]
        nuclear = stressed_idx[-1] if stressed_idx else len(sylls) - 1
        # sentence declination: later sentences start a little lower and quieter
        sent_drop = min(sentence_index, 3)
        line_top = top + LIFT - 0.6 * sent_drop
        beat = 0                               # v2: stressed beats so far in the phrase (the lilt)
        for k, s in enumerate(sylls):
            word_seed = Rng(h32(s['word']))
            wobble = word_seed.pm() * 0.4        # the same word always sits the same way
            dlen = 1.0 + word_seed.pm() * 0.08
            frac = k / max(1, len(sylls) - 1)
            line = line_top - 2.0 * frac * rng_ # the phrase drifts down
            last = (k == len(sylls) - 1)
            if s['stressed']:
                # v2: stressed beats sit higher (2.3, was 2.0) and alternate up and down
                p = line + (2.3 + (lilt if beat % 2 == 0 else -0.5 * lilt)) * rng_ + wobble
                amp = 0.86
                beat += 1
            elif s['func']:
                p = line - 1.0 * rng_ + wobble
                amp = 0.58
            else:
                p = line - 0.9 * rng_ + wobble   # v2: was 0.6, a deeper bounce between beats
                amp = 0.70
            if k == nuclear:
                p = line + 3.0 * rng_ + wobble
                amp = 1.0
            ms = 150.0 * tempo * dlen
            if s['stressed']: ms *= 1.30
            elif s['func']: ms *= 0.72           # v2: was 0.80
            else: ms *= 0.88                     # v2: plain pulses shorter, stressed ones long
            if last: ms *= 1.45 if mark in '.!?…' else 1.2
            p0, p1 = p, p - lean                 # every syllable leans down (v2: 0.5 to 0.8, by mood)
            # where the phrase lands: anchors in the key
            if last:
                if mark == ',' or mark in ';:':
                    p0, p1 = p, G4 if p < G4 else p + 1.0                 # continuation: up to G
                elif mark == '?':
                    tgt = (E4 + 3.0) if q_wh else A4                    # wh: a small rise; yes/no: to A
                    p0, p1 = min(p, tgt - 4.0), tgt
                    ms *= 1.1
                elif mark == '…':
                    p0, p1 = p, p - 1.5
                    ms *= 1.3
                else:
                    land = D4 if quirk == 'open-end' else HOME
                    p0, p1 = max(p, land + 3.0), land
                    if mark == '!': p0 += 1.0; amp = max(amp, 1.0)
                    # delighted: the landing starts a major third higher, a hop before home
                    if quirk == 'flick' and pi == len(phrases) - 1: p0 += 4.0
                    # sleepy: home, then a little under it
                    if quirk == 'sag': p1 = land - 0.7
            # habits
            if quirk == 'droop' and s['si'] == s['n'] - 1 and not last: p1 -= 0.7
            if quirk == 'staccato': ms *= 0.72
            if quirk == 'hold-first' and first_word_of_sentence and k == 0: ms *= 1.6
            if quirk == 'leap' and not leapt and s['stressed']:
                p0 += 4.0; p1 += 3.0; leapt = True
            if quirk == 'swing': ms *= 1.18 if (k % 2 == 0) else 0.80
            p0, p1 = cap(p0), cap(p1)            # v2: the soft ceiling
            scoop = 0.0
            if quirk in ('scoop', 'swing') and s['stressed']: scoop = -1.5 if quirk == 'scoop' else -2.0
            amp *= 10 ** ((gain - 1.0 * sent_drop) / 20)
            onset = s['onset']
            hard = (onset in HARD) or quirk == 'staccato'
            attack = (5.0 if hard else 22.0) * (1.6 if quirk in ('legato',) or mood == 'sleepy' else 1.0)
            fc = VOWEL_FC.get(s['v'], 1000.0) * bright * SMALL
            word_start = s['si'] == 0
            word_end = s['si'] == s['n'] - 1
            ev.append(dict(t=t, ms=ms, p0=p0, p1=p1, amp=amp, fc=fc, open=0.25, breath=breath,
                           hard=hard, attack=attack, scoop=scoop, word_start=word_start,
                           reveal=(s['b'] if word_start else None), vib=(0.10 if mood != 'sad' else 0.0),
                           vib_hz=(3.8 if mood == 'sleepy' else 5.2), console=console))
            first_word_of_sentence = False
            t += ms
            # what comes after this syllable
            if last:
                gap = {',': 180, ';': 230, ':': 230, '.': 340, '!': 320, '?': 360, '…': 420}[mark]
                ev[-1]['reveal_end'] = mark_end
                ev[-1]['release'] = 60.0 if mark in '.?!…' else 40.0
                # cheeky's fall-rise: the landing flicks back up, short and light
                if quirk == 'swing' and mark in '.!' and pi == len(phrases) - 1:
                    ev.append(dict(t=t + 25, ms=95 * tempo, p0=HOME + 1.0, p1=E4, amp=0.55 * 10 ** (gain / 20), fc=1150 * bright * SMALL,
                                   open=0.25, breath=breath, hard=False, attack=10.0, scoop=0.0, word_start=False, reveal=None,
                                   vib=0.0, vib_hz=5.2, release=35.0, console=console))
                    t += 120
                # v2, delighted: one tiny upward chirp per reply, after the first phrase that
                # ends in ! or , (never the last phrase, which has the hop). Always the same
                # two notes: C5 up to F5, home an octave above, as the opening gesture reaches.
                if quirk == 'flick' and mark in '!,' and not chirped and pi != len(phrases) - 1:
                    # it starts 30 ms after the landing and jumps to its first note (word_start)
                    ev.append(dict(t=t + 30, ms=65.0, p0=G4, p1=HOME + 12.0, amp=0.45 * 10 ** ((gain - 1.0 * sent_drop) / 20),
                                   fc=1700.0 * bright * SMALL, open=0.25, breath=breath, hard=False, attack=4.0, scoop=0.0,
                                   word_start=True, reveal=None, vib=0.0, vib_hz=5.2, release=25.0, console=0.5))
                    chirped = True
                    t += 105
                t += gap * tempo
                if mark in '.?!…':
                    sentence_index += 1
                    first_word_of_sentence = True
                    leapt = False
            elif word_end:
                # between words the hum dips but keeps sounding, and the pitch glides on
                ev[-1]['release'] = 30.0
                ev[-1]['floor'] = 0.30 if quirk == 'legato' else (0.0 if quirk == 'staccato' else 0.12)
                t += (45.0 if quirk != 'legato' else 30.0) * tempo
            else:
                ev[-1]['release'] = 0.0            # within a word: a dip, not a stop
    return ev, t

# ----------------------------------------------------------- gestures (transitions)
GESTURE_CONSOLE = 0.15
def gesture_open(t=0.0):
    """the whole mind arrives: the closed hum opens and rises to home above (v2: C5 to F5)"""
    return [dict(t=t, ms=200, p0=G4, p1=G4, amp=0.70, fc=700, open=0.10, breath=0.02, hard=False, attack=25,
                 scoop=0, word_start=False, reveal=None, vib=0.0, vib_hz=5.2, release=0.0,
                 fc1=700, open1=0.10, console=GESTURE_CONSOLE),
            dict(t=t + 200, ms=330, p0=G4, p1=C5, amp=1.0, fc=700, open=0.10, breath=0.02, hard=False, attack=12,
                 scoop=0, word_start=False, reveal=None, vib=0.12, vib_hz=5.2, release=110.0,
                 fc1=1750, open1=1.0, console=GESTURE_CONSOLE)]

def gesture_close(t=0.0):
    """it is alone again: an open note closes and falls back into the hum (v2: F5 to C5)"""
    return [dict(t=t, ms=420, p0=C5, p1=G4, amp=0.85, fc=1750, open=1.0, breath=0.02, hard=False, attack=18,
                 scoop=0, word_start=False, reveal=None, vib=0.0, vib_hz=5.2, release=90.0,
                 fc1=700, open1=0.10, console=GESTURE_CONSOLE)]

# ------------------------------------------------------------------- renderer
TABLE = np.sin(np.arange(1025) * 2 * math.pi / 1024).astype(F32)

def tsin(ph):                                   # ph in cycles, 0..1
    x = ph * 1024.0
    i = int(x); f = x - i
    return TABLE[i] + (TABLE[i + 1] - TABLE[i]) * f

def lorentz(f, fc, bw):
    d = (f - fc) / bw
    return 1.0 / (1.0 + d * d)

MASTER = 0.38          # set by measurement against Samantha through the speaker band
                       # (v2: was 0.42; the lifted hum puts ~0.9 dB more into the band)
BODY_HZ, BODY_BW = 1650.0, 320.0                 # Wink's case: the one fixed resonance
KMAX, FMAX = 12, 3800.0
# v2: the console colour, a 25% pulse wave's harmonics |sin(pi k / 4)| / k (every 4th is 0)
PULSE = [abs(math.sin(math.pi * k / 4)) / k if k % 4 else 0.0 for k in range(1, KMAX + 1)]

def harmonics(f0, fc, opn, amps, con=0.0):
    """per-harmonic gains, computed once per 32 samples; they sum to 1.
    v2: a share `con` of the sum is a pulse wave's spectrum, the rest is the hum's own"""
    tilt_exp = 1.2 - 0.8 * opn                   # closed mouth: darker
    bw = 380.0 + 500.0 * opn
    tot = 0.0; ptot = 0.0
    pw = [0.0] * KMAX
    for k in range(KMAX):
        f = f0 * (k + 1)
        if f > FMAX: amps[k] = 0.0; continue
        hp = (f * f) / (f * f + 420.0 * 420.0)   # nothing worth having below the speaker
        a = (k + 1) ** -tilt_exp * (0.22 + lorentz(f, fc, bw)) * (1.0 + 0.9 * lorentz(f, BODY_HZ, BODY_BW)) * hp
        amps[k] = a; tot += a
        pw[k] = PULSE[k] * hp; ptot += pw[k]
    for k in range(KMAX):
        amps[k] = (1.0 - con) * amps[k] / tot + (con * pw[k] / ptot if ptot > 0 else 0.0)

def render(ev, total_ms, seed=1, hush_ms=None):
    n = int(total_ms * SR / 1000) + BLOCK
    n = (n // BLOCK + 1) * BLOCK
    out = np.zeros(n, dtype=F32)
    rng = Rng(seed)
    ph = 0.0; vph = 0.0; amp = 0.0; st = 5.0; jit = 0.0
    nz_lp = 0.0; nz_hp = 0.0; lp_out = 0.0
    amps = [0.0] * KMAX
    phk = [0.0] * KMAX
    ei = 0
    cur = None
    for i in range(n):
        tms = i * 1000.0 / SR
        # which event is sounding
        while ei < len(ev) and ev[ei]['t'] <= tms:
            cur = ev[ei]; ei += 1
            if cur.get('word_start') or amp < 0.02:
                st = cur['p0'] + cur['scoop']     # a new word may jump (it starts from near silence)
        target = 0.0; p_t = st; fc = 1000.0; opn = 0.25; breath = 0.0; vib = 0.0
        if cur is not None:
            u = (tms - cur['t']) / cur['ms']
            rel = cur.get('release', 30.0)
            if u < 1.0:
                p_t = cur['p0'] + (cur['p1'] - cur['p0']) * min(1.0, u * 1.15)
                env = 1.0 - 0.18 * u              # a held syllable sinks a little
                target = cur['amp'] * env
                # a dip at the end of a syllable inside a word
                if rel == 0.0 and u > 0.82: target *= 0.35
                fc = cur['fc'] + (cur.get('fc1', cur['fc']) - cur['fc']) * u
                opn = cur['open'] + (cur.get('open1', cur['open']) - cur['open']) * u
                breath = cur['breath']
                vib = cur['vib'] if (tms - cur['t']) > 80 and cur['ms'] > 160 else 0.0
                atk = cur['attack']; tau_ms = atk / 3.0
            else:
                target = cur.get('floor', 0.0) * cur['amp']
                breath = cur['breath'] * 0.5
                tau_ms = max(rel, 8.0) / 3.0
                p_t = cur['p1']
                fc = cur.get('fc1', cur['fc']); opn = cur.get('open1', cur['open'])
            if cur['scoop'] and u < 0.25:
                p_t = cur['p0'] + cur['scoop'] * (1.0 - u / 0.25)
        else:
            tau_ms = 10.0
        if hush_ms is not None and tms >= hush_ms:
            target = 0.0; tau_ms = 4.0             # hush: gone in about 20 ms, no click
        ka =1.0 - math.exp(-1000.0 / (SR * max(tau_ms, 0.5)))
        amp += (target - amp) * ka
        kp = 1.0 - math.exp(-1000.0 / (SR * 22.0))   # 22 ms glide
        st += (p_t - st) * kp
        # living: a slow wander and a late vibrato
        if (i & 31) == 0:
            jit += (rng.pm() * 0.06 - jit * 0.02)
        vph += (cur['vib_hz'] if cur is not None else 5.2) / SR
        if vph >= 1.0: vph -= 1.0
        f0 = C4 * 2.0 ** ((st + jit + vib * tsin(vph)) / 12.0)
        if (i & 31) == 0:
            harmonics(f0, fc, opn, amps, cur.get('console', 0.0) if cur is not None else 0.0)
        s = 0.0
        for k in range(KMAX):
            if amps[k] == 0.0: continue
            phk[k] += f0 * (k + 1) / SR
            phk[k] -= int(phk[k])
            s += amps[k] * tsin(phk[k])
        # breath and click: white noise through a 1.0 to 3.2 kHz band
        w = rng.pm()
        nz_lp += (w - nz_lp) * 0.72               # one-pole low-pass near 3.2 kHz
        nz_hp += (nz_lp - nz_hp) * 0.33           # one-pole tracker near 1 kHz
        band = nz_lp - nz_hp
        click = 0.0
        if cur is not None and cur['hard']:
            dt = tms - cur['t']
            if 0 <= dt < 6.0: click = 0.45 * (1.0 - dt / 6.0)
        if hush_ms is not None and tms >= hush_ms:
            click = 0.0                            # hush clears the events: no later consonant sounds
        y = amp * (s + breath * 2.2 * band) + click * band * cur['amp'] if cur is not None else 0.0
        lp_out += (y - lp_out) * 0.80             # one-pole near 4 kHz: nothing shrill
        out[i] = lp_out * MASTER
    return out

# ------------------------------------------------------------------ io and checks
def write_wav(path, x):
    x16 = np.clip(np.round(x * 32767), -32768, 32767).astype('<i2')
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + 2 * len(x16)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, SR, SR * 2, 2, 16))
        f.write(b'data' + struct.pack('<I', 2 * len(x16)) + x16.tobytes())

def biquad(x, b, a):
    y = np.zeros_like(x, dtype=np.float64); x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x):
        o = b[0] * v + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y

def rbj(kind, f, q=0.707):
    w = 2 * math.pi * f / SR; c = math.cos(w); al = math.sin(w) / (2 * q)
    if kind == 'hp': b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
    else: b = [(1 - c) / 2, 1 - c, (1 - c) / 2]
    a = [1 + al, -2 * c, 1 - al]
    return [v / a[0] for v in b], [1.0, a[1] / a[0], a[2] / a[0]]

def speaker(x):
    """a crude model of the 1 W speaker: 2nd-order high-pass 400 Hz twice, low-pass 5 kHz"""
    y = x.astype(np.float64)
    for _ in range(2): y = biquad(y, *rbj('hp', 400))
    return biquad(y, *rbj('lp', 5000))

def active_rms_db(x, thr_db=-40):
    fr = x[:len(x) // 320 * 320].reshape(-1, 320)
    r = np.sqrt((fr ** 2).mean(1))
    act = r > 10 ** (thr_db / 20)
    return 20 * math.log10(math.sqrt((fr[act] ** 2).mean())) if act.any() else -120.0

def bands(x):
    X = np.abs(np.fft.rfft(x)) ** 2; fq = np.fft.rfftfreq(len(x), 1 / SR); tot = X.sum()
    return [X[(fq >= a) & (fq < b)].sum() / tot for a, b in [(0, 400), (400, 800), (800, 3000), (3000, 5000), (5000, 8001)]]

def pitch_track(x, hop=160, win=640, fmin=200, fmax=1000):
    out = []
    for s in range(0, len(x) - win, hop):
        fr = x[s:s + win] * np.hanning(win)
        if np.sqrt((fr ** 2).mean()) < 10 ** (-42 / 20): out.append(0.0); continue
        ac = np.correlate(fr, fr, 'full')[win - 1:]
        lo, hi = int(SR / fmax), int(SR / fmin)
        k = lo + int(np.argmax(ac[lo:hi]))
        out.append(SR / k if ac[k] > 0.3 * ac[0] else 0.0)
    return np.array(out)

def planned_f0(ev, n_frames, hop=160):
    f = np.zeros(n_frames)
    for e in ev:
        a = int(e['t'] * SR / 1000 / hop); b = int((e['t'] + e['ms']) * SR / 1000 / hop)
        for j in range(a, min(b, n_frames)):
            u = (j - a) / max(1, b - a)
            f[j] = C4 * 2 ** ((e['p0'] + (e['p1'] - e['p0']) * u) / 12)
    return f

def mouth(x):
    """like envAdd's ring: the loudest sample in each 10 ms, 0..1"""
    fr = x[:len(x) // 160 * 160].reshape(-1, 160)
    return np.abs(fr).max(1)
