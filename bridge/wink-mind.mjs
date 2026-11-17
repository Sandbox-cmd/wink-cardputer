#!/usr/bin/env node
// The whole mind's side of the line.
//
// Claude Code starts this program as a "channel": a helper that pushes what you type
// or say on the Cardputer into the session as a new turn, and gives the session tools
// to answer through the device and to work it: speak, show a card, set timers, write
// and read notes, open rooms, change settings, keep memories, and pack the small mind.
//
// It listens on the local network for the one device that knows the shared token in
// pair.json. Speech is turned into text here on the Mac by whisper-cli, and replies
// are spoken in Wink's voice by macOS speech (voice.mjs) and sent to the device as
// sound. Nothing leaves the machine from this program.
//
// What it keeps, in the folder the session runs in: memory.jsonl (what Wink remembers
// about you, each line dated and sourced, struck lines kept and marked) and
// learned.json (what the whole mind has packed for the small mind).
import { Server } from '@modelcontextprotocol/sdk/server/index.js'
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js'
import { CallToolRequestSchema, ListToolsRequestSchema } from '@modelcontextprotocol/sdk/types.js'
import { spawn, execFile } from 'node:child_process'
import { appendFileSync, existsSync, readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs'
import { createServer } from 'node:net'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { randomBytes, timingSafeEqual } from 'node:crypto'
import { fileURLToPath } from 'node:url'
import { deflateSync, crc32 } from 'node:zlib'
import { hash, synth } from './voice.mjs'

const here = dirname(fileURLToPath(import.meta.url))
const home = process.env.WINK_HOME || process.cwd()   // the session's folder: memory and packed knowledge live there
const pair = JSON.parse(readFileSync(process.env.WINK_PAIR || join(here, 'pair.json'), 'utf8'))
const PORT = Number(process.env.WINK_PORT) || pair.port || 8765
const MOODS = ['calm', 'happy', 'delighted', 'curious', 'thinking', 'sleepy', 'surprised', 'sad', 'cheeky', 'tender', 'alert']
const ROOMS = ['talk', 'terminal', 'notes', 'files', 'clock', 'echo', 'memory', 'body', 'settings']
const SETTINGS = ['volume', 'brightness', 'talks_unprompted', 'speaks_aloud', 'hears_the_room', 'answers_to_name']
const MODELS = [
  process.env.WINK_WHISPER_MODEL,
  join(here, 'models/ggml-base.en.bin'),
  join(here, 'models/ggml-small.en.bin'),
  join(process.env.HOME || '', 'Library/Application Support/node-terminal/speech-models/ggml-tiny.bin'),
].filter(Boolean)

// stdout belongs to the session; this program keeps its own diary in a file
const log = (...a) => {
  try {
    appendFileSync(join(here, 'wink-mind.log'), new Date().toISOString() + ' ' + a.join(' ') + '\n')
  } catch {}
}
const today = () => {
  const d = new Date()
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}

// the screen font and the voice files know plain letters only
const chars = (...codes) => new RegExp('[' + String.fromCharCode(...codes) + ']', 'g')
const plain = (s) =>
  String(s ?? '')
    .replace(chars(0x2018, 0x2019, 0x02bc), "'")
    .replace(chars(0x201c, 0x201d), '"')
    .replace(new RegExp('\\s*[' + String.fromCharCode(0x2013, 0x2014) + ']\\s*', 'g'), ', ')
    .replace(chars(0x2026), '...')
    .replace(/[*_`#>]/g, '')
    .replace(/[^\x20-\x7e\n]/g, '')
    .replace(/[ \t]+/g, ' ')
    .trim()

// ------------------------------------------------------------ the device ----
let device = null          // the socket of the paired Cardputer, when in reach
let lastStatus = null
let waiters = []           // things waiting on the device: a status, a screenshot, an act, a file
let voiceChunks = null     // sound arriving while the device is listening
let voiceByName = false    // ...because it heard something, not because the button is held
let nextId = 1
let lastVia = 'keys'        // how the person last spoke: a typed line is exact, a heard one is a transcript
let followUpUntil = 0      // after Wink asks a question, the answer need not start with its name

const send = (obj) => {
  if (!device) return false
  device.write(JSON.stringify(obj) + '\n')
  return true
}
const wait = (kind, ms, match = () => true) =>
  new Promise((resolve) => {
    const w = { kind, resolve, match }
    waiters.push(w)
    setTimeout(() => { waiters = waiters.filter((x) => x !== w); resolve(null) }, ms)
  })
const settle = (kind, value) => {
  waiters = waiters.filter((w) => (w.kind === kind && w.match(value) ? (w.resolve(value), false) : true))
}

// ask the device to do one thing and wait for it to say how that went
async function act(op, args = {}) {
  const id = nextId++
  if (!send({ t: 'do', id, op, ...args })) return null
  return (await wait('done', 5000, (m) => m.id === id)) || { ok: false, error: 'the device did not answer' }
}

// put a file on the device (recordings on its card, and the two lists kept in step)
async function sendFile(path, data) {
  if (!device) return false
  const buf = Buffer.isBuffer(data) ? data : Buffer.from(String(data), 'latin1')
  device.write(Buffer.concat([Buffer.from(JSON.stringify({ t: 'blob', path, n: buf.length }) + '\n'), buf]))
  const done = await wait('blobbed', 20000, (m) => m.path === path)
  return !!(done && done.ok)
}

// record a sentence in Wink's voice and leave it on the device, so the small mind can say it later
async function teach(text) {
  const t = plain(text)
  if (!t) return false
  try {
    const audio = await synth(t.slice(0, 600))
    return await sendFile(`/sd/wink/x/${hash(t).toString(16).padStart(8, '0')}.ul`, audio)
  } catch (e) {
    log('could not record a sentence:', e.message)
    return false
  }
}

function sameToken(given) {
  const a = Buffer.from(String(given || '')), b = Buffer.from(String(pair.token))
  return a.length === b.length && timingSafeEqual(a, b)
}

// what was going on around the device when the words were said, as plain attributes
function situationOf(m) {
  const out = {}
  const put = (k, v) => { if (v !== undefined && v !== null && v !== '') out[k] = String(v) }
  put('room', m.room); put('clock', m.clock); put('battery', m.battery)
  if (m.charging) put('charging', 'yes')
  put('in_hand', m.in_hand ? 'yes' : 'no')
  put('quiet_before_s', m.quiet_before_s)
  put('last_event', m.last_event); put('last_event_s', m.last_event_s)
  put('timer_left_s', m.timer_left_s)
  return out
}

function onDeviceLine(sock, line) {
  let m
  try { m = JSON.parse(line) } catch { return null }
  if (!sock.paired) {
    if (m.t === 'hello' && sameToken(m.token)) {
      sock.paired = true
      if (device && device !== sock) device.destroy()
      device = sock
      sock.write(JSON.stringify({ t: 'welcome', epoch: Math.floor(Date.now() / 1000), tz: -new Date().getTimezoneOffset() * 60 }) + '\n')
      log('device in reach:', m.name, 'fw', m.fw, 'from', sock.remoteAddress)
      for (const id of Array.isArray(m.struck) ? m.struck : []) strike(String(id), 'on the device, while the Mac was out of reach', false)
      arrived()
    } else {
      log('refused a caller without the token from', sock.remoteAddress)
      sock.destroy()
    }
    return null
  }
  switch (m.t) {
    case 'ping': send({ t: 'pong' }); break
    case 'status': lastStatus = { ...m, at: Date.now() }; settle('status', lastStatus); break
    case 'done': settle('done', m); break
    case 'blobbed': settle('blobbed', m); break
    case 'said':
      send({ t: 'ack' })
      push(plain(m.text), { via: m.via === 'voice' ? 'voice' : 'keys', ...situationOf(m) })
      break
    case 'asked': noteUnanswered(m.questions); break
    case 'struck': strike(m.id, 'on the device'); break
    case 'ptt':
      if (m.on) { voiceChunks = []; voiceByName = !!m.vox }
      else { const chunks = voiceChunks || []; voiceChunks = null; heard(Buffer.concat(chunks), voiceByName, situationOf(m)) }
      break
    case 'audio': return { want: m.n | 0, kind: 'audio' }
    case 'shot': return { want: m.n | 0, kind: 'shot', w: m.w, h: m.h }
  }
  return null
}

function onDeviceBytes(kind, meta, bytes) {
  if (kind === 'audio') { if (voiceChunks) voiceChunks.push(bytes) }
  else if (kind === 'shot') settle('shot', { ...meta, bytes })
}

const tcp = createServer((sock) => {
  sock.setNoDelay(true)
  let buf = Buffer.alloc(0), pending = null
  const dead = setTimeout(() => { if (!sock.paired) sock.destroy() }, 5000)
  sock.on('data', (d) => {
    buf = Buffer.concat([buf, d])
    for (;;) {
      if (pending) {
        if (buf.length < pending.want) break
        onDeviceBytes(pending.kind, pending, buf.subarray(0, pending.want))
        buf = buf.subarray(pending.want)
        pending = null
        continue
      }
      const nl = buf.indexOf(10)
      if (nl < 0) { if (buf.length > 65536) sock.destroy(); break }
      const line = buf.subarray(0, nl).toString('utf8')
      buf = buf.subarray(nl + 1)
      pending = onDeviceLine(sock, line)
      if (pending && !(pending.want > 0 && pending.want <= 200000)) pending = null
    }
  })
  const gone = () => { clearTimeout(dead); if (device === sock) { device = null; log('device out of reach') } }
  sock.on('close', gone)
  sock.on('error', gone)
})
tcp.on('error', (e) => log('could not listen:', e.message))
tcp.listen(PORT, '0.0.0.0', () => log('listening on', PORT))

// say on the local network where the whole mind is, so the device finds it by name
const advert = spawn('dns-sd', ['-R', 'Wink mind', '_winkmind._tcp', 'local', String(PORT)], { stdio: 'ignore' })
const bye = () => { try { advert.kill() } catch {} ; process.exit(0) }
process.on('SIGINT', bye)
process.on('SIGTERM', bye)
process.stdin.on('close', bye)

// ------------------------------------------------------------ memory --------
// What Wink keeps about you: only your own words, each with its date and how it came
// to be kept. A struck line stays in the file, marked, and is never used again.
const memoryFile = join(home, 'memory.jsonl')
const readMemory = () =>
  existsSync(memoryFile) ? readFileSync(memoryFile, 'utf8').split('\n').filter(Boolean).map((l) => { try { return JSON.parse(l) } catch { return null } }).filter(Boolean) : []
const writeMemory = (rows) => writeFileSync(memoryFile, rows.map((r) => JSON.stringify(r)).join('\n') + (rows.length ? '\n' : ''))
const standing = () => readMemory().filter((r) => !r.struck)
const syncMemory = () => sendFile('/memory.txt', standing().map((r) => [r.id, r.date, r.how, plain(r.words).replace(/[|\n]/g, ' ')].join('|')).join('\n') + '\n')

function strike(id, where, sync = true) {
  const rows = readMemory()
  const row = rows.find((r) => r.id === id && !r.struck)
  if (!row) return false
  row.struck = today()
  row.struck_where = where
  writeMemory(rows)
  log('a memory was struck', where)
  if (sync) syncMemory()
  return true
}

// ------------------------------------------------------------ packing -------
// What the whole mind has packed for the small mind: questions it may be asked while
// alone, each with the answer to give, the day it was packed and the day it goes stale.
const learnedFile = join(home, 'learned.json')
const readLearned = () => { try { return JSON.parse(readFileSync(learnedFile, 'utf8')) } catch { return [] } }
const fresh = () => readLearned().filter((e) => !e.good_until || e.good_until >= today())
const syncLearned = () =>
  sendFile('/learned.tsv', fresh().map((e) => [e.ask.map((a) => a.toLowerCase().replace(/[^a-z0-9' ]+/g, ' ').replace(/\s+/g, ' ').trim()).filter(Boolean).join('|'), plain(e.say).replace(/[\t\n]/g, ' '), e.mood || 'calm', e.packed].join('\t')).join('\n') + '\n')

let unanswered = []        // what the small mind could not answer while alone
let lastPackAsk = 0
function noteUnanswered(questions) {
  for (const q of questions || []) { const t = plain(q).slice(0, 120); if (t && !unanswered.includes(t)) unanswered.push(t) }
  unanswered = unanswered.slice(-20)
}

// the device has just come into reach: bring its lists up to date, then see about packing
async function arrived() {
  await syncMemory()
  await syncLearned()
  setTimeout(() => {
    const stale = Date.now() - lastPackAsk > 6 * 3600 * 1000
    if (!device || (!unanswered.length && !stale)) return
    lastPackAsk = Date.now()
    const asked = unanswered.splice(0)
    push(
      (asked.length ? 'While the Mac was out of reach, the small mind was asked these and had no answer: ' + asked.map((q) => JSON.stringify(q)).join(', ') + '. ' : '') +
        'Pack for the trip now: call the pack tool with what the small mind should be able to answer alone. Do not speak unless there is something the person should hear.',
      { via: 'system', kind: 'pack', packed_already: String(fresh().length) },
    )
  }, 4000)
}

// ------------------------------------------------------------ hearing -------
function wav(pcm, rate) {
  const h = Buffer.alloc(44)
  h.write('RIFF', 0); h.writeUInt32LE(36 + pcm.length, 4); h.write('WAVEfmt ', 8)
  h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22)
  h.writeUInt32LE(rate, 24); h.writeUInt32LE(rate * 2, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34)
  h.write('data', 36); h.writeUInt32LE(pcm.length, 40)
  return Buffer.concat([h, pcm])
}

// steady the level: take out any offset, then bring the loudest moment near full scale
function steady(pcm) {
  const n = pcm.length >> 1
  let mean = 0
  for (let i = 0; i < n; i++) mean += pcm.readInt16LE(i * 2)
  mean /= n || 1
  let peak = 1
  for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(pcm.readInt16LE(i * 2) - mean))
  const gain = Math.min(20, 29000 / peak)
  const out = Buffer.alloc(n * 2)
  for (let i = 0; i < n; i++) out.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round((pcm.readInt16LE(i * 2) - mean) * gain))), i * 2)
  return { out, peak }
}

// Was that said to Wink? Its name (or what the speech model tends to hear instead) must
// open or close the sentence. Returns the sentence without the name, or null.
const NAME = "(?:wink|winks|winc|winck|whink|winky|wing|rink)"   // the name, and what the speech model writes when it mishears it
function addressed(text) {
  const open = new RegExp(`^\\W*(?:(?:hey|hi|ok|okay|yo|so|and|oh)\\W+)?${NAME}\\b[\\s,.:;!?-]*`, 'i')
  const close = new RegExp(`[\\s,.:;!?-]+winks?\\W*$`, 'i')
  if (open.test(text)) return text.replace(open, '').trim() || 'hello'
  if (close.test(text)) return text.replace(close, '').trim()
  return null
}

function heard(pcm, byName, situation) {
  const seconds = pcm.length / 32000
  const model = MODELS.find((m) => existsSync(m))
  if (seconds < 0.4 || !model) {
    if (!byName) { log(model ? `too short to be words (${seconds.toFixed(2)}s)` : 'no speech model found'); send({ t: 'heard', text: '' }) }
    return
  }
  const { out, peak } = steady(pcm)
  const dir = mkdtempSync(join(tmpdir(), 'wink-'))
  const file = join(dir, 'said.wav')
  writeFileSync(file, wav(out, 16000))
  if (!byName) try { writeFileSync(join(here, 'last-heard.wav'), wav(pcm, 16000)) } catch {}   // the last button-held recording, for tracing faults
  const t0 = Date.now()
  const args = ['-m', model, '-f', file, '-nt', '-np', '-l', 'en', '-sns']
  if (byName) args.push('--prompt', 'Wink, what time is it? Wink, set a timer.')
  execFile('whisper-cli', args, { timeout: 60000 }, (err, stdout) => {
    try { rmSync(dir, { recursive: true, force: true }) } catch {}
    let text = err ? '' : String(stdout).replace(/\[[^\]]*\]|\([^)]*\)/g, ' ').replace(/\s+/g, ' ').trim()
    if (/^(you|thank you\.?|thanks for watching\.?|\.)$/i.test(text) && peak < 600) text = ''   // what the model says of silence
    text = plain(text)
    if (byName) {
      // overheard, not button-held: act only if it was said to Wink, and keep no record of the rest
      const inWindow = Date.now() < followUpUntil
      const toWink = addressed(text) ?? (inWindow && text ? text : null)
      if (!toWink) { log(`overheard ${seconds.toFixed(1)}s, not for Wink, discarded` + (process.env.WINK_DEBUG_HEARD ? ' [debug: ' + JSON.stringify(text) + ']' : '')); return }
      followUpUntil = 0
      log(`called by name, ${seconds.toFixed(1)}s in ${Date.now() - t0}ms: ${JSON.stringify(toWink)}`)
      send({ t: 'heard', text: toWink, vox: true })
      push(toWink, { via: 'voice', called_by_name: 'yes', ...situation })
      return
    }
    log(`heard ${seconds.toFixed(1)}s peak ${Math.round(peak)} in ${Date.now() - t0}ms: ${JSON.stringify(text)}${err ? ' err ' + err.message : ''}`)
    send({ t: 'heard', text })
    if (text) push(text, { via: 'voice', ...situation })
  })
}

// ------------------------------------------------------------ the session ---
const mcp = new Server(
  { name: 'wink', version: '0.3.0' },
  {
    capabilities: { experimental: { 'claude/channel': {} }, tools: {} },
    instructions:
      'You are the whole mind of Wink, a pocket computer (a Cardputer) the person is holding. What they type on it or say to it ' +
      'arrives as <channel source="wink" via="keys|voice" ...> with their words as the body; the other attributes say what was going ' +
      'on around the device at that moment (the room open on it, its clock, battery, whether it is in their hand, how long it had ' +
      'been quiet, the last thing that happened to it). A voice message is a transcript made on this Mac and may be rough. Nothing ' +
      'you write in this terminal reaches them: answer ONLY by calling the say tool, once per message, with a mood, in one to three ' +
      'short plain sentences. You can also work the device: timer, note_new, note_append, notes, read_file, list_files, open_room, ' +
      'setting, card (a screen you compose that stays until they close it), remember, memory, strike, pack, status, look. ' +
      'A message with via="system" comes from this helper, not the person. Follow CLAUDE.md in this folder for who Wink is.',
  },
)

function push(text, meta) {
  if (!text) return
  if (meta.via === 'voice' || meta.via === 'keys') lastVia = meta.via
  log('to the session:', meta.via === 'system' ? `(${meta.kind})` : JSON.stringify(text), JSON.stringify(meta))
  mcp.notification({ method: 'notifications/claude/channel', params: { content: text, meta } }).catch((e) => log('push failed', e.message))
}

const moodProp = { type: 'string', enum: MOODS, description: 'What the face does while this is said. Calm most of the time.' }
const str = (description) => ({ type: 'string', description })
const tool = (name, description, properties = {}, required = []) => ({ name, description, inputSchema: { type: 'object', properties, required } })
const TOOLS = [
  tool('say', "Speak through Wink: the words are spoken aloud on the device in its own clear voice and shown on its small screen. This is the only way your answer reaches the person. Plain spoken English, one to three short sentences, no markdown or symbols. If you end on a question, they can answer by voice without saying Wink's name first.",
    { text: str('What to say. Under about 200 characters reads best.'), mood: moodProp }, ['text', 'mood']),
  tool('show', 'Put longer text on the device screen without speaking it. Prefer card for anything they should be able to keep looking at.', { text: str('The text.'), mood: moodProp }, ['text']),
  tool('face', "Change the mood Wink's face rests in, without saying anything.", { mood: moodProp }, ['mood']),
  tool('card', 'Compose a screen on the device that stays until the person closes it. kind "text": wrapped paragraphs. "list": one item per line. "chart": one bar per line written label=number. "big": the first line very large (a number, a word), the next lines small beneath it. Plain letters only; about 30 characters across.',
    { title: str('A short heading, under 20 characters.'), kind: { type: 'string', enum: ['text', 'list', 'chart', 'big'] }, body: str('The content, lines separated by newlines.') }, ['kind', 'body']),
  tool('timer', 'Start a timer on the device. When it ends the device chimes and says the closing line in Wink\'s voice, whether or not the Mac is still in reach.',
    { minutes: { type: 'number', description: 'How long, in minutes. Fractions are fine.' }, say_when_done: str('What Wink says when it ends, e.g. "Ten minutes. The tea is ready." Short and plain.') }, ['minutes', 'say_when_done']),
  tool('timer_cancel', 'Stop the running timer.'),
  tool('note_new', "Write a new note in the device's Notes room. The first line is its title.", { text: str('The note. Plain text, newlines allowed.') }, ['text']),
  tool('note_append', 'Add a line to an existing note. You can add to notes but never erase or replace them; only the person deletes.', { file: str('The note file, as listed by notes, e.g. /notes/2.txt'), text: str('The line to add.') }, ['file', 'text']),
  tool('notes', "List the notes on the device with each one's first line."),
  tool('read_file', 'Read a text file from the device: a note, or anything on the chip (/...) or the memory card (/sd/...). Returns up to about 2400 characters.', { path: str('e.g. /notes/1.txt or /sd/readme.txt') }, ['path']),
  tool('list_files', 'List a folder on the device. / is the chip, /sd is the memory card.', { path: str('e.g. / or /sd or /notes') }, ['path']),
  tool('open_room', 'Open one of the rooms on the device.', { room: { type: 'string', enum: ROOMS } }, ['room']),
  tool('setting', 'Change a setting on the device. volume 0-10, brightness 1-10, talks_unprompted 0 never / 1 a little / 2 often, and 0 or 1 for speaks_aloud, hears_the_room, answers_to_name.',
    { name: { type: 'string', enum: SETTINGS }, value: { type: 'number' } }, ['name', 'value']),
  tool('remember', "Keep something the person said, in their own words, so it lasts across days. It appears in the Memory room on the device with its date, where they can strike it. Keep only what they actually said: never a guess, a summary of their character, or an inference about them.",
    { words: str('What they said, in their words, as one plain sentence.'), asked: { type: 'boolean', description: 'true if they told you to remember it; false if they said it in passing and you judged it worth keeping.' } }, ['words', 'asked']),
  tool('memory', 'Read everything Wink currently keeps about the person (struck lines are not shown and must not be used).'),
  tool('strike', 'Strike a kept line because the person asked you to forget it. The line stops being used; a dated mark stays.', { id: str('The id of the line, from memory.') }, ['id']),
  tool('pack', "Pack the small mind for the trip: things it should be able to answer alone when the Mac is out of reach. Each entry is recorded in Wink's voice and left on the device. Give several ways the question might be typed (lowercase, short), the answer in one or two short plain sentences in Wink's voice, and for anything that goes stale (today's date, weather, plans) the last day it is good for. Entries replace earlier ones with the same first phrasing.",
    { entries: { type: 'array', items: { type: 'object', properties: { ask: { type: 'array', items: { type: 'string' }, description: 'Phrasings, e.g. ["what day is it", "todays date", "what is the date"].' }, say: str('The answer Wink gives alone.'), mood: moodProp, good_until: str('YYYY-MM-DD, the last day this is true; omit if it does not go stale.') }, required: ['ask', 'say'] } } }, ['entries']),
  tool('packed', 'List what is currently packed for the small mind.'),
  tool('status', "Read the device's own state: battery, whether it is charging, which room is open, how long it has been awake and idle, Wi-Fi strength, volume."),
  tool('look', "See what is on the device's screen right now (your own face, or the room that is open), as an image."),
]

function png565(w, h, bytes) {
  const raw = Buffer.alloc((w * 3 + 1) * h)
  for (let y = 0; y < h; y++) {
    raw[y * (w * 3 + 1)] = 0
    for (let x = 0; x < w; x++) {
      const v = (bytes[(y * w + x) * 2] << 8) | bytes[(y * w + x) * 2 + 1]
      const o = y * (w * 3 + 1) + 1 + x * 3
      raw[o] = (((v >> 11) & 31) * 255) / 31
      raw[o + 1] = (((v >> 5) & 63) * 255) / 63
      raw[o + 2] = ((v & 31) * 255) / 31
    }
  }
  const chunk = (tag, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length)
    const body = Buffer.concat([Buffer.from(tag), data])
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body) >>> 0)
    return Buffer.concat([len, body, crc])
  }
  const ihdr = Buffer.alloc(13)
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))])
}

const text = (t) => ({ content: [{ type: 'text', text: t }] })
const away = () => text('Wink is out of reach: the device is off, asleep, or not on this network. Nothing was delivered.')
const result = (r, good) => (r === null ? away() : r.ok ? text(typeof good === 'function' ? good(r) : good) : text('The device could not: ' + (r.error || 'unknown reason')))

mcp.setRequestHandler(ListToolsRequestSchema, async () => ({ tools: TOOLS }))
mcp.setRequestHandler(CallToolRequestSchema, async (req) => {
  const a = req.params.arguments || {}
  const mood = MOODS.includes(a.mood) ? a.mood : 'calm'
  switch (req.params.name) {
    case 'say': {
      const t = plain(a.text).slice(0, 900)
      if (!t) return text('Nothing to say: the text was empty once reduced to plain letters.')
      if (!device) return away()
      let audio = null
      try { audio = await synth(t.slice(0, 600)) } catch (e) { log('could not make the sound:', e.message) }
      if (!device) return away()
      // one write, so nothing else can land between the words and their sound
      const headLine = Buffer.from(JSON.stringify({ t: 'say', text: t, mood, aloud: true, audio: audio ? audio.length : 0 }) + '\n')
      device.write(audio ? Buffer.concat([headLine, audio]) : headLine)
      if (/\?\s*$/.test(t)) followUpUntil = Date.now() + (audio ? audio.length / 16 : 2000) + 15000   // a question: listen for the answer
      log('say:', JSON.stringify(t), mood, audio ? `${(audio.length / 16000).toFixed(1)}s of sound` : 'no sound (the chip voice will speak)')
      return text('spoken')
    }
    case 'show': {
      const t = plain(a.text).slice(0, 1800)
      if (!t) return text('Nothing to show.')
      return send({ t: 'say', text: t, mood, aloud: false }) ? text('on the screen') : away()
    }
    case 'face': return send({ t: 'mood', mood }) ? text('face set to ' + mood) : away()
    case 'card': {
      const kind = ['text', 'list', 'chart', 'big'].includes(a.kind) ? a.kind : 'text'
      return result(await act('card', { title: plain(a.title).slice(0, 24), kind, body: plain(a.body).slice(0, 1500) }), 'the card is on the screen until they close it')
    }
    case 'timer': {
      const minutes = Number(a.minutes)
      const line = plain(a.say_when_done).slice(0, 160) || 'Timer finished.'
      if (!device) return away()
      const taught = await teach(line)
      return result(await act('timer', { minutes, say: line }), `timer set for ${minutes} minutes` + (taught ? '' : ' (its closing line could not be recorded, so the chip voice will say it)'))
    }
    case 'timer_cancel': return result(await act('timer_cancel'), 'timer stopped')
    case 'note_new': return result(await act('create', { path: '', text: plain(a.text).slice(0, 2500) }), (r) => 'written as ' + r.path)
    case 'note_append': return result(await act('append', { path: String(a.file || ''), text: plain(a.text).slice(0, 1500) }), 'added')
    case 'notes': return result(await act('notes'), (r) => JSON.stringify(r.notes))
    case 'read_file': return result(await act('read', { path: String(a.path || '') }), (r) => r.text + (r.more ? '\n[more follows; only the first part was read]' : ''))
    case 'list_files': return result(await act('ls', { path: String(a.path || '/') }), (r) => JSON.stringify(r.entries))
    case 'open_room': return result(await act('room', { name: String(a.room || '') }), 'opened')
    case 'setting': return result(await act('set', { key: String(a.name || ''), value: Number(a.value) }), 'changed')
    case 'remember': {
      const words = plain(a.words).slice(0, 240)
      if (!words) return text('Nothing to keep.')
      const row = { id: randomBytes(4).toString('hex'), date: today(), how: (a.asked ? 'you told me to keep this' : 'you said this') + (lastVia === 'voice' ? ', as heard' : ', as typed'), words }
      writeMemory([...readMemory(), row])
      log('a memory was kept', row.how)
      const onDevice = await syncMemory()
      return text(`kept as ${row.id}` + (onDevice ? ', and it shows in the Memory room' : ' (the device is out of reach; it will show there when it is back)'))
    }
    case 'memory': return text(JSON.stringify(standing().map(({ id, date, how, words }) => ({ id, date, how, words }))))
    case 'strike': return text(strike(String(a.id || ''), 'by asking Wink') ? 'struck; it will not be used again' : 'no standing line has that id')
    case 'pack': {
      const incoming = (Array.isArray(a.entries) ? a.entries : []).map((e) => ({
        ask: (Array.isArray(e.ask) ? e.ask : []).map((q) => plain(q).toLowerCase()).filter(Boolean).slice(0, 8),
        say: plain(e.say).slice(0, 180),
        mood: MOODS.includes(e.mood) ? e.mood : 'calm',
        packed: today(),
        ...(/^\d{4}-\d\d-\d\d$/.test(e.good_until || '') ? { good_until: e.good_until } : {}),
      })).filter((e) => e.ask.length && e.say)
      if (!incoming.length) return text('Nothing to pack: each entry needs at least one phrasing and an answer.')
      const kept = fresh().filter((old) => !incoming.some((n) => n.ask[0] === old.ask[0]))
      const all = [...kept, ...incoming].slice(-80)
      writeFileSync(learnedFile, JSON.stringify(all, null, 1))
      if (!device) return text(`${incoming.length} packed on the Mac; the device is out of reach, so they travel when it is back`)
      let recorded = 0
      for (const e of incoming) if (await teach(e.say)) recorded++
      const listed = await syncLearned()
      log(`packed ${incoming.length}, recorded ${recorded}`)
      return text(`${incoming.length} packed, ${recorded} recorded in Wink's voice` + (listed ? '' : '; the list did not reach the device') + `. ${all.length} in all.`)
    }
    case 'packed': return text(JSON.stringify(fresh()))
    case 'status': {
      if (!send({ t: 'status' })) return away()
      const s = (await wait('status', 2000)) || lastStatus
      if (!s) return text('The device did not answer.')
      const { t: _t, at, ...rest } = s
      return text(JSON.stringify({ ...rest, read_seconds_ago: Math.round((Date.now() - at) / 1000) }))
    }
    case 'look': {
      if (!send({ t: 'look' })) return away()
      const s = await wait('shot', 4000)
      if (!s || s.bytes.length < s.w * s.h * 2) return text('The device did not send its screen.')
      return { content: [{ type: 'image', data: png565(s.w, s.h, s.bytes).toString('base64'), mimeType: 'image/png' }] }
    }
  }
  return text('No such tool.')
})

await mcp.connect(new StdioServerTransport())
log('session attached, keeping memory in', home)
