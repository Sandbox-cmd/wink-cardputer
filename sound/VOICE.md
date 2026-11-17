# Wink's own voice: a hum

> **Version 2, 17 November 2026.** The hum has moved toward a small, bright, sing-song child of a machine: a fourth higher, a little quicker, bouncier, with a little game-console colour and one chirp when delighted. Sections 1 to 6 below describe version 1, the one the owner heard and approved. Its numbers are version 1's. **Section 7 lists every change and gives version 2's numbers.** Version 1's code is kept whole in `sound/port/v1/`, and its WAVs are still in `sound/sketch/`. Version 2's are in `sound/sketch2/`.

Written by prelap (motion and sound), 16 November 2026, for the main session. **Unheard.** Every description of how it sounds below is predicted from renders that were measured and looked at as spectrograms, never listened to. No one had heard it through the device when this was written. The sketch that proves the recipe is `sound/sketch/hum.py` (the planner and renderer, written as the C will be) and `sound/sketch/render.py` (the examples and measurements).

## 1. What it is

Wink's own words are hummed with its mouth closed, the way a person hums a sentence ("mm-hm, hm-hmm?"): the tune and rhythm of what it is saying, none of the words. The closed mouth is the meaning: Samantha is the open voice of words someone composed (a written line, or the whole mind on the Mac), and the hum is Wink thinking aloud in words it made itself.

## 2. What a listener hears (predicted, unheard)

All four are in one small, warm, round voice, pitched a little above Samantha. It has one fixed resonance, its "case", at 1.65 kHz, so it stays the same creature in every mood. Each word is one connected swell, longer words have two or three pulses inside, and the hum dips between words without stopping. It stops only at punctuation.

- **Calm**, "It is quiet in here. The good kind of quiet." (`sketch/calm.wav`, 3.1 s): two even phrases at speaking pace, about four pulses a second. Each drifts down, and its last word glides home to the low C and settles. A third of a second of silence sits between the sentences. The second sentence is half a semitone lower and 1 dB quieter, as a person's would be.
- **A question**, curious, "Did you sleep at all, or is it coffee again?" (`sketch/question.wav`, 3.1 s): the first phrase lifts at "all" and stops for 180 ms as if to continue. The second runs quicker on its small words, then "again" climbs about four semitones to A and is left hanging there. The rise is the loudest moment of the line.
- **Cheeky**, "I saw that. I will say nothing, loudly." (`sketch/cheeky.wav`, 3.2 s): a swung, long-short rhythm, with stressed words scooped up into from below. "saw" is leaned on. "loudly" falls home and then flicks back up a third in one short, light afterthought: the hum's wink. It comes once per reply, at the very end.
- **Sleepy**, "It is late. The numbers can wait until morning." (`sketch/sleepy.wav`, 4.7 s): low, slow (2.4 to 3 pulses a second) and darker, with a little breath in it and soft onsets. The last note reaches home and sinks just under it, like a head nodding.

The seam with Samantha is in `sketch/open.wav` (the hum opens, then her "That's the Mac. Now we can really talk.") and `sketch/close.wav` (today's link-down line in her voice, then the hum closing back into itself and a calm line).

**Playing them to him on the Mac:** the Mac's speakers reproduce the low notes the device loses, which makes Samantha seem louder against the hum than she will be on the device. To judge the balance on the Mac, play `sketch/as-device/*.wav`. Those are the same six files passed through a model of the device's speaker (high-pass at 400 Hz, low-pass at 5 kHz), with one gain (+3.5 dB) for all six. To judge the sound itself, play the plain files, which are exactly what the chip would send.

## 3. The recipe

### 3.1 The unit

The unit is **one glided contour per word, with a pulse per syllable inside it**. Letters never sound on their own; they only decide how many pulses a word has, which one is stressed, how bright each is, and whether its onset is hard or soft. This is the line between this voice and the two things it must not be:

- one blip per letter is the video-game imitation;
- letters mapped to vowel sounds is the 1982 chip voice he could not follow.

### 3.2 Text to events (the planner, once per sentence)

**Tokens.** A word is a run of letters, digits and apostrophes. The marks that shape the sound are `, ; : . ? !` and `...` (treated as one trail mark). A newline counts as `.`. Every other character is silent. A phrase is the run of words up to a mark; the text's end counts as `.`.

**Syllables.** Count the groups of vowels (a e i o u y). Drop a silent final e: the word ends in e, not in le, has more than one group, and the last group is that e alone. A word gets at least 1 and at most 4. A number gets one pulse per digit, up to 4.

**Stress.**
- One-syllable function words are unstressed: a an the to of and or but is it its in on at for as be are was were i you me my your we he she they them this that with so if not do does did am by from up out just.
- Every other word is stressed on its first syllable, except two-syllable words of five letters or more that start with be de re un con com ex pre a, which take the second ("again", "until", "begin").
- The **main stress** of a phrase is its last stressed syllable.

**Duration of a pulse** (ms):
- 150 × mood tempo × (1 ± 0.08, fixed per word);
- × 1.30 if stressed, × 0.80 for a function word;
- the phrase's last pulse × 1.45 before `. ? ! ...`, or × 1.2 before `, ; :`;
- a question's last pulse × 1.1 more, and a trail's × 1.3.

**What sits between pulses:**

| Where | ms (× mood tempo) | What the sound does |
|---|---|---|
| between pulses inside a word | 0 | the last 18% of the pulse dips to 35% of its level; pitch glides on |
| between words | 45 (tender 30) | dips to 12% of the word's level (tender 30%, alert 0%) and keeps sounding; pitch glides on |
| `,` | 180 | silence |
| `;` `:` | 230 | silence |
| `.` | 340 | silence |
| `!` | 320 | silence |
| `?` | 360 | silence |
| `...` | 420 | silence |

**Pitch.** Pitch is counted in semitones above C4 (261.63 Hz), which is home: the device's chimes are already in C major (boot G4 C5 G5, link up C5 E5 G5 C6). For pulse k of n in a phrase:

- the phrase line is `L = top − 0.6 × s − 2.0 × range × k/(n−1)`, where s is the sentence's number in the reply (0, 1, 2, capped at 3), and top and range come from the mood;
- the main stress sits at `L + 3.0 × range`;
- another stressed pulse at `L + 2.0 × range`;
- a plain unstressed pulse at `L − 0.6 × range`;
- a function word at `L − 1.0 × range`;
- add a fixed nudge per word of ±0.4 st, from a hash of the word, so the same word always sits the same way;
- every pulse glides from its pitch p down to p − 0.5 across 87% of its length.

**The landings.** The phrase's last pulse is replaced by its landing. These are the only notes fixed to the key; everything between them is a continuous line, never snapped to a scale.

| Mark | Last pulse glides | Note |
|---|---|---|
| `.` | from max(p, 3) down to **C4 (0)** | home; curious lands on **D4 (2)** instead, left open |
| `!` | as `.`, starting 1 st higher, level 1.0 | |
| `,` `;` `:` | from p up to **G4 (7)**, or p+1 if already above | "more is coming" |
| `?` yes/no | from min(p, 5) up to **A4 (9)** | left hanging, off home |
| `?` starting who/what/where/when/why/how/which | from min(p, 3) up to **G4 (7)** | a smaller rise |
| `...` | from p down to p − 1.5 | no landing, a trail |

**Level of a pulse:**
- main stress 1.0, other stressed 0.86, plain 0.70, function word 0.58;
- × the mood's gain;
- −1 dB for each sentence after the first, to at most −3 dB.

The main stress is therefore the loudest beat of its phrase: measured in calm, cheeky and sleepy, the loudest pulse of the first sentence is its main stress. In the question it is the final rise.

**Event record** (about 32 bytes; a 90-character sentence makes at most about 40):

```c
typedef struct {
  uint32_t t_ms;        // start, from the start of the reply
  uint16_t ms;          // pulse length
  float    p0, p1;      // pitch at start and end, semitones above C4
  float    scoop;       // st below p0 to start from (0, or -1.5 / -2.0); rises over the first 25%
  float    amp;         // 0..1
  float    fc;          // brightness centre, Hz
  float    open, fc1, open1;   // mouth openness (0.25 for the hum); fc1/open1 only for the two transitions
  float    breath;      // noise mix
  float    floor;       // level held after the pulse until the next one (0 at punctuation)
  uint8_t  hard;        // hard onset: click and 5 ms attack
  uint8_t  attack_ms, release_ms;
  uint8_t  vib_cents_x10, vib_hz_x10;
  int16_t  reveal_to;   // text index to show when this pulse starts (end of its word, -1 if not a word start)
} HumEvent;
```

Onsets: the letter before the syllable's vowel (the first letter, for the first syllable) is hard if it is one of p b t d k g c q x, or always when the mood is alert. Hard onsets attack in 5 ms; soft ones in 22 ms (× 1.6 for sleepy and tender). Releases: 0 inside a word, 30 ms at a word's end, 40 ms at `, ; :`, 60 ms at `. ? ! ...`.

Brightness, from the syllable's vowel letter, × the mood's bright: a 950 Hz, o 860, u 820, e 1180, i 1420, y 1380. This moves only the colour of the hum by a few hundred hertz. It is not a vowel anyone could name.

### 3.3 The sound (the renderer, per sample at 16 kHz, in blocks of 512)

**Source.** A sum of harmonics of f0: k = 1..12, keeping only those below 3.8 kHz. Each harmonic's gain is recomputed every 32 samples (2 ms), then all gains are scaled to sum to 1, so the source can never exceed ±1:

```
a_k = k^-(1.2 - 0.8*open)                      // closed mouth: darker
    * (0.22 + L(f_k; fc, 380 + 500*open))      // the moving colour
    * (1 + 0.9 * L(f_k; 1650, 320))            // the case: one fixed resonance, the same in every mood
    * f_k^2 / (f_k^2 + 420^2)                   // nothing spent below what the speaker can play
L(f; c, b) = 1 / (1 + ((f - c)/b)^2)
```

Hum openness is 0.25. Use `expf(-tilt * logk[k])` with a 12-entry log table for the power. Phases are floats in [0,1), one per harmonic. Sine comes from a table: 257 entries with linear interpolation (1 KB) are enough, with error near -80 dB; the sketch uses 1025.

**Pitch.** f0 = 261.63 × 2^((st + jitter + vibrato)/12), computed every 32 samples, not every sample.
- `st` chases the event's pitch through a one-pole slew with τ 22 ms. A pulse's target moves linearly from p0 to p1.
- A new word may jump only when the level is below 0.02, which happens only after punctuation. Otherwise it glides.
- Jitter, every 32 samples: `j += 0.06*r − 0.02*j`, where r is uniform ±1 from xorshift32, seeded with a hash of the text and mood. The result is a slow wander of about ±0.3 st.
- Vibrato: 0.10 st at 5.2 Hz (sleepy 3.8 Hz, sad none). It starts 80 ms into pulses longer than 160 ms.

**Level.**
- `amp` chases its target through a one-pole slew: τ = attack/3 while a pulse sounds, release/3 after it.
- During a pulse the target is `amp_event × (1 − 0.18u)`, where u = 0..1 through the pulse (a held note sinks a little), and × 0.35 in the last 18% when the pulse is inside a word.
- After the pulse the target is `floor × amp_event`.
- Slew coefficients are `1 − exp(−1/(τ × 16))` for τ in ms, computed once per event.

**Breath and consonant.**
- White noise from xorshift32 goes through two one-poles: `lp += 0.72(r − lp)`, `hp += 0.33(lp − hp)`, `band = lp − hp` (roughly 1 to 3.2 kHz).
- Breath adds `amp × breath × 2.2 × band`, at half that in the gaps between words.
- A hard onset adds a click for its first 6 ms: `0.45 × (1 − t/6) × band × amp_event`.

**Out.** `y = amp × (source + breath term) + click`, then a one-pole low-pass `o += 0.80(y − o)` (about 4 kHz, which keeps the harsh band quiet), × 0.42, to int16. Hand each 512-sample block to the speaker as the recordings are, and feed every sample through the existing `envAdd` so the mouth follows it.

**Cost.** About 100 float operations a sample (12 harmonics), which is roughly 1.6 million a second, a few percent of one core. There are no delay lines. State is under 200 bytes, and the event list for one sentence about 1.3 KB.

**Level, measured.** Through a model of the speaker (400 Hz to 5 kHz), Samantha's recordings sit at -21.4 dBFS (speech-active loudness, median of 40 lines), and the calm hum at -23.4 dBFS: 2 dB under her, "under its breath". The other moods run from -20.0 (surprised) to -27.1 (sleepy). Peaks run from -9.1 to -13.5 dBFS. Every render has zero clipped samples. Between 51% and 78% of the hum's energy falls between 800 Hz and 3 kHz, and under 1% above 5 kHz. Without the speaker filter Samantha reads -16.1 dBFS, but 64% of her energy is below 400 Hz, which the device does not play. That is why the match is made through the band, and why the hum's numbers look lower than hers on a meter.

### 3.4 The eleven moods

Six numbers and one habit each. A mood moves the line between the landings, never the key and never the case resonance, so the sentence's shape stays in charge. The face carries the rest.

| Mood | top (st) | range | tempo | bright | breath | gain dB | habit |
|---|---|---|---|---|---|---|---|
| calm | 5 | 1.00 | 1.00 | 1.00 | 0.04 | 0 | none |
| happy | 7 | 1.30 | 0.92 | 1.10 | 0.03 | +0.5 | stressed pulses scoop up from 1.5 st below |
| delighted | 9 | 1.55 | 0.88 | 1.18 | 0.03 | +1 | the reply's last landing starts a major third higher: a hop before home |
| curious | 6 | 1.20 | 0.97 | 1.06 | 0.04 | 0 | statements land on D, not C: left open |
| thinking | 4.5 | 0.45 | 1.15 | 0.92 | 0.04 | -1 | the first pulse of each sentence held 1.6 × ("hmm") |
| sleepy | 2 | 0.50 | 1.40 | 0.80 | 0.12 | -3 | landings sink 0.7 st under home; vibrato 3.8 Hz; slower attacks |
| surprised | 8 | 1.40 | 0.90 | 1.14 | 0.03 | +1 | the first stressed pulse of a sentence leaps 4 st higher |
| sad | 3 | 0.60 | 1.25 | 0.85 | 0.07 | -3 | each word's end falls 0.7 st; no vibrato, no scoops |
| cheeky | 6 | 1.50 | 0.95 | 1.10 | 0.04 | 0 | swing (pulses alternately × 1.18 and × 0.80), scoops from 2 st below; the reply's last landing flicks back up to E for 95 ms at 55% level |
| tender | 3.5 | 0.80 | 1.15 | 0.84 | 0.08 | -2.5 | legato: between words the hum holds 30% and the gap is 30 ms; slower attacks |
| alert | 7 | 1.00 | 0.85 | 1.16 | 0.02 | +1 | staccato: pulses × 0.72, all onsets hard, true silence between words |

Measured on one line, "The kettle has gone quiet. Tea, then?":

| Mood | centre (st) | pulses/s | brightness centre (Hz) | loudness through the speaker (dBFS) |
|---|---|---|---|---|
| calm | 5.2 | 3.3 | 1130 | -22.1 |
| happy | 7.4 | 3.6 | 1192 | -21.2 |
| delighted | 9.1 | 3.7 | 1243 | -20.2 |
| curious | 6.3 | 3.4 | 1167 | -21.8 |
| thinking | 4.6 | 2.7 | 1084 | -23.2 |
| sleepy | 2.2 | 2.4 | 1070 | -25.4 |
| surprised | 8.4 | 3.6 | 1209 | -20.0 |
| sad | 3.2 | 2.6 | 1050 | -25.3 |
| cheeky | 6.5 | 3.4 | 1207 | -21.9 |
| tender | 3.9 | 2.9 | 1051 | -24.7 |
| alert | 7.2 | 4.8 | 1230 | -20.9 |

Closest pairs on these numbers:
- **thinking and tender** differ by 0.7 st and 33 Hz. They are told apart by the held first "hmm" against legato and breath.
- **happy and surprised** are told apart by the leap.
- **sad and tender** are told apart by the falling word ends against legato.

These three pairs are the ones I trust least to be heard as different. All eleven are in `sound/scratch/moods.wav`, in the table's order, with 0.5 s between them.

### 3.5 Timing against the text: the voice is the clock

- **Words appear on their onsets.** When a word's first pulse starts, the whole word is shown at once, and its punctuation is shown with its last pulse. `voice::spokenChars()` returns the `reveal_to` of the last pulse that has started; `os::speechShown()` already reads it. There is no letter-by-letter typing while it hums: each word lands on its own transient. The pace is about 3.4 words a second (calm), slower than today's silent typing at 34 characters a second.
- **One sentence of lookahead.** A question's rise must be planned before its last word sounds, so the hum never starts a sentence before the network has finished generating it: up to `. ? !`, a newline, or 90 characters. The order is:
  1. generate sentence 1 silently while the face shows thinking (about 10 to 20 pieces at 50 to 90 ms, so 0.5 to 1.8 s);
  2. plan it and hum it;
  3. generate sentence 2 while sentence 1 sounds.

  Humming (about 250 ms a word) is slower than generating, so the generator stays ahead. If the next sentence is not ready when the gap after `.` ends, the gap lengthens until it is. It is silence, never a filler sound.
- **Where it runs.** It is a new command in the voice task's queue (one task owns the speaker), next to `C_SAY` and `C_STREAM`: `C_HUM {text, mood}`. It never overlaps Samantha. When the two follow each other, at least 250 ms separates them.
- **Volume at 0.** Nothing is synthesised, and the words appear on the same clock, silently.
- **Sound joining mid-reply.** If the volume comes up while a reply is showing, the hum joins at the next word's onset, never inside a pulse. Tested by rendering: exactly zero before the join, and the step at the join (0.090) is smaller than an ordinary consonant click (0.210).
- **Hush** (the back key, or a tap on the edge button). The level target goes to 0 with τ 4 ms, the event list is dropped, and the rest of the text appears at once. Tested by rendering: -78 dBFS 30 ms after the key, with no click and no later consonant sounding. The strongest cut is a sound stopping, so hush should feel sharp. It does.
- **The mouth.** It follows the hum's real loudness through `envAdd`, so it dips between words (to 12 to 16% of its peak, measured) and closes at punctuation. A suggestion for the picture, not decided by me: cap the mouth's opening at about half while it hums, so the face shows a closed-mouth voice and opens fully only for Samantha.
- **Length.** A 90-character line hums for 5.2 s (`sound/scratch/long.wav`). For replies longer than three sentences, the hum stops at the end of the third, and the rest appears silently on the same clock.

## 4. The two transitions

**The whole mind arrives: the mouth opens.** In place of today's `CH_LINK_UP` (four square tones C5 E5 G5 C6), before Samantha's "That's the Mac. Now we can really talk.":

| ms | Sound | Picture |
|---|---|---|
| 0 to 200 | a closed hum on G4: brightness 700 Hz, openness 0.10, level 0.70 | |
| 200 to 530 | the pitch rises to C5 while the mouth opens: brightness 700 to 1750 Hz, openness 0.10 to 1.0, level 1.0, vibrato 0.12 st | the dots go finer at 200 ms, on the opening's onset |
| 530 to 640 | release | |
| +250 | Samantha begins | |

The hum's own sound opens into something nearer hers, then she speaks. The fall to home becomes a rise to home: home an octave up. In `sketch/open.wav` the gesture is 0.8 s, peaks at -10.8 dBFS and sits at -21.1 dBFS through the speaker model, level with her. Caption: [Wink's hum opens].

**The whole mind leaves: the mouth closes.** I argue the link-down moment itself should be silent: the face coarsens and nothing sounds. A network can drop by itself, and a sound then would be Wink talking unprompted, which he has set to never. Today that event plays `CH_LINK_DOWN` and Samantha's "The link is down..." on its own; whether to keep them is the main session's call. The return is heard instead at the head of the **next answer the small mind gives**:

| ms | Sound |
|---|---|
| 0 to 420 | an open note on C5 (brightness 1750 Hz, openness 1.0, level 0.85) falls to G4 while closing (to 700 Hz, openness 0.10) |
| 420 to 510 | release |
| +200 | the answer's hum begins |

It is the opening run backwards, so the pair reads as one mouth. In `sketch/close.wav` the closing and its line peak at -10.1 dBFS. Caption: [Wink's hum closes].

The link-up gesture plays at the event because the event follows his own act (he runs `wake`); the link-down gesture waits for his next question for the same reason.

## 5. What would make it annoying or cute, and what the recipe does about it

| The risk | What the recipe does |
|---|---|
| **Letter-rate chatter** (the game imitation): 10 to 15 blips a second reads as a gag | Pulses are per syllable inside a word's contour, 2.4 to 4.8 a second, the rate of speech. The hum keeps sounding through a phrase, so there is no train of separate blips. |
| **A squeak** | Mood centres run from D4 to A4 (294 to 440 Hz). Calm, thinking, sleepy, sad and tender never go above A4 (440 Hz). Happy, cheeky, curious and alert reach about B4 (470 to 500 Hz), delighted D5 (593 Hz), and surprised E5 (646 Hz) on its one leap. The fundamental sits mostly under what the speaker plays, so the voice is heard through its harmonics, which reads as round, not shrill. Harmonics stop at 3.8 kHz, and a 4 kHz low-pass follows them. |
| **Beeps** | It has a harmonic spectrum with a moving colour and a fixed body, 22 ms soft onsets, glides of 22 ms, a held note that sinks 18%, a slow wander and late vibrato. The first draft had silent gaps between words and its spectrogram showed a train of tones; that is why words now dip instead of stopping. |
| **A tune** | Only phrase ends touch the key (C, D, G, A). Everything between is continuous. Nothing is snapped to a scale. |
| **Sameness at the hundredth hearing** | Sentence structure (stress, length, punctuation) drives the contour, so different sentences hum differently. The same sentence always hums the same, and common words always sit the same way, as a person's habits do. That is identity, not a loop. |
| **Cuteness piling up** | Each mood has one habit, and the two showiest (cheeky's flick, delighted's hop) happen once per reply, at its end. No inhale before speaking, no "thinking" noise while it generates, no filler in waits: all three were cut. |
| **Cute where it must not be** | CHARACTER.md says that told someone is low, Wink does not joke. Sad has no vibrato, no scoops, a narrow range and -3 dB; its word ends fall by only 0.7 st, short of a whimper. If the reply concerns his wellbeing, use sad or tender, never cheeky. |
| **Too loud next to Samantha** | Matched through the speaker's band, 2 dB under her. It never overlaps her. |
| **Too long** | It stops after three sentences; hush cuts it in 20 ms. |
| **Pretending to be speech** | The colour moves a few hundred hertz with the vowel letter, which is too little to make a vowel anyone could name. No consonant has its own sound beyond a 6 ms click. |

## 6. What was checked, and what was not

Checked (by `sound/sketch/render.py`; numbers in `sound/scratch/measure.txt`, pictures in `sound/scratch/*-spec.png` and `*-wave.png`, each looked at):
- zero clipped samples in all 18 renders;
- the peaks above;
- the level against Samantha through the speaker model;
- the energy in each band;
- that the main stress is the loudest pulse;
- that the planned pitch line survives the speaker's band (an autocorrelation pitch tracker on the band-limited render agrees with the plan to within a semitone in 99 to 100% of voiced frames for the plain moods, 84 to 89% where scoops are not in the plan);
- that the mouth's level falls between words and at punctuation;
- that sound joining mid-reply and hush are click-free;
- all eleven moods on one line, measured apart.

Not checked:
- anyone hearing it;
- the device's real speaker (the model is two filters, not a measurement);
- the C port;
- the cost on the chip;
- whether a missing fundamental near 262 Hz still reads as pitch, rather than as timbre, on a 1 W speaker;
- the syllable counter on real generated text, which may include odd tokens;
- reading pace on the 240 × 135 screen.

## 7. Version 2, 17 November 2026: what changed

Written by a sound agent working alone overnight, for the main session. **Unheard, like version 1.** Every claim about how it sounds is predicted from renders, spectrograms and measurements. No one had heard it through the device when this was written.

**The brief.** Make the hum smaller, brighter and more earnest: a sing-song child of a machine. It should be higher, bouncier and a little quicker, with wider contours when happy, delighted or cheeky, and a little game-console colour. Delighted gets an occasional tiny chirp. Sad and tender stay plain and small. Nothing here imitates any existing character, voice, melody or sound effect.

**What stayed.** These are unchanged from version 1:
- the unit (one glided contour per word, a pulse per syllable);
- every planner rule for syllables, stress, marks, gaps and the reveal clock;
- the closed mouth (openness 0.25);
- the case resonance at 1.65 kHz;
- the logic of the landings;
- each mood's habit;
- hush, joining mid-reply, the two gestures' shapes;
- the C interface in `hum.h`.

### 7.1 The changes

| What | Version 1 | Version 2 | Why |
|---|---|---|---|
| Register | home C4 (262 Hz) | every pitch +5 st (`LIFT`): home **F4** (349 Hz) | Smaller and brighter. The 1 W speaker loses what sits under about 400 Hz, and calm's fundamental now mostly sits above that. |
| Landings | C4, D4, G4, A4 | F4 (statements), G4 (curious statements), **C5** (continuations and wh-questions), D5 (yes/no questions); cheeky's flick goes up to A4 | Every fixed note is still a note of C major, and the "more is coming" note is now C itself. |
| Ceiling | none (surprised reached E5, 646 Hz) | above E5 a pitch moves at half rate, and never goes past **G5 (784 Hz)** | Above about 800 Hz only four harmonics remain under 3.8 kHz, and the voice would turn to beeps. |
| Pace | tempo as in the table | × 0.90 for calm, happy, delighted, curious, thinking, surprised, cheeky and alert; sad, tender and sleepy keep version 1's tempo | A little quicker. |
| Bounce | stressed +2.0 × range; plain −0.6; function pulses × 0.80 | stressed **+2.3**; plain **−0.9**; plain pulses **× 0.88**, function **× 0.72** (stressed stay × 1.30) | Long-high against short-low, a hop on every beat. This also makes sad and tender 3 to 4% quicker. |
| Lean | every pulse falls 0.5 st | falls by the mood's **lean**: 0.8 happy, delighted, cheeky; 0.7 calm, curious, surprised, alert; 0.6 thinking; 0.5 sad, tender, sleepy | Each beat drops a little further, so the next one hops back up. |
| Lilt (new) | none | a phrase's stressed beats alternate up (+lilt × range) and down (−½ lilt × range); the main stress is left alone. lilt: calm .4, happy .6, delighted .7, curious .5, thinking .2, sleepy .1, surprised .5, sad 0, cheeky .6, tender 0, alert .3 | The sing-song. |
| Wider contours | range happy 1.30, delighted 1.55, cheeky 1.50 | **1.60, 1.90, 1.80**; their tops come down by 0.5 to 1 st (happy 6.5, delighted 8, cheeky 5.5), so the widening goes down as well as up | Wider, without living at the ceiling. |
| Console colour (new) | none | a share of a 25% pulse wave's spectrum (\|sin(πk/4)\|/k, through the same 420 Hz high-pass) is mixed into the 12 harmonic gains before they are made to sum to 1. Shares: calm .22, happy .28, delighted .32, curious .25, thinking .20, sleepy .12, surprised .28, sad .10, cheeky .30, tender .10, alert .25; gestures .15; the chirp .5 | A hollow, slightly buzzy edge, while staying a voice. It cannot clip, cannot alias, and costs nothing per sample. |
| Colour centre | vowel colours × the mood's bright | × 1.12 more | A smaller mouth's resonances sit higher. The case stays at 1650 Hz. |
| Delighted's chirp (new) | none | once per reply, after the first phrase that ends in `!` or `,`, never the last phrase (which has the hop). It starts 30 ms after the landing, jumps to **C5** and glides up to **F5** (home an octave above) in 65 ms, at 45% level, attack 4 ms, release 25 ms, console .5. It adds 105 ms to the clock. | The one tiny chirp. It always uses the same two notes, so it belongs to the voice. |
| Gestures | open G4 → C5; close C5 → G4 | lifted: open **C5 → F5**, close **F5 → C5** | Still a rise to home an octave up. |
| Level | MASTER 0.42 | **0.38** | The lifted hum put about 0.9 dB more into the speaker's band. Calm through the speaker model is now −23.4 dBFS (version 1: −23.5), still 2 dB under Samantha. |
| C structs | HumEvent 84 bytes, Hum 684 | HumEvent **88** (adds `console`), Hum **696** | |

**Why +5 and not +7.** A lift of +7 would land statements on G and push delighted and surprised further into the ceiling. +5 keeps C as a landing (continuations and wh-questions) and was the first designer's own suggestion. It does mean a statement now comes home to **F**, not C. If his ear hears F as "not home", the lift is one constant (`LIFT` in hum.py, `LIFT` in hum.c). +7 gives G, and +12 gives C5 but puts calm's centre near 700 Hz.

### 7.2 Version 2, measured

Version 1's figures are in brackets. These are the eleven moods on "The kettle has gone quiet. Tea, then?", from `sound/sketch2/render2.py` (numbers in `sound/sketch2/measure.txt`):

| Mood | centre (st above C4) | highest planned (Hz) | pulses/s | brightness centre through the speaker (Hz) | loudness through the speaker (dBFS) |
|---|---|---|---|---|---|
| calm | 10.5 (5.2) | 587 | 3.7 (3.3) | 1170 (1130) | −21.9 (−22.1) |
| happy | 12.6 (7.4) | 674 | 4.0 (3.6) | 1213 (1192) | −21.0 (−21.2) |
| delighted | 14.2 (9.1) | 722 | 4.5 (3.7) | 1251 (1243) | −20.3 (−20.2) |
| curious | 11.7 (6.3) | 624 | 3.8 (3.4) | 1198 (1167) | −21.6 (−21.8) |
| thinking | 9.8 (4.6) | 587 | 3.0 (2.6) | 1125 (1084) | −22.9 (−23.2) |
| sleepy | 7.3 (2.2) | 587 | 2.4 (2.4) | 1104 (1070) | −25.1 (−25.4) |
| surprised | 13.8 (8.4) | 754 | 4.1 (3.6) | 1262 (1209) | −20.0 (−20.0) |
| sad | 8.4 (3.2) | 587 | 2.7 (2.6) | 1098 (1050) | −24.9 (−25.3) |
| cheeky | 11.8 (6.5) | 666 | 3.8 (3.4) | 1221 (1207) | −21.7 (−21.9) |
| tender | 9.1 (3.9) | 587 | 3.0 (2.9) | 1094 (1051) | −24.3 (−24.7) |
| alert | 12.5 (7.2) | 639 | 5.3 (4.8) | 1255 (1230) | −20.7 (−20.9) |

How to read the table:
- The "highest" is 587 Hz in most moods because that is the yes/no question's landing on "then?" (D5).
- On the longer lines, delighted touches the ceiling at 784 Hz.
- Pulse rates stay within speech rate: 2.4 to 5.3 a second.

The four examples are shorter: calm 2.75 s (3.07), question 2.69 s (3.07), cheeky 2.82 s (3.17), sleepy 4.58 s (4.70).

What else was measured:
- **Clipping.** Every render has zero clipped samples.
- **Peaks.** They run from −9.8 to −14.2 dBFS.
- **Main stress.** By peak it is the loudest pulse in ten of the eleven moods. In sleepy it ties with the next one (0.200 against 0.202); by RMS the main stress is louder (0.075 against 0.071).
- **Landings.** Tracked through the speaker model, the landings sit within 0.6 st of their notes: F4 heard at 5.3 st, C5 at 11.8, D5 at 13.5. The one exception is delighted's last landing, which falls 10 st inside one pulse and ends 0.9 st sharp.
- **Hush.** It still leaves −80 dBFS 30 ms after the key, with no click.

### 7.3 What a listener should hear (predicted, unheard)

- The same small closed-mouth creature, a fourth higher and about 10% quicker.
- Each stressed word hops up and drops back, and the beats of a phrase go high, lower, high in a sing-song.
- Happy, delighted and cheeky swing wider.
- A faint hollow, buzzy edge sits under the hum, most in cheeky and delighted, and least in sad and tender, which stay small and narrow.
- When delighted, one quick "bip" up to the high F after the first comma or exclamation.

**The risks I trust least:**
- The console share could read as cheap or buzzy rather than charming. It is one number per mood.
- The lilt could read as mechanical alternation on long phrases.
- Home on F may feel unresolved next to chimes that end on C.

### 7.4 The C port of version 2

`firmware/lib/hum/hum.c` and `hum.h` match the new hum.py. `sound/port/compare.py` has three changes:
- it now checks 30 cases, which adds the mood line in all eleven moods, delighted's chirp among them;
- it compares against `sound/sketch2/`, not version 1's WAVs;
- it checks the new `console` field.

All cases pass its thresholds; the table is in `sound/port/scratch/results.md` and in `sound/REPORT2.md`. The cost per sample is lower than version 1's, because higher notes leave fewer harmonics under 3.8 kHz: 0.84 to 0.90 of version 1's time across the eleven moods on this Mac. What it costs on the chip is still unmeasured.
