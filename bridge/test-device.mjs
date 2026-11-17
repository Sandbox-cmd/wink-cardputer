// Checks the real device against the real bridge, with a stand-in for the Claude Code
// session. The device must be on Wi-Fi, paired, plugged in by its cable (the test
// presses its keys through the cable), and no real session may be running.
// It keeps its test memories in a scratch folder, not in Wink's real memory, and the
// Mac speaks a few sentences aloud for the hearing checks.
//   node test-device.mjs [folder-for-screenshots]
import { Client } from '@modelcontextprotocol/sdk/client/index.js'
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js'
import { execFileSync, spawn } from 'node:child_process'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const shots = process.argv[2]
const scratch = mkdtempSync(join(tmpdir(), 'wink-device-test-'))
const dev = (...steps) => execFileSync(join(here, '../tools/dev'), steps, { encoding: 'utf8' })
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const aloud = (words) => new Promise((r) => spawn('say', [words]).on('close', r))
let failed = 0
const check = (name, ok, detail = '') => {
  console.log(`${ok ? 'pass' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`)
  if (!ok) failed++
}

// before the Mac is in reach: a kept line is struck on the device, alone
const awayLine = 'A line to strike while away.'
writeFileSync(join(scratch, 'memory.jsonl'), JSON.stringify({ id: 'testaway', date: '2026-11-16', how: 'you said this, as typed', words: awayLine }) + '\n')
dev('cmd:rm /struck.txt', 'cmd:rm /memory.txt', `cmd:write /memory.txt testaway|2026-11-16|you said this, as typed|${awayLine}`, 'cmd:room memory', 'cmd:press del', 'cmd:press del')
check('alone: a line struck in the Memory room is noted on the device', dev('cmd:cat /struck.txt').includes('testaway'))
dev('cmd:room home')

const session = new Client({ name: 'stand-in-session', version: '0' }, { capabilities: {} })
const pushed = []
session.fallbackNotificationHandler = async (n) => { if (n.method === 'notifications/claude/channel') pushed.push(n.params) }
await session.connect(new StdioClientTransport({ command: process.execPath, args: [join(here, 'wink-mind.mjs')], env: { ...process.env, WINK_HOME: scratch } }))
const call = async (name, args = {}) => (await session.callTool({ name, arguments: args })).content[0]
const fromPerson = () => pushed.filter((p) => p.meta.via !== 'system')
const until = async (test, ms = 20000) => { for (let t = 0; t < ms && !test(); t += 250) await sleep(250); return test() }
const snap = async (name) => {
  const look = await call('look')
  if (look.type === 'image' && shots) writeFileSync(join(shots, name), Buffer.from(look.data, 'base64'))
  return look.type === 'image'
}

// ---- reach
let status
const t0 = Date.now()
for (let i = 0; i < 45 && !status; i++) {
  const s = await call('status')
  if (s.text.startsWith('{')) status = JSON.parse(s.text)
  else await sleep(1000)
}
check('the device finds the Mac by itself and is let in', !!status, status ? `after ${((Date.now() - t0) / 1000).toFixed(0)}s` : 'never arrived in 45s')
if (!status) { await session.close(); process.exit(1) }
check('back in reach: the line struck while away is struck on the Mac and does not return to the device', readFileSync(join(scratch, 'memory.jsonl'), 'utf8').includes('"struck"') && !dev('cmd:cat /memory.txt').includes(awayLine))
dev('cmd:room home', 'cmd:hush')
await sleep(3000)   // let it finish saying that the whole mind arrived

// ---- talking, with awareness
check('say is delivered', (await call('say', { text: 'This is the whole mind, testing the line.', mood: 'happy' })).text === 'spoken')
await sleep(4500)
check('the last thing it said came as sound from the Mac', /last spoke from: sound from the Mac/.test(dev('cmd:voice')))
dev('cmd:room notes')
dev('cmd:room home', 'type:what is the tallest mountain', 'cmd:press enter')
await until(() => fromPerson().length > 0, 5000)
const typed = fromPerson().at(-1)
check('a typed line reaches the session with the situation around it', typed?.content === 'what is the tallest mountain' && typed.meta.room === 'Talk' && 'in_hand' in typed.meta && 'battery' in typed.meta, JSON.stringify(typed?.meta))
await call('say', { text: 'Everest.', mood: 'calm' })
await sleep(2500)

// ---- act
const line = 'That was six seconds. The test timer is done.'
check('timer is set', (await call('timer', { minutes: 0.1, say_when_done: line })).text.startsWith('timer set for 0.1'))
const note = await call('note_new', { text: 'Test note from the whole mind\nsecond line' })
const noteFile = note.text.match(/written as (\S+)/)?.[1]
check('note_new writes a note on the device', !!noteFile, note.text)
check('notes lists it', (await call('notes')).text.includes('Test note from the whole mind'))
await call('note_append', { file: noteFile, text: 'a line added later' })
check('read_file reads it back, with the added line', /second line\s+a line added later/.test((await call('read_file', { path: noteFile })).text))
if (noteFile) dev(`cmd:rm ${noteFile}`)   // the test's own note
check('list_files sees the memory card', (await call('list_files', { path: '/sd/wink' })).text.includes('voice.pak'))
await sleep(9000)   // the timer rings and says its line while we wait
check('card puts a composed screen up', (await call('card', { title: 'Peaks', kind: 'chart', body: 'Everest=8849\nK2=8611\nKangchenjunga=8586\nLhotse=8516' })).text.includes('until they close it'))
await sleep(600)
check('look sees the card', await snap('card.png'))
check('the timer ended and spoke its closing line from a recording made for it', /last spoke from: recordings on the card/.test(dev('cmd:voice')), dev('cmd:voice').split('\n')[0])
check('setting reaches the device (set to the value it already has, so nothing of the owner\'s changes)', (await call('setting', { name: 'volume', value: status.volume })).text === 'changed')
check('open_room goes back to the face', (await call('open_room', { room: 'talk' })).text === 'opened')

// ---- remember
const words = 'The test printer is on the second floor.'
const kept = await call('remember', { words, asked: true })
check('remember keeps the words and they reach the device', kept.text.includes('Memory room') && dev('cmd:cat /memory.txt').includes(words), kept.text)
await call('open_room', { room: 'memory' })
await sleep(500)
await snap('memory.png')
await call('strike', { id: kept.text.match(/kept as (\w+)/)?.[1] })
await sleep(800)
check('a struck line leaves the device', !dev('cmd:cat /memory.txt').includes(words))
await call('open_room', { room: 'talk' })

// ---- hearing: by name, hands-free
let before = fromPerson().length
await aloud('The meeting moved to Thursday afternoon.')
await sleep(5000)
check('speech in the room that does not name Wink is not passed on', fromPerson().length === before)
await aloud('Wink, what time is it?')
await until(() => fromPerson().length > before, 15000)
const called = fromPerson()[before]
check('"Wink, what time is it?" said in the room reaches the session', !!called && called.meta.called_by_name === 'yes' && /time/i.test(called.content), JSON.stringify(called ?? 'nothing arrived'))
await call('say', { text: 'Twenty to ten.', mood: 'calm' })
await sleep(3500)

// ---- hearing: the button
before = fromPerson().length
const droppedBefore = Number(dev('cmd:ears').match(/dropped (\d+)/)?.[1])
dev('cmd:talk on')
await sleep(400)
await aloud('What is the weather like on Mars?')
await sleep(300)
dev('cmd:talk off')
await until(() => fromPerson().length > before, 20000)
const voice = fromPerson()[before]
check('with the button held, the whole sentence reaches the session', !!voice && /weather/i.test(voice.content) && /mars/i.test(voice.content), JSON.stringify(voice?.content ?? 'nothing heard'))
const droppedAfter = Number(dev('cmd:ears').match(/dropped (\d+)/)?.[1])
check('and no sound was lost on the way', droppedAfter === droppedBefore, `${droppedAfter - droppedBefore} pieces of 16 ms dropped`)
await call('say', { text: 'Cold and dusty.', mood: 'cheeky' })
await sleep(3000)

// ---- grow: pack, go out of reach, and ask the small mind
const answer = 'The test answer is forty two.'
const pack = await call('pack', { entries: [{ ask: ['what is the test answer', 'test answer'], say: answer, mood: 'cheeky' }] })
check('pack records an answer and leaves it on the device', pack.text.startsWith('1 packed, 1 recorded'), pack.text)
console.log(dev('cmd:sys').split('\n').filter((l) => /memory/.test(l)).join('\n'))
await session.close()   // the whole mind goes out of reach
await sleep(9000)
dev('cmd:room home', 'cmd:hush', 'type:what is the test answer', 'cmd:press enter')
await sleep(3500)
const after = dev('cmd:voice', 'cmd:link')
check('alone, the small mind gives the packed answer in a recording', /last spoke from: recordings on the card/.test(after) && /whole mind away/.test(after), after.split('\n').filter((l) => l && !l.startsWith('#')).join(' / '))
if (shots) dev(`shot:${join(shots, 'alone.png')}`)

console.log(failed ? `${failed} check(s) failed` : 'all checks passed')
process.exit(failed ? 1 : 0)
