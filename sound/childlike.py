# A childlike, smaller-sounding Samantha that stays clear.
#
#   train/.venv/bin/python -I sound/childlike.py in.aiff out.ul [strength | chosen] [trim]
#
# Reads what `say` wrote (any rate; AIFF-C included), and writes 16 kHz mono mu-law shaped
# exactly as bridge/voice.mjs shapes the plain voice (high-pass 160 Hz, then the limiter at
# 0.95, last), with two things between them: the voice made smaller, and a gentle lift of
# the top.
#
# "Smaller" is Praat's "Change gender" (PSOLA, through the praat-parselmouth package): the
# formants (the resonances that say how big the speaker is) move up by one ratio, and the
# pitch by another, larger one, so it sounds like a smaller person rather than a sped-up
# tape (moving both by the same ratio is the chipmunk). The speech is also made a little
# quicker. Strength 4 is the one chosen from sound/childlike_check.py's tables; the ladder is kept so
# the choice can be heard and moved.
import os, subprocess, sys

import numpy as np
import parselmouth

#            formant ratio, pitch ratio (median), pitch range factor, duration factor
LADDER = {
    1: (1.06, 1.19, 1.10, 0.96),   # +3 st
    2: (1.10, 1.26, 1.15, 0.95),   # +4 st
    3: (1.14, 1.33, 1.20, 0.94),   # +5 st
    4: (1.18, 1.41, 1.25, 0.93),   # +6 st
    5: (1.22, 1.50, 1.30, 0.92),   # +7 st
    6: (1.26, 1.59, 1.30, 0.91),   # +8 st
    7: (1.30, 1.78, 1.30, 0.90),   # +10 st, past where it should go: there to find the break
    8: (1.36, 2.00, 1.30, 0.90),   # +12 st, the same
}
# Chosen 17 November 2026 by design, not by whisper: on the straight and speaker paths every
# step up to 8 keeps plain's accuracy (sound/sketch2/voice-results.md), so the 5-point rule
# never decided. Step 4 is the strongest that lost no word relative to plain on both paths
# while its formants move about half as far as its pitch (x1.18 against +6 st): "a few
# semitones, no chipmunk". Under added noise every treated step, even 1, loses 4 to 7 words
# more than plain (step 4: 92.5% against 98.1%); from step 6 on it loses more.
CHOSEN = 4
WORK_RATE = 22050                  # what `say` writes; the treatment runs at this rate
BRIGHT = 'treble=g=3:f=3000'       # slightly brighter: a +3 dB shelf above about 3 kHz

def load(path):
    b = subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', path, '-f', 'f32le', '-ac', '1', '-ar', str(WORK_RATE), '-'],
                       capture_output=True, check=True, timeout=30).stdout
    return parselmouth.Sound(np.frombuffer(b, np.float32).astype(np.float64), sampling_frequency=WORK_RATE)

def smaller(snd, strength=CHOSEN):
    formant, pitch, rng, dur = LADDER[strength]
    f = snd.to_pitch(time_step=0.01, pitch_floor=75, pitch_ceiling=600).selected_array['frequency']
    f = f[f > 0]
    median = float(np.median(f)) if len(f) else 0.0   # 0: Praat keeps the pitch where it is
    return parselmouth.praat.call(snd, 'Change gender', 75, 600, formant, median * pitch, rng, dur)

def write_ulaw(snd, out_path, trim=False):
    x = snd.values[0].astype(np.float32)
    cut = 'silenceremove=start_periods=1:start_threshold=-48dB'
    filters = ['highpass=f=160', BRIGHT] + ([cut, 'areverse', cut, 'areverse'] if trim else []) + ['alimiter=limit=0.95']
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'f32le', '-ar', str(WORK_RATE), '-ac', '1', '-i', '-',
                    '-af', ','.join(filters), '-ar', '16000', '-ac', '1', '-f', 'mulaw', out_path],
                   input=x.tobytes(), check=True, timeout=30)

def main(argv):
    if len(argv) < 3:
        sys.exit('usage: childlike.py in.aiff out.ul [strength 0..8 | chosen] [trim]')
    strength = int(argv[3]) if len(argv) > 3 and argv[3] != 'chosen' else CHOSEN
    snd = load(argv[1])
    if strength > 0: snd = smaller(snd, strength)
    write_ulaw(snd, argv[2], trim=len(argv) > 4 and argv[4] == 'trim')

if __name__ == '__main__':
    main(sys.argv)
