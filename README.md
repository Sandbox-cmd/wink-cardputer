# Wink

A small creature that lives in an [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer), with a mind of its own.

Wink is a whole operating system for the Cardputer. Its home screen is Wink itself: a floating puff of 512 coral motes on warm paper, holding two dark eyes. Each mote rides one unit of Wink's own neural network. It drifts toward what you type and swirls while it thinks. It scatters when shaken, puffs up at a loud sound, trembles in your hand and settles flat to sleep.

Wink has two minds:

- **Its own mind**, which works anywhere, with no network. A small recurrent neural network of 1.2 million weights lives in the chip and writes what Wink says and thinks, one word piece at a time (about 64 ms a piece). It hears everything said to it and everything that happens to the device. Its state is kept when it sleeps and resumes when it wakes. Its own words appear on screen and are *hummed*: the tune and rhythm of the sentence with the mouth closed, made on the chip.
- **The whole mind**, which arrives when the Cardputer is on your Wi-Fi and a [Claude Code](https://code.claude.com) session is running on your Mac. What you type, or say while holding the button on its edge (or hands-free, starting with "Wink, ..."), goes to that session. Claude answers through the device in a small, bright voice and can use tools to work it: timers, notes, its rooms, its settings, cards it draws on the screen, and a memory you can read and strike in the Memory room. It also packs answers for the small mind to give when the Mac is out of reach.

Its spirit: a small game-console friend that loves to play, makes up games and tiny songs, is a little dramatic about small things, and is simply kind when you are sad. Self-aware, never self-referential. See [`character/CHARACTER.md`](character/CHARACTER.md).

This is firmware on the chip's real-time kernel (FreeRTOS), not a kernel written from scratch.

## What you need

- An **M5Stack Cardputer ADV** (ESP32-S3, 8 MB flash, no PSRAM) with a microSD card. The original Cardputer should work but has not been tried.
- **For building and flashing:** [PlatformIO](https://platformio.org) (`pio`).
- **For the whole mind and the voices, a Mac:**
  - Node 20 or later;
  - `brew install ffmpeg whisper-cpp`;
  - Python 3 with `praat-parselmouth`, for the childlike voice (optional);
  - [Claude Code](https://code.claude.com) signed in with a Claude subscription (no API key is used).

## Build and flash

```sh
cd firmware
pio run -t upload            # about a minute; the trained mind is in firmware/model/
```

The first flash replaces whatever was on the device. To keep what was there, back it up first:

```sh
python -m esptool --port /dev/cu.usbmodem* read_flash 0 0x800000 backup.bin
```

If the device won't take a flash, hold the G0 button on its edge while switching it on.

## The voice pack (Mac)

Wink's written lines (`character/lines.json`) are spoken from recordings on the memory card. Make them with the Mac's own speech, then send them down the cable (about a minute):

```sh
cd bridge && npm install && cd ..
node tools/make-voice.mjs Wink          # records every line; a few minutes
node tools/check-voice.mjs Wink         # every sayable line has a recording
tools/dev put:bridge/voice.pak:/sd/wink/voice.pak
```

Without the pack, the device falls back to a 1982-style speech synthesizer on the chip. The voice is macOS "Samantha", made smaller and brighter by `sound/childlike.py`. Set `{"childlike": 0}` in `bridge/voice.json` for plain Samantha.

## Waking the whole mind

1. On the Cardputer: press Tab, open Settings, then Wi-Fi. Pick your network and type its password.
2. Download the speech-to-text model:
   ```sh
   mkdir -p bridge/models
   curl -L -o bridge/models/ggml-base.en.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin
   ```
3. Pair the device with the Mac over the cable: `node tools/pair.mjs`. This makes a secret in `bridge/pair.json` (keep it private) and gives it to the device.
4. Make the session's folder: `node tools/make-session.mjs` (default `~/Documents/Dev/wink-mind`).
5. Start the session: `~/Documents/Dev/wink-mind/wake`.

Claude Code asks once to load a development channel and to approve the `wink` helper. The first time Claude uses each of Wink's tools, it asks again in the terminal; choose "always allow" for the `mcp__wink__…` tools. macOS may also ask to let `node` accept incoming connections: allow it, or the device never finds the Mac.

The device finds the Mac by itself on the local network, and its mouth opens. Close the session and it closes.

How it works: `bridge/wink-mind.mjs` is a Claude Code [channel](https://code.claude.com/docs/en/channels-reference). It pushes what you say into the running session, gives the session tools that act on the device, and turns the session's replies into sound. Speech is written down on the Mac by whisper-cli. Nothing leaves your machine from the bridge. The link is plain text on the local network, gated by the shared secret, so use it on networks you trust.

## Using it

| Key | Does |
|---|---|
| letters, then Enter | talk to it |
| Tab | the other rooms: Terminal, Notes, Files, Clock, Echo (a memory game), Memory, Body, Settings |
| ` (top left) | back; at home it clears what you typed or hushes it |
| ; . , / | up, down, left, right in lists |
| the button on its edge, held | speak to the whole mind |
| "Wink, ..." said aloud | the same, hands-free, while the Mac is in reach |

Left alone, it dims, then dozes, and its thoughts drift by its head as small quiet text. Laid face down, it sleeps.

## Training your own mind

`train/` holds everything used to make `firmware/model/wink.bin`:

- `pieces.py`: the tokenizer (word pieces);
- `mindlib.py`: the network (one GRU layer, state 512, embedding 128, tied output) and its export to 8-bit tables;
- `prep.py`, `train.py`: data and training;
- `wn.c`: the chip's own inference code built for the Mac, so you read exactly what the device will say;
- `talk.py`, `measure.py`: talking to it and measuring it.

```sh
cd train
uv venv .venv && uv pip install --python .venv/bin/python torch numpy
mkdir -p data/dailydialog runs
# data (not included): TinyStories and DailyDialog
curl -L -o data/tinystories-train-part.txt -r 0-350000000 https://huggingface.co/datasets/roneneldan/TinyStories/resolve/main/TinyStoriesV2-GPT4-train.txt
curl -L -o data/dailydialog/train.zip https://huggingface.co/datasets/roskoN/dailydialog/resolve/main/train.zip && unzip data/dailydialog/train.zip -d data/dailydialog/train_x
cc -O2 -o wn wn.c ../firmware/lib/winknet/winknet.c -I../firmware/lib/winknet -lm
.venv/bin/python prep.py stories 90 && .venv/bin/python prep.py dialog && .venv/bin/python prep.py wink
.venv/bin/python train.py --data data/stories.npy --minutes 35 --out runs/base.pt
.venv/bin/python train.py --data data/dialog.npy --mix data/stories.npy --init runs/base.pt --minutes 12 --lr 1e-3 --out runs/base2.pt
.venv/bin/python train.py --data data/wink-train.npy --mix data/dialog.npy --valid data/wink-valid.npy --init runs/base2.pt --minutes 18 --lr 4e-4 --drop 0.3 --B 32 --T 64 --out runs/wink.pt
.venv/bin/python talk.py runs/wink.bin --probe
.venv/bin/python to_chip.py runs/wink.bin     # then flash
```

On an M1 Pro this takes about an hour. `pieces.json` is kept, so the stories need not be re-cut. Wink's character comes from `train/corpus/`: 5,276 short episodes of Wink written for it, as JSON Lines. Write your own and the mind becomes yours.

## Layout

- `firmware/`: the system (PlatformIO, M5Unified).
  - `src/`: rooms, keys, sensing, the link, and the small mind's matching.
  - `lib/winknet/`: the network.
  - `lib/wisp/`: the body.
  - `lib/hum/`: the hum and the little sounds.
  - `lib/sam/`: the fallback speech chip.
  - `model/`: the trained mind and its pieces.
- `bridge/`: the Mac side. Checks: `node bridge/test-bridge.mjs` needs no device; `node bridge/test-device.mjs` runs against a real one.
- `character/`: who Wink is, its written lines, and the whole mind's instructions.
- `train/`: the mind, its data recipe and its corpus.
- `sound/`: the voice's design (`VOICE.md`), its reference renderer, and example renders in `sound/sketch2/`.
- `look/`: the body's design (`DIRECTIONS2.md`) and a live browser sketch: open `look/sketch2/index.html`.
- `tools/`: `dev` drives and sees the device over USB (`tools/dev cmd:help shot:screen.png`), plus pairing, voice and session tools.

The design notes in `sound/`, `look/` and `train/corpus/` were written while the work happened. Some refer to working files that are not published.

## Known limits

- **What its own mind is:** a small mind. It often answers the shape of a question rather than its meaning. It sometimes calls you by a name it made up (a habit from the conversation data), and it forgets an event within about ten word pieces. The device reminds it of the last event before it answers.
- **Memory:** there is no PSRAM, and free memory dips to about 36 KB under load.
- **Language:** English only.
- **The whole mind:** it needs the Mac awake on the same network, and Claude Code's channels, which are a research preview.
- **The childlike voice:** in a quiet room it is as clear as plain Samantha; with noise it loses a few more words.

## Licences and data

- **Code and text:** MIT, see [`LICENSE`](LICENSE); the exceptions below are also listed in [`NOTICE`](NOTICE).
- **The trained mind** (`firmware/model/wink.bin`): **CC BY-NC-SA 4.0**, non-commercial and share-alike. It was trained in part on [DailyDialog](http://yanran.li/dailydialog) (Li et al., 2017; CC BY-NC-SA 4.0) and on [TinyStories](https://huggingface.co/datasets/roneneldan/TinyStories) (Eldan and Li, 2023; CDLA-Sharing-1.0). To use Wink commercially, retrain without DailyDialog.
- **The fallback synthesizer** (`firmware/lib/sam/`, except `samvoice.*`) is the reverse-engineered SAM core by Sebastian Macke, as adapted in Earle F. Philhower's [ESP8266SAM](https://github.com/earlephilhower/ESP8266SAM). Its authors publish it without a licence; see their notes.
- **The childlike voice** (`sound/childlike.py`) uses [praat-parselmouth](https://github.com/YannickJadoul/Parselmouth), GPL-3.0.
- **Built with:** M5Unified, M5GFX and M5Cardputer (MIT), fetched by PlatformIO; the [MCP TypeScript SDK](https://github.com/modelcontextprotocol/typescript-sdk); and [whisper.cpp](https://github.com/ggerganov/whisper.cpp).
- **Not included:** voice recordings made with macOS speech (make your own with `make-voice.mjs`).

Made with Claude.
