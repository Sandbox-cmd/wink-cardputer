// Checks that the voice pack has a recording for everything the small mind can say:
// every line, with every clock time, every battery percent, and a spread of numbers.
// It covers text the same way the device does (firmware/src/voice.cpp, planClips).
//   node tools/check-voice.mjs [Name]
import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { norm } from '../bridge/voice.mjs'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const name = process.argv[2] || 'Wink'
const lines = JSON.parse(readFileSync(join(root, 'character/lines.json'), 'utf8'))
const pack = readFileSync(join(root, 'bridge/voice.pak'))
const count = pack.readUInt32LE(4)
const have = new Set()
for (let i = 0; i < count; i++) have.add(pack.readUInt32LE(8 + i * 12))

// the device's own number and clock words
const ones = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven', 'twelve',
  'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen', 'eighteen', 'nineteen']
const tens = ['', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy', 'eighty', 'ninety']
function words(n) {
  if (n < 0) return 'minus ' + words(-n)
  if (n < 20) return ones[n]
  if (n < 100) return tens[Math.floor(n / 10)] + (n % 10 ? ' ' + ones[n % 10] : '')
  if (n < 1000) return ones[Math.floor(n / 100)] + ' hundred' + (n % 100 ? ' and ' + words(n % 100) : '')
  if (n < 1000000) return words(Math.floor(n / 1000)) + ' thousand' + (n % 1000 ? ' ' + words(n % 1000) : '')
  return words(Math.floor(n / 1000000)) + ' million' + (n % 1000000 ? ' ' + words(n % 1000000) : '')
}
const hour = (h) => words(h % 12 === 0 ? 12 : h % 12)
function clock(h, m) {
  if (m === 0) return hour(h) + " o'clock"
  if (m === 15) return 'quarter past ' + hour(h)
  if (m === 30) return 'half past ' + hour(h)
  if (m === 45) return 'quarter to ' + hour(h + 1)
  return m < 30 ? words(m) + ' past ' + hour(h) : words(60 - m) + ' to ' + hour(h + 1)
}

function covered(text) {
  const w = norm(text).split(' ').filter(Boolean)
  const n = w.length
  const next = new Array(n + 1).fill(-1)
  next[n] = n
  for (let i = n - 1; i >= 0; i--) {
    let h = 0x811c9dc5
    for (let j = i; j < n; j++) {
      if (j > i) h = Math.imul(h ^ 32, 0x01000193) >>> 0
      for (const c of Buffer.from(w[j], 'latin1')) h = Math.imul(h ^ c, 0x01000193) >>> 0
      if (next[j + 1] >= 0 && have.has(h >>> 0)) next[i] = j + 1
    }
  }
  if (next[0] < 0) return 0
  let pieces = 0
  for (let i = 0; i < n; i = next[i]) pieces++
  return pieces
}

const times = []
for (let h = 0; h < 24; h++) for (let m = 0; m < 60; m++) times.push(clock(h, m))
const numbers = [...Array(201).keys(), 250, 999, 1000, 1024, 4096, 12345, 99999, 250000, -7, -120].map(words)
  .concat(['seven point five', 'three point one four', 'minus two point five'])
let said = 0, missed = 0, most = 0
const missing = []
for (const [key, set] of Object.entries(lines)) {
  for (const { say } of set) {
    const base = say.replaceAll('{name}', name)
    const variants = base.includes('{time}') ? times.map((t) => base.replace('{time}', t))
      : base.includes('{pct}') ? [...Array(101).keys()].map((p) => base.replace('{pct}', words(p)))
      : base.includes('{n}') ? numbers.map((v) => base.replace('{n}', v))
      : [base]
    for (const v of variants) {
      const pieces = covered(v)
      said++
      most = Math.max(most, pieces)
      if (!pieces) { missed++; if (missing.length < 8) missing.push(`${key}: ${v}`) }
    }
  }
}
console.log(`${count} recordings in the pack; ${said} things it could say checked; ${missed} without a full recording; longest is ${most} pieces`)
missing.forEach((m) => console.log('  no recording covers: ' + m))
process.exit(missed ? 1 : 0)
