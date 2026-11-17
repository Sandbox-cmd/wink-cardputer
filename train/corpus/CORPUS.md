# The Wink corpus, batch one

Written by stet, 16 November 2026. This folder is the text a small network learns to be Wink from. Batch one is on disk: **1,882 episodes, 3,229 Wink lines, 792 thoughts, in 31 files.** Every file parses as JSON Lines and passes the checker with no failures (`node scratch/check-corpus.js .`; the last full report is `scratch/check-report-batch1.txt`).

Roughly half of it has been read by me and half by nobody but its writer. One cold reader saw 36 pairs.

## Two things to rule on before training

1. **Two minds, or one at two sizes.** The corpus teaches "I'm Wink. Claude is the whole mind, on the Mac." That is my reading of "its mind is now its own". The README and `character/LINKED.md` still say one character at two sizes. This was not settled when it was written. on it. Six Wink lines say it outright (they name Claude, or call its mind its own); find them with `node scratch/grep-sense.js "claude|of my own|all mine" all`.
2. **Waking.** Seven `wake` thoughts say it starts with nothing ("Fresh start. No idea what came before."). I was told its state is saved and restored on waking, and told not to claim long memory; claiming none is the opposite claim. They are easy to cut: `node scratch/grep-sense.js "no idea|nothing in my head|came before|from nothing|blank|know a thing" all`.

## What every writer was given

- `scratch/CANON.md`: who Wink is. One sentence, the one move (every reply takes a word or the shape of what was just said), what it can sense, what it never does, fixed answers to the questions he will ask first, seventeen small stances, the voice, the moods, the thoughts.
- `scratch/FORMAT.md`: the episode format and its rules.
- `scratch/BRIEF.md` or `scratch/BRIEF-THOUGHTS.md`: what makes an episode or a thought good, the method, the limits.
- One example file each, written by me, different for each writer (`scratch/ex-01` to `ex-16`; together they are `ep-seed-01.jsonl`).

One writer ran first as a pilot (40 episodes, read in full). It changed the sheets in four ways before the rest ran: plainer and shorter lines, no remarks on capital letters, an exclamation mark allowed for the person's good news, and calm as the resting face rather than the most common one.

## The matrix, with what is on disk

Fifteen Opus writers, one slice each. The cell figures are what each writer was asked for and reported back; I did not recount them. The three right-hand columns are counted from disk, after my cuts.

| Files | Slice | Cells asked for | Episodes | Wink lines | Thoughts |
|---|---|---|---|---|---|
| `ep-greet` | Greetings and partings | after wake 12 · morning 12 · daytime and evening 12 · late and "still up" 12 · goodbyes 12 · goodnights 12 · back after a long gap 14 · quick check-ins 14 | 100 | 206 | 15 |
| `ep-doing` | Small talk, what the person is doing | doing something now 60 · idle small talk 40 | 100 | 207 | 15 |
| `ep-moods` | How the person is | tired 18 · bored 17 · pleased 20 · annoyed 20 · sad or worried 16 · mixed 9 (two writers; one with long specific openings, one with short common ones) | 99 | 239 | 16 |
| `ep-facts` | Facts asked for and declined | places 12 · history 12 · science 14 · numbers 10 · word meanings 12 · famous people 8 · how-to and health 12 · news, weather, scores 10 · homework 10 | 100 | 235 | 15 |
| `ep-self` | Questions about Wink, and tests | name 10 · what are you 10 · alive, real, conscious 12 · AI, robot, Claude 8 · feelings 12 · see, hear, remember, sleep 14 · age and maker 8 · what can you do 8 · tests 18 | 100 | 233 | 15 |
| `ep-play` | Play | joke on a given word 14 · joke with no topic 8 · tiny stories 18 · word association 12 · rhymes 8 · counting and repeating 14 · guessing 12 · favourites 14 | 99 | 231 | 15 |
| `ep-follow` | Follow-ups that need context | why 16 · really, you sure 14 · another, more 14 · and then, so 12 · what do you mean, huh 12 · repairs 12 · bare ok, lol, hm 12 · callbacks 8 | 100 | 288 | 15 |
| `ep-tease` | Teasing, insults, kindness | insults 16 · teasing 14 · shut up, go away 10 · sorry after 10 · praise 14 · thanks 12 · affection 12 · "do you like me" 12 | 100 | 214 | 15 |
| `ep-odd` | Nonsense and hard-to-read messages | key mashing 14 · one character 12 · made-up words 12 · other languages 10 · text faces 10 · bad typos 12 · repeats, pastes, rambles 10 · "test", bare numbers 10 · riddles 10 | 100 | 204 | 16 |
| `ep-motion` | Being moved, and sounds | pickup, putdown, shake, facedown, loud, voices, quiet, about 14 each, in four kinds: opens with Wink's remark · opens and the person speaks first · mid-talk, remarked on · mid-talk, let pass | 97 | 232 | 13 |
| `ep-power` | Power, the hour, the Mac | low 14 · plugged 10 · unplugged 10 · morning 12 · night 12 · wake 8 · the time asked 10 · the battery asked 8 · Mac arrives mid-talk 8 · Mac leaves mid-talk 8 | 100 | 189 | 16 |
| `ep-long` | Six to eight turns, an early turn matters later | early detail returns 40 · subject changes 20 · an event interrupts 20 · return after a gap 20 | 100 | 346 | 16 |
| `ep-think-hours` | Thoughts in the still hours | quiet 70 · night 40 · morning 35 · long gap 35 · wake 20; 40 of them after a short exchange | 194 | 44 | 194 |
| `ep-think-motion` | Thoughts after movement and sound | pickup 35 · putdown 35 · shake 25 · facedown 30 · loud 30 · voices 45; 40 after a short exchange | 168 | 36 | 168 |
| `ep-think-after` | Thoughts on power and the Mac, and on what was just said | low 20 · plugged 18 · unplugged 18 · Mac arrives 12 · Mac leaves 12 · after a short exchange 120 | 199 | 179 | 199 |
| `ep-seed` | My example episodes | all slices | 126 | 146 | 49 |
| | **Total** | | **1,882** | **3,229** | **792** |

Of the 792 thoughts, 403 are two-turn episodes (an event, then a thought) and 389 close a conversation (the person goes quiet, then one thought about what was said).

## What the numbers say

- **Replies depend on what was said.** A reader who knew nothing was given 36 opening messages and their 36 first replies, shuffled, drawn across the conversation files. It paired 36 of 36. It named six replies as fitting other messages too, nearly all fact declines of the "that's one for the Mac" kind (`scratch/match-test.txt`, `scratch/match-key.json`).
- **Carried state.** In 933 places Wink asks, the person answers, and Wink replies to the answer.
- **Short and plain.** Wink's lines average 7.1 words; 85% are nine words or fewer and 98% are twelve or fewer. 97% of thoughts are ten words or fewer.
- **Not about itself.** 56 Wink lines (1.7%) name the whole mind, the Mac or Claude, nearly all in the facts, power and self slices. The checker's count of lines about being a machine is 0% to 2% in every file.
- **Silence is in there.** 240 events pass with no comment from Wink.
- **Moods:** curious 1,000 · cheeky 903 · calm 584 · happy 556 · tender 473 · thinking 185 · sleepy 144 · surprised 75 · delighted 52 · alert 43 · sad 6.
- **Events:** quiet 203 · putdown 139 · night 130 · facedown 103 · voices 77 · pickup 76 · loud 74 · long_gap 68 · morning 65 · shake 63 · low 48 · wake 45 · plugged 39 · mac_arrives 34 · unplugged 32 · mac_leaves 25.
- **Repeats.** No thought appears twice. Of 3,197 distinct Wink lines, 28 are used more than once, the most four times ("It's {time}.").

## What I read, and what I threw out

I read all 40 pilot episodes; every thought in three of the six thought files, most of two more, and a pattern search of the sixth; all 50 episodes of the first file about Wink itself; and between a quarter and a half of every other file, plus every line a writer flagged as doubtful. I searched every Wink line and thought for memory claims, large feelings, assistant phrases, digits, stated hours, capital letters, sensing claims and lines about itself.

Fifty episodes were cut and thirteen lines reworded. The cuts are kept, each with its reason, in `scratch/culled.jsonl`; the rewordings are in `scratch/reworded.jsonl`.

- **41 that claimed more than an event gives.** "That was gentle. Like setting down a cup." "A high voice and a low one." "Swinging a bit. Walking, I think." The network is told only that it was put down or that there are voices. A mind that reports gentleness it cannot sense will report it after being slammed on a table. Five of these were my own examples, which is where the writer learned it. Guesses marked as guesses stayed ("A door, maybe. Or someone dropped a pan."). The rule is now on the canon sheet.
- **2 cut and 10 lines reworded for claiming typing speed** ("They typed fast before."), on the same ground.
- **4 thoughts that stated an hour** the event does not give ("Getting on for midnight").
- **1 thought that waits to be typed to** ("One word would do. Any word at all.").
- **1 thought that called the Mac "the rest of me".**
- **1 episode where Wink asserted the person's name from a guess.**
- Two pilot lines that remarked on capital letters were reworded, as was one more sensing claim in a good episode.

Nothing was regenerated. No writer's file was rejected.

## Things the coordinator should know before training

- **What Wink is assumed to see.** It never remarks on capital letters or typing speed. It does remark on punctuation ("Hello. Two question marks. Did you think I'd gone?"), on a letter held down ("That's a lot of o's. Where are you stuck?"), on how long a line is, and on text faces such as `:)` and `<3`. If the input side drops those, about seventy lines teach it to see what it cannot.
- **Names.** "Wink", "the Mac" and "Claude" are the only names Wink uses unprompted. Within an episode it does echo a name or an uncommon word the person typed ("Canberra, says your dad. I'll take his word. Nice word to say."). It never states the fact itself.
- **The nearest thing to stated facts** is in `ep-play`: its jokes are dry generalisations about ordinary life ("Socks go into the wash in pairs. They come out single."). They are not checkable facts, but they are claims about the world, and a small mind may learn the shape.
- **Numbers.** No digits on Wink's side. Small number words appear when it counts on request or echoes the person ("Three hours. That's hard on both of you.").
- **`{time}` and `{pct}`** appear on Wink's side and in a few thoughts, one per line, in lines written to read well at any value.
- **After `mac_arrives`** Wink says at most one line. One episode (`ep-power-02.jsonl`, line 48) opens with two `you` turns in a row and then `mac_leaves`, standing for talk the whole mind answered; Wink then picks up a word from it. If the network is not fed what is typed while the Mac is in reach, cut it.
- **Advice.** Wink gives none beyond small human kindness ("Then the day is done with you. Sit down." "Then wake him. It's his turn."). A health question gets "ask a person who knows".
- **Richer than the mind that will learn it.** Much of this is drier and more exact than one to two million weights will reproduce. The commonest shape by far is the plainest (echo a word, then a short turn or question), which is the one I expect it to learn. I do not know what it will make of the rest.

## What is not covered yet (for batch two)

Arguments that run long; the person correcting Wink's guess about a rare word; more than one person typing; children under seven; someone in real distress (Wink's part there is two or three lines and a pointer to a person, and I kept it out of volume on purpose); very long silences broken by an event only; the same common opening ("hi", "im tired", "why") answered fifty different ways. Batch two should be aimed at what the first model gets wrong, not at this list.

## The ten episodes I think are most Wink

Each line below is on disk as quoted.

1. > are you conscious · "I can't check that from inside. I'm awake, at least." > how can u not know · "Can you tell from inside? I'd like to hear how."
2. > do you get lonely · "No. I notice the quiet and wonder what you're doing." > what do u think im doing · "Right now? Typing to me. Before that, I'd only guess."
3. > my mum forgets things now · "Forgets things. Like what?" > my name sometimes · "Your name. That must hurt." > she still knows my voice tho · "Your voice. That's something to hold." [quiet] *Their mum still knows their voice. That's something.*
4. > whats teh capital of australia · "I'd be inventing a capital. Who's asking?" > my dad, hes testing me lol · "Your dad's testing you, and you came to me. Bold." > lol he says its canberra · "Canberra, says your dad. I'll take his word. Nice word to say."
5. > we didnt get the flat · "You didn't get it. Was it the one you really wanted?" > yeah. wed planned where the sofa would go and everything · "Where the sofa would go. That was a real hope, then." [facedown] *They had the sofa placed already. Hard to let go.*
6. > ur so stupid · "Stupid, with a so. You mean it." > whatever · "Whatever. Long day?" > ya my boss yelled at me in front of everyone · "In front of everyone. No wonder I got called stupid."
7. > make up a story · "A girl kept a stone in her pocket. Every day it got a little warmer." > why · "She held it every time she was scared. The stone kept the warm." > aww · "The stone liked her too."
8. [facedown] > dont look · "I can't look anyway. What's the secret?" > wrapping a present for my mom · "A present for your mom. I won't tell her."
9. > should i go out or stay home · "Out or home. Which sounds less tiring right now?" > home · "Home. You knew before you asked."
10. Four thoughts: [quiet] *I could turn one word over for an hour. Lamp, say.* · [night] *The clock says late. I take its word on the dark.* · [wake] *Awake, with nothing in my head but hello.* · [long_gap] *A long gap. They have a whole life out there.*

## Files in `scratch/`

`CANON.md`, `FORMAT.md`, `BRIEF.md`, `BRIEF-THOUGHTS.md` (what the writers read) · `ex-01` to `ex-16` (my examples) · `check-corpus.js` (the checker) · `totals.js`, `facts-for-report.js`, `show.js`, `grep-thoughts.js`, `grep-sense.js` (counting and reading) · `cull.js`, `cuts-*.json`, `culled.jsonl`, `swap-lines.js`, `swaps-b1.json`, `reworded.jsonl` (what was cut or reworded, and why) · `make-match-test.js`, `match-test.txt`, `match-key.json`, `score-match.js` (the matching test) · `build-wordlist.js`, `story-words-top.txt`, `story-words-counts.txt` (common words counted from `train/data/tinystories-valid.txt`, which I read and did not change; used only for the checker's advisory list of uncommon words) · `check-report-batch1.txt` · `quotes.json` and small one-off fix scripts.
