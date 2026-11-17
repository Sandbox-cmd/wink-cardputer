# Wink: three creatures (round two)

Written by dwell, 16 November 2026, after round one was turned down for four reasons: wrong kind of creature, too dark or unsettling, not alive or rich enough, lost the character it had.

**The sketch is the argument:** `look/sketch2/index.html`. Open it in Chrome; it runs at 30 frames a second, 240 × 135 shown at 4×, in 16-bit colour. Type on the keyboard to talk; Enter sends, and it thinks and then hums an answer (or speaks it, with the whole mind here). The arrow keys tilt it. Buttons cover think, hum, whole mind arrives/leaves, sleep, wake, pick up, shake, a loud sound, and the moods calm, pleased, curious, cheeky and low. Left alone, a thought drifts by as small text after about 6 s; after 45 s it gets sleepy and sleeps. The mind-driven parts run on 1,800 real pieces of Wink's network (N = 512). Measured behaviour is kept: the state settles toward the model's own mean in quiet, and effort is normalised by the model's own spread.

**Kept from the dot face in all three:** its eye language, in the same table of eleven expressions from `face.cpp` (lids, slope, the lower-lid smile, the cheeky wink, the glint). Also kept: the coral (232,124,88) and the per-mood tints, your blue for what you type, ink for text, blinks, and the gaze that drops to the cursor while you type.

**What is new in all three:** a body with weight under real gravity (tilt), and habits. It goes somewhere, hops, sleeps in a heap, scatters when shaken, startles at a loud sound, and lets its thoughts drift past. The mind sets the pace: **restlessness** (how far the live state is from its rest) shortens its idle pauses and quickens its moves, so it fidgets while it thinks and grows still in a quiet room. Each piece it writes is felt in the body, and its glances lean where the mind's first two principal directions point.

---

## 1. Mochi: the dot face grows a body

**What it is.** The face he liked, made of the same lit coral dots, now a soft jelly body that sits on the floor, squashes, slides when tilted, hops toward your cursor, and melts into a loaf to sleep. This is the "evolve, don't replace" answer.

**Recipe.**
- **Body:** a ring of 28 points, Verlet with 3 substeps a frame. Each substep pulls the points back toward their rest oval (46 × 33 px; asleep 56 × 22) by 0.11 to 0.16 (shape matching with rotation), so it wobbles and recovers. Gravity is 260 px/s² along the real tilt. The floor is the top of the text band.
- **Habits:** hops every 2.5 to 7 s, divided by (0.5 + energy) and shortened by restlessness; toward the cursor while you type. A loud sound gives a high hop. Picked up, it floats and trembles; put down, it lands and squashes. Waking is a stretch and a hop.
- **Material:** the device's own lattice: 5 px cells, a dot of 1 to 4 px by brightness, and 3 px cells sweeping in from the middle when the whole mind is here (as today).
  - Body cells 0.075 + 0.075 × light (from the upper left) + 0.05 × the unit at that cell's fixed address, scaled by restlessness. The rim is +0.2. The eyes and mouth come from `face.cpp`'s field, squashed with the body.
  - Each piece it writes is a small ring rising inside the body (size 1.2 + 2.6 × effort) plus a nudge to the nearest ring point. Your pieces and the Mac's carry a fleck of blue or ink.
- **Humming:** the ring trembles with the loudness and the closed mouth ripples.
- **Cost on the chip (estimated):** the body is filled by spans per row (27 rows × 28 edges ≈ 760 operations), not a point test per cell. Cells: 1,296 coarse (3,600 fine with the whole mind) × about 40 operations = 50k to 150k. Dots ≤ 20k pixel writes. Physics about 4k. Clear 32k. **About 120k to 250k operations a frame; no extra memory.**
- **Palette:** ground (16,11,11) with a faint lattice; dots on today's ramp, near-black → mood tint (calm (232,124,88), low (150,140,190), asleep (170,105,140)) → pale (255,240,226); the whole mind tints the bright dots toward ink (255,237,219).
- **Ground:** dark warm, because its material is light. On a light ground the lit dots lose their contrast and the charm with it.

**How it fails at 29 mm.** It is the old face in a bag. It is the safest but the least new, and he may read it as "features and bells and whistles". At 5 px the silhouette edge is coarse, and in motion the squash is what sells it; a still undersells it.

## 2. Wisp: a puff of motes on warm paper (my pick)

**What it is.** A small warm cloud of 512 coral motes, one riding each unit of its mind, holding the dot face's eyes. It floats to where its attention is, swirls while it thinks, bristles at a noise, scatters when shaken and gathers itself again, and settles flat on the floor to sleep. It is light and warm, and every mote is moving for a reason.

**Recipe.**
- **The puff's centre** is a damped spring toward a goal. The goal is the cursor (20 px right of it, just above the text) while you type, higher while it thinks, the floor while it sleeps, and otherwise a new place every 4 to 9 s, sooner when restless. Tilt adds a sideways drift of 90 px/s² × sin(tilt). Shake adds large random accelerations.
- **Each mote i** has a home on a sunflower spiral inside the puff (34 × 28 px), its unit `ORDER[i]`, and an orbit around that home:
  - orbit radius 0.6 + 4.5 × |dev| × (0.25 + restless) px;
  - angular speed (0.4 + 3 × restless) × (0.5 + |dev|), turning by the sign of dev.
  It follows its target with a spring (k 38, damping 7). So when the puff moves, the motes lag and stream: its trails are its inertia.
- **Events:** a loud sound puffs it out to 1.4× size for 0.5 s. A piece sends a darker ring out through the motes (strength 0.4 + 0.6 × effort). Humming ripples the motes outward from the mouth with the loudness.
- **Words from outside:** your words send 5 blue motes up from the cursor into the puff for 4 s. The Mac's pieces send 2 ink-violet motes falling from the top edge to join it; they stay while it is linked and float off when it leaves.
- **Eyes and mouth:** the dot face's shapes, drawn solid in dark ink (52,26,28) with a white glint, at half the old size. A soft shadow on the floor shrinks and pales as it rises: drawn depth.
- **Trails:** the frame is never cleared; every pixel fades toward the paper. **Lesson from the sketch:** a fractional fade in a 16-bit frame stalls short of the target, and since red and blue step by 8 and green by 4, the stall leaves a **green stain**. Each fade step must move every channel by at least one of its own steps.
- **Cost on the chip (estimated):** the fade, over the puff's bounding box plus 30 px (about 10k pixels) × 3 channels × about 6 operations = 180k. Paper colour: computed by a hash per pixel, not stored, because there is no memory for a second frame. Motes 512 × about 35 operations = 18k plus 2k pixel blends. Eyes about 40k. **About 250k to 300k operations a frame. Memory: 512 motes in int16 fixed point, about 5 KB** (the free minimum is about 40 KB, so check this).
- **Palette:** paper (246,235,220) with a ±3 grain at fixed addresses; motes from deep (150,46,40) at the core to the mood tint at the rim (calm (232,124,88), low (150,140,190)); the thought ring darker; eyes (52,26,28) with glint (255,248,238); your motes (60,112,160); the Mac's (112,72,150); text band (236,222,205) with ink text (52,38,36).
- **Ground:** light. A creature of pigment on paper is warm and friendly, and a lit IPS carries a light ground well.

**How it fails at 29 mm.** Its eyes are 19 × 23 px (2.3 × 2.8 mm) and read well. But big dark rounded eyes on a fluffy ball can tip into **generic cute mascot**. CHARACTER.md's dryness has to come from its behaviour (the cheeky wink, the deadpan stillness, where it chooses to float), not from its face. The 2 px motes merge into fuzz at arm's length; that is the intent. The Mac's motes are few and small, so the arrival is mostly told by the ink stream at the moment of arrival. Light ground costs battery on an IPS (the backlight is on either way, so less than on OLED; not measured).

## 3. Pip: a small walker with the glowing eyes

**What it is.** A dark warm bean on two legs, with the dot face's coral eyes glowing in it and a sprung curl on its head. It lives at the bottom of a dusk sky: it wanders, walks over to stand beside what you type, braces against a tilt and slides when it is steep, dangles its legs when picked up, tumbles when shaken, and curls up to sleep. When the whole mind arrives, a moon rises in its sky and lights its other side.

**Recipe.**
- **States:** idle, walk, attend, think, hum, sleep, held, tumble.
  - Idle pauses last 1.5 to 6 s, shortened by restlessness. The chance of walking off is 0.3 + 0.6 × restless + 0.3 × energy, within ±110 px.
  - Walking speed is 24 px/s × (0.6 + 0.6 × restless + 0.4 × energy), and 0.6× when low.
  - Legs 9 px; two-phase walk with 3.5 px stride and 2.5 px lift.
  - Body 40 × 36 px superellipse (exponent 2.3); asleep 46 × 28 with no legs.
- **Tilt:** gravity 300 px/s². Under 0.42 rad it leans uphill and walks slowly; over it, it slides and scrambles. The body leans by -0.5 × the slope.
- **The curl** is a damped spring (k 90, damping 7) on the head, so it lags every move and droops when low or asleep. **Its tip flashes with each piece the mind writes** (0.25 + 0.5 × effort s; blue for yours, ink for the Mac's) and sometimes lets a spark float up.
- **Lighting:** a vertical dusk gradient. The body is dark plum with a rim light from the warm horizon, a soft top light, and, with the whole mind here, a cool ink rim from the moon (radius 7.5 px, halo 34 px, rising to y 26 in about 2 s).
- **Cost on the chip (estimated):** the sky, one colour per row (32k pixel writes, near memset); body 44 × 40 px × about 35 operations = 62k; eyes about 35k; legs and curl about 2k pixel writes; moon halo 37k when present. **About 150k to 200k operations; no extra memory.**
- **Palette:** sky (92,58,84) at the top to (236,156,118) at the horizon; floor (70,38,44) with a (255,190,150) edge; body (74,40,52) with rim (255,150,110); eyes in the mood tint, lifted toward (255,240,226); curl (84,46,58); moon (255,240,226).
- **Ground:** a mid-tone sky. The body is a silhouette, so the ground must be lighter than the body yet dark enough for the glowing eyes. Dusk does both and is warm.

**How it fails at 29 mm.** It is the smallest face of the three (eyes about 14 × 17 px, 1.7 × 2 mm); expressions read, but subtly. With legs and a stalk it can read as **a generic game robot**; I changed the antenna to a curl for that reason. The sky gradient will **band** in 16-bit colour (about 20 red steps over 135 rows). Use a fixed 2 × 2 ordered dither at fixed addresses, or accept the bands. The moon pulls the eye away from Pip.

---

## My pick: Wisp

It is the only one that answers all four of his ticks at once:
- **another kind of being,** not a face on a screen and not a mascot with limbs;
- **light and warm;**
- **the richest life on the screen:** 512 motes, each moving because its unit moves, streaming with inertia, scattering and re-forming;
- **it keeps the character:** the eyes he has been looking at all evening, the coral, the blink, and the gaze at the cursor.

What it costs if I am wrong: the eyes over a fluffy ball can tip into cute. I would watch that first, and the fix is in behaviour (deadpan stillness, the cheeky wink, choosing where to float), not in drawing.

**Where I would take liberty if he likes Wisp:** give it Pip's sense of place. Small things in its world that it drifts to and back from, a moon of its own when the whole mind is here, and the floor it sleeps on.

**What I would refuse in any of them:**
- a costume (scanlines, CRT glow, a robot visor);
- a mind diagram (a grid or graph of the 512 numbers);
- any motion without a cause. Every movement here is tilt, a hand, a sound, a typed word, a piece of its mind, or a habit timer scaled by restlessness.

## What I checked, and what I could not

- **Checked:**
  - a syntax check after every change;
  - the page runs headless at 30 frames a second (600 frames in 20 s of virtual time) with no script errors;
  - 12 scripted states per creature, looked at as stills at 2× (`look/scratch/s2-*.png`);
  - the live page at 4× (`live-wisp.png`).
- **Faults the stills caught and fixed:**
  - Mochi's eyes were drowned by a bright body;
  - Pip was too small and its stalk read as an antenna;
  - thoughts landed on the face;
  - the green stain from the fade;
  - the Mac's motes read as dirt.
- **Not checked:**
  - motion in his real Chrome (the coordinator will open it);
  - any of it on the device;
  - real frame times on the chip (every cost is an estimate from counting operations);
  - the hum, which is a made-up loudness envelope, not the real closed-mouth hum.

Files: `look/sketch2/index.html`, `look/sketch2/lines.js` (lines copied from `character/lines.json`); scratch `look/scratch/sheet2.html`, `shot2.mjs`, `check2.js`, `s2-mochi.png`, `s2-wisp.png`, `s2-pip.png`, `live-wisp.png`; and `look/sketch2-lines.tmp.json`, a stray intermediate I wrote in `look/` and cannot delete without `rm` (safe to delete).
