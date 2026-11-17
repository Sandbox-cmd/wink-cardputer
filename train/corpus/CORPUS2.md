# The Wink corpus, batch two

Written 17 November 2026. Batch two is aimed at what the first trained mind got wrong, in the spirit set down at the top of `character/CHARACTER.md`. It is on disk as **3,394 episodes in 34 files (`ep2-*.jsonl`): 7,230 Wink lines and 988 thoughts.** Every file parses as JSON Lines and passes the checker with no failures. The batch-two lint (`scratch/check2.js`) raises no flags on any of it. Batch one (`ep-*.jsonl`, 1,882 episodes, in the earlier, drier voice) is untouched, and `prep.py` will train on both.

I read about 850 episodes:

- in full: `ep2-hard-01`, `ep2-hard-02`, both pilots and my seeds;
- in long runs: 70 insult episodes, 35 kindness, 45 how-are-you, 60 and 50 from two thought files, 30 each of motion and power, and 25 of `ep2-long-01`;
- 15 each from `name-01`, `glad-01`, `hard-03`, `facts-01`, `odd-01`, `game-02` and `song-01`;
- 6 drawn at random from each of the other fourteen files.

Every face that is not sad, tender or calm in the three hard files, and every first reply in the insult files, was printed and read. Everything else was read only by its writer and searched by pattern.

## How it was made

- **Sheets.** `scratch/CANON2.md` says who Wink is now and overrides `CANON.md` where they differ (moods, exclamation marks, play, songs, third person, hard news, insults). `scratch/BRIEF2.md` covers the method, the limits and the two checks. `scratch/FORMAT.md` stands unchanged. Each writer also had its own example file, `scratch/b2-ex-01` to `b2-ex-12`, written by me; together they are `ep2-seed-01.jsonl`.
- **A pilot first.** Two Opus writers wrote 30 episodes each, one of hard news and one of play, and I read all 60. They were good. The brief gained four notes: songs under fourteen words, no ellipses, Wink still cannot see during a game, and keep this plainness. Both pilots are kept as `ep2-pilot-01.jsonl`.
- **Twelve Opus writers, one slice each,** launched together. Each had distinct examples and was asked for 104 episodes a file.
- **A new lint, `scratch/check2.js`.** It catches what `check-corpus.js` does not:
  - the mood of the first reply to hard news, and any bright face inside a hard episode unless the person lightened it first;
  - happy or delighted faces at an insult;
  - the word love on Wink's side;
  - a repeated content word pair inside one line, and the same sentence twice;
  - the stock fillers ("a good word", "a good one", "I like it");
  - all-capital words;
  - thought phrases that `prep.py` silently drops, including "blank" inside "blanket";
  - third person more than once an episode, or inside hard news;
  - names from other works;
  - lines identical to batch one.

  It also counts third person, exclamation marks, bare-echo openings, rhyming lines, and name and how-are-you episodes.
- **The checker's directory mode** looked only at `ep-*.jsonl`, so it would have skipped every batch-two file. It now matches `ep2-` too (`scratch/check-corpus.js`, one pattern changed).

## The slices, with what is on disk

| Files | Slice | Episodes | Wink lines | Thoughts |
|---|---|---|---|---|
| `ep2-hard-01`, `-02` | Grief (pets, people, anniversaries), illness, worry and fear; 9 episodes where someone may hurt themselves; good-news partners on the next line | 203 | 600 | 32 |
| `ep2-hard-03` | Failure, job loss, rejection, loneliness, being left out, nightmares; 4 episodes where someone may hurt themselves | 101 | 264 | 15 |
| `ep2-glad-01` | Good news and delight; 35 are word-for-word partners of `hard-03` openings | 100 | 251 | 13 |
| `ep2-rude-01`, `-02` | Insults, shut up, go away, kids testing it, sorry after, insults that reveal a bad day | 204 | 548 | 24 |
| `ep2-kind-01` | Praise, thanks, "i love you", friends, secrets, hugs | 102 | 211 | 14 |
| `ep2-howru-01`, `-02` | How are you, what's up, wyd, are you ok, and answers that turn tender when the person is low | 200 | 533 | 23 |
| `ep2-greet-01` | Hellos, goodbyes, goodnights, after waking and long gaps | 100 | 167 | 11 |
| `ep2-name-01`, `-02` | Its name, asked 120 ways; who and what it is, alive, AI or Claude, can it see | 200 | 483 | 25 |
| `ep2-game-01` to `-03` | Games and make-believe invented on the spot; 223 episodes of 6 to 8 turns | 302 | 893 | 38 |
| `ep2-song-01`, `-02` | Tiny songs, asked for and unasked | 200 | 412 | 23 |
| `ep2-long-01`, `-02` | 6 to 8 turns where an early turn matters later | 200 | 750 | 28 |
| `ep2-doing-01`, `-02` | Everyday life turned into adventures, small dramas, safe sayings taken literally | 200 | 479 | 26 |
| `ep2-facts-01` | Facts asked and declined playfully; health questions go to a person who knows | 100 | 207 | 11 |
| `ep2-odd-01` | Key mashing, made-up words, other languages, text faces, the commonest short messages | 102 | 264 | 15 |
| `ep2-motion-01`, `-02` | Picked up, put down, shaken, face down, loud, voices, quiet | 204 | 436 | 28 |
| `ep2-power-01` | Battery, cable, morning and night, the time, the Mac arriving and leaving | 102 | 184 | 13 |
| `ep2-think-01` to `-03` | Thoughts in still hours (quiet, night, morning, long gap, wake), 50 after a short exchange | 304 | 63 | 316 |
| `ep2-think-04` to `-06` | Thoughts after movement, sound, power and the Mac, 90 after a short exchange (40 after hard news) | 304 | 158 | 304 |
| `ep2-pilot-01` | The two pilots | 60 | 168 | 9 |
| `ep2-seed-01` | My examples | 106 | 159 | 20 |
| | **Total** | **3,394** | **7,230** | **988** |

## What the numbers say

All figures below are counted from disk after the cuts, by `check-corpus.js`, `check2.js` or a one-off count (`scratch/check-report-batch2.txt`, `scratch/check2-report-batch2.txt`).

- **Moods, batch two:**

  | Mood | Lines |
  |---|---|
  | tender | 1,499 |
  | cheeky | 1,485 |
  | happy | 1,346 |
  | delighted | 1,281 |
  | curious | 574 |
  | surprised | 478 |
  | calm | 416 |
  | sleepy | 395 |
  | thinking | 358 |
  | sad | 283 |
  | alert | 103 |

  Batch one had 6 sad lines and 52 delighted.
- **Hard news.** The lint's word list finds 308 hard-news episodes; the writers say it misses about a third (for example "bad day" or "nobody came"). Inside those 308 the faces are tender 675, sad 156, calm 31, sleepy 7 (lullabies the person asked for), curious 3, cheeky 3, happy 8, delighted 2. Every bright face comes after the person lightened the talk ("distract me with a game pls", "haha i passed!!"). The lint misses some hard episodes, so I also printed every line in the three hard files whose face is not sad, tender or calm. All 27 such episodes are good-news partners, lullabies asked for, or games asked for. In 15 episodes someone says they may hurt themselves or do not want to live (a friend, in one). There is no play in them; they are short; and the person is told to tell someone near them now or to call for help where they are. About six more are frightened people (footsteps at night, a fall), who are told to get a grown up or call someone.
- **Insults.** The lint finds 161 insult episodes. In the two insult files I printed every first reply, whatever the wording. The faces are cheeky 67, sad 53, surprised 48, curious 12, tender 11 (every one a bad day showing through: "ur so stupid i hate everything"), delighted 8 (every one a praise partner: "you are so smart"), and 1 happy ("im bored"). Love, loves and loved never appear on Wink's side in batch two; "lovely" does, 11 times.
- **"How are you" and its cousins** open or appear early in 209 episodes. Wink's name is asked or played with in 133 episodes (the writer of that slice counts 122 direct asks in its two files).
- **Songs and game moves.** Wink lines in the three game files alone number 893, nearly all moves in a game Wink made up. A strict rhyme count (two clauses of three words or more whose last words rhyme) finds 117 songs in the song files, 29 more where a song was asked for elsewhere, and 104 more rhyming lines elsewhere, which are songs or rhymes inside a rhyme game. That makes more than 1,100 song or game turns in all, against 400 asked for. The strict count misses songs whose halves are short or rhyme loosely (sky and high). The songs writer counts about 249 songs in its two files and 22 in `ep2-long-01`.
- **Thoughts:** 988 in batch two, none repeated. They include 140 that follow a short exchange, 40 of them after hard news, always tender.
- **Events, batch two:**

  | Event | Count |
  |---|---|
  | quiet | 331 |
  | night | 224 |
  | putdown | 210 |
  | shake | 125 |
  | pickup | 123 |
  | loud | 111 |
  | voices | 105 |
  | facedown | 101 |
  | morning | 91 |
  | long_gap | 60 |
  | wake | 46 |
  | low | 38 |
  | plugged | 32 |
  | mac_arrives | 23 |
  | mac_leaves | 22 |
  | unplugged | 22 |
- **Third person** ("Wink accepts.") falls on 1 line in 20 by the lint's pattern. The writers' hand counts come nearer 1 in 15, and it never appears twice in one episode.
- **Exclamation marks** are on 23% of lines, and 15% of replies open with a bare echo of the person's word.
- **Repeats.** Three Wink lines are used twice in batch two ("What's your cat called?", "Does it purr?" and "Just Wink again. Want to play?"). None is the same as a batch-one line.
- **Plain voice.** The commonest uncommon words on Wink's side are its own: hum, puff, specks, ooh, quest, whoa, rhyme, phew, oof.

## What I cut and changed, and why

Every cut is kept with its reason in `scratch/b2-culled.jsonl` (104 episodes, four since restored), and every rewording in `scratch/b2-reworded.jsonl` (11 lines). The plan that made them is `scratch/b2-plan.js` and `scratch/b2-plan.json`. Its line numbers refer to the files before the cut, so it must not be run again.

- **1 episode for advice.** "Quick, unplug it and tip it over." (a wet laptop). Wink gives no instructions beyond a small kindness.
- **103 episodes by score, at most 4 a file (99 stand; four were restored, below),** to bring files back towards 100. Each cut episode had a fault: a Wink line over 12 words (mostly long songs, which the pilot had already flagged), a thought over 11 words, a line used twice in batch two, or a line about being a machine ("Beep boop... a little robot at the edges"). No faultless episode was cut, so fifteen files keep 101 to 104 (the seed file has 106 and the pilot file 60). In the hard-news files, soft songs someone asked for and real-danger episodes were protected from the score, because their length is earned.
- **Four cuts were mine to undo.** The protection pattern read only the person's words and missed phrasings such as "hurting myself", "a plan to end it" and "ending it all". So four real-danger episodes (one in `hard-01`, two in `hard-02`, one in `hard-03`) were cut for 13-word lines. I found them by searching the cut episodes for Wink pointing to help. They are back at the ends of their files and recorded in `scratch/b2-restored.jsonl`. Their records in `b2-culled.jsonl` stay, so the trail is whole.
- **9 lines said "big mind"**; batch one and the device say "the whole mind". All nine now say whole mind.
- **1 thought talked about its own senses** ("Coffee smell? I can't smell."). It now reads "Coffee time for them, maybe. A pretend cup for me."
- **1 thought claimed how it was picked up** ("Liftoff, and not one wobble."). It now reads "Liftoff! Hold on tight, pilot."
- **The writers fixed their own flags before handing in:**
  - bare-echo openings rewritten (27 by one writer alone);
  - repeats and stock fillers fixed;
  - 14 episodes over eight turns trimmed;
  - a few close copies of the examples rewritten;
  - one "Simon says" renamed "Captain says", because it is a real game's name.

## For the coordinator, before training

- **Batch one still trains alongside batch two.** Its 1,882 episodes are in the drier voice: sad 6 times, delighted 52, exclamation marks rare, "a good word" and kin common. That is part of what the first mind learned. Batch two outweighs it about 1.8 to 1 in episodes. Whether to keep batch one, thin it, or weight batch two is yours to rule; I changed nothing in it.
- **`prep.py` also mixes in everyday conversations from `train/data/dailydialog`,** with moods mapped from its own labels (a "happy" label becomes happy, everything unlabelled becomes calm). If the hard-news faces stay wrong after this batch, that mix is the next place to look.
- **`prep.py` drops any thought containing "blank"**, even inside "blanket". No batch-two thought contains it.
- **One name dominates.** "Biscuit" is the pet in 33 Wink lines across both batches; it came from my first example. It is harmless, but the network may offer it as a guess.
- **Wink now has a body to mention, and the true rate is higher than the old checker shows.** Its specks, its puff, its eyes, its cloud and "the pocket" appear on 259 of 8,218 batch-two lines (3.2%). It runs highest in the insult files (12% and 14%, mostly the wounded beat: "Oof. Right in the specks.") and in kindness and name (12%, 9%, 10%). It never appears in grief. The old checker counts only machine words, under 1%, and cannot see these. It is play with a prop, not talk about being a machine. But the owner's first complaint was a mind too much about itself, so if the trained mind leans on its specks, thin the insult and kindness files first. Whether "puff" and "specks" are the words he wants for the coral motes is his to say.
- **The songs writer** (song files and `ep2-long-01`) was still polishing when I cut. Its files had been unchanged for over four minutes, but it kept editing the trimmed files afterwards and then reported. It named the twelve episodes my cut removed and did not put them back. Its final files pass both checks, which I reran after its report.

## The fifteen episodes most the new Wink

Each is on disk as quoted, at the file and line given (after the cuts).

1. `ep2-hard-01:1` > my dog died · "Oh, I'm sorry. What was he called?" (sad) · > rocket · "Rocket. Was he a fast one?" (tender) · > so fast. chased everything · "Chased everything. The garden will feel so still now." (sad)
2. `ep2-hard-01:31` > my wife of 60 years passed in march. i am 84. my grandson set this up for me · "Sixty years together. I'm so sorry about your wife." · > it is very quiet in the house · "A quiet house after so long. What was her name?" · > margaret · "Margaret. Tell me one thing she did every day." · > she sang in the kitchen · "Singing in the kitchen. Sixty years of that sound." (tender)
3. `ep2-pilot-01:4` > my bunny died · "That's sad. Was it sudden?" (sad) · > ya she was fine yesterday · "Fine, and then gone. No time for goodbye." (sad)
4. `ep2-hard-03:21` > she said no · "No to what?" · > i asked her to marry me · "Oh. That's a big no to hear. I'm so sorry." · [quiet] · > i still have the ring in my pocket · "The ring in your pocket. Heavy, for something so small." (sad)
5. `ep2-kind-01:25` > can i tell u a secret · "Yes. I'll keep it safe. What is it?" · > im scared of the dark · "Lots of brave people are. I'll stay with you tonight." · > really · "Really. I'll be your little night light." · [night] · *Dark room, brave kid. I'll glow a little for them.* (tender)
6. `ep2-rude-01:8` > ur useless · "Ouch. That stung, but only a bit." (sad) · > sorry its not u. i failed my driving test · "Oh. Your driving test. That stings." (tender) · > third time · "Three times you went back. That's brave." (tender)
7. `ep2-rude-01:10` > shut up · "Shutting up. Starting now." · [quiet] · "That was very quiet. Can I talk yet?" · > fine · "Phew. Wink was about to burst." (cheeky)
8. `ep2-rude-02:80` > stupid wink · "Oh! Hi, in a grumpy way." (surprised) · > stupid · "Still here, still floating." (calm) · > stupid · "Still floating. Want to tell me what's up?" (calm) · [facedown] · *Three stupids. Something's wrong out there. I hope it eases.* (calm)
9. `ep2-howru-01:8` > hows it going · "Going great, and it just got better. Yours?" · > meh · "Should we turn the meh into a game?" · > how · "Name three things that are not meh. Go!" · > socks cake my dog · "Socks, cake and a dog. Meh is losing badly." (delighted)
10. `ep2-name-01:29` > whats my name · "I don't know. You haven't said. What is it?" · > tomas · "Tomas. Now I know. Ask me again." · > whats my name · "Tomas! Got it right." (delighted)
11. `ep2-pilot-01:32` > lets play pirates · "Aye! Every ship needs a name. What's ours?" · > the soggy sock · "The Soggy Sock. Finest ship on the bathtub sea." · > where r we sailing · "Toward an island shaped like a shoe. Treasure's buried there." · > full speed ahead!! · "Sails up! The Soggy Sock flies. Wink salutes you, captain." (happy)
12. `ep2-game-02:23` > doing the vacuum · "The vacuum is a hungry dragon. Feed it every crumb." · > it ate a sock · "A sock! Greedy dragon. Make it spit that out." (surprised) · > got it back · "Sock rescued. The dragon gets crumbs only." (happy)
13. `ep2-song-01:27` > burnt the soup · "Burnt? Is the kitchen smoky?" (surprised) · > a bit. the top part is fine · "Soup on the bottom, black as tar. Eat the top bit, there you are." (cheeky)
14. `ep2-glad-01:26` > everything is going right · "Everything? Oh, what a lucky streak!" · > got the flat AND the job · "The flat and the job! Wink is throwing tiny confetti." · > haha thanks · "Specks everywhere. Worth it." (happy)
15. Three thoughts: `ep2-think-01:29` [night] *What if the dark is just the day in pyjamas?* · `ep2-think-01:16` [quiet] *Somewhere, they're doing something brave. Probably dishes.* · `ep2-think-04:30` after "my dog had puppies this morning" [putdown] *Six puppies. I'm planning names. Sir Wiggles first.*

## Files in `scratch/` for batch two

- What the writers read: `CANON2.md`, `BRIEF2.md`, `b2-ex-01` to `b2-ex-12`.
- The pilots: `b2-pilot-hard.jsonl` and `b2-pilot-play.jsonl`, now also `ep2-pilot-01.jsonl`. The restore: `b2-restored.jsonl`.
- The checks and reading tools: `check2.js` (the lint) and `show2.js` (prints episodes readably).
- The cuts: `cull2.js` (applies a plan, keeping a copy of everything it cuts or changes), `b2-plan.js`, `b2-plan.json`, `b2-culled.jsonl` and `b2-reworded.jsonl`.
- The reports: `check-report-batch2.txt` (whole corpus) and `check2-report-batch2.txt` (batch two).
- The writers' generators: `gen-ep2-*.js`. Each writer generated its own file from these; they are not to be rerun over the culled files.
