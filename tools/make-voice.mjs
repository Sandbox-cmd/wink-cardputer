// Records every line of the small mind in Wink's voice and packs the recordings into
// one file for the device's memory card.
//   node tools/make-voice.mjs [Name]        (the name the lines say; default Wink)
// then send it over the cable:
//   tools/dev put:bridge/voice.pak:/sd/wink/voice.pak
//
// A line with a placeholder ({time}, {pct}, {n}) is recorded as the words before it and
// the words after it; the value in between is played from recordings of numbers and
// clock phrases. The device finds a recording by a hash of its words.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { hash, norm, settings, synth, RATE } from '../bridge/voice.mjs'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const name = process.argv[2] || 'Wink'
const lines = JSON.parse(readFileSync(join(root, 'character/lines.json'), 'utf8'))

// the same words the device makes of a number (firmware/src/voice.cpp)
const ones = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven', 'twelve',
  'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen', 'eighteen', 'nineteen']
const tens = ['', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy', 'eighty', 'ninety']
const words = (n) => (n < 20 ? ones[n] : n < 100 ? tens[Math.floor(n / 10)] + (n % 10 ? ' ' + ones[n % 10] : '') : 'one hundred')

const wanted = new Map()   // normalized words -> the text to speak
const want = (text) => {
  const key = norm(text)
  if (key && !wanted.has(key)) wanted.set(key, text.trim())
}

let whole = 0, split = 0
for (const set of Object.values(lines)) {
  for (const { say } of set) {
    const text = say.replaceAll('{name}', name)
    const parts = text.split(/\{(?:time|pct|n|hours)\}/)
    if (parts.length === 1) { want(text); whole++ } else { parts.forEach(want); split++ }
  }
}
for (let n = 0; n <= 100; n++) want(words(n))
for (let n = 1; n <= 9; n++) { want(ones[n] + ' hundred'); want(ones[n] + ' hundred and') }
for (const w of ['thousand', 'and', 'point', 'minus', 'million']) want(w)
for (let n = 1; n <= 29; n++) { want(words(n) + ' past'); want(words(n) + ' to') }
for (const w of ['quarter past', 'half past', 'quarter to']) want(w)
for (let n = 1; n <= 12; n++) want(ones[n] + " o'clock")

// two different wordings must never share a hash
const byHash = new Map()
for (const key of wanted.keys()) {
  const h = hash(key)
  if (byHash.has(h)) throw new Error(`hash clash between "${key}" and "${byHash.get(h)}"`)
  byHash.set(h, key)
}

// recordings are kept between runs, so only changed lines are spoken again
const cache = join(root, 'bridge/voice-cache', settings.voice + (settings.rate ? '-' + settings.rate : '') + (settings.childlike ? '-child' + settings.childlike : ''))
mkdirSync(cache, { recursive: true })
const todo = [...wanted.entries()]
const clips = new Map()
let made = 0
async function worker() {
  for (;;) {
    const item = todo.pop()
    if (!item) return
    const [key, text] = item
    const file = join(cache, hash(key).toString(16).padStart(8, '0') + '.ul')
    let audio
    if (existsSync(file) && existsSync(file + '.txt') && readFileSync(file + '.txt', 'utf8') === text) audio = readFileSync(file)
    else {
      // macOS speech sometimes stalls when asked for several things at once: try again
      for (let attempt = 1; !audio; attempt++) {
        try { audio = await synth(text, { trim: true }) } catch (e) { if (attempt >= 4) throw e }
      }
      writeFileSync(file, audio)
      writeFileSync(file + '.txt', text)
      made++
    }
    clips.set(hash(key), audio)
  }
}
await Promise.all(Array.from({ length: 2 }, worker))

// the pack: "WVP1", how many, then a sorted table of (hash, where, how long), then the sound
const table = [...clips.keys()].sort((a, b) => a - b)
const head = Buffer.alloc(8 + table.length * 12)
head.write('WVP1', 0)
head.writeUInt32LE(table.length, 4)
let offset = head.length, seconds = 0
table.forEach((h, i) => {
  const len = clips.get(h).length
  head.writeUInt32LE(h, 8 + i * 12)
  head.writeUInt32LE(offset, 12 + i * 12)
  head.writeUInt32LE(len, 16 + i * 12)
  offset += len
  seconds += len / RATE
})
const pack = Buffer.concat([head, ...table.map((h) => clips.get(h))])
writeFileSync(join(root, 'bridge/voice.pak'), pack)
console.log(`${whole} whole lines, ${split} lines in pieces, ${table.length} recordings (${made} newly spoken) in the voice of ${settings.voice}`)
console.log(`${(seconds / 60).toFixed(1)} minutes of speech, ${(pack.length / 1048576).toFixed(1)} MB -> bridge/voice.pak`)
