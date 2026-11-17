// Pairs a Cardputer with this Mac: makes a shared secret and gives it to the device over
// the USB cable. Only a device holding the secret can speak into your Claude Code session.
//   node tools/pair.mjs            (keeps an existing bridge/pair.json)
//   node tools/pair.mjs --new      (makes a fresh secret)
import { execFileSync } from 'node:child_process'
import { randomBytes } from 'node:crypto'
import { existsSync, readFileSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const file = join(root, 'bridge/pair.json')
if (!existsSync(file) || process.argv.includes('--new')) {
  writeFileSync(file, JSON.stringify({ token: randomBytes(18).toString('base64url'), port: 8765 }, null, 2) + '\n', { mode: 0o600 })
  console.log('made a new secret in bridge/pair.json (keep it private)')
}
const { token } = JSON.parse(readFileSync(file, 'utf8'))
const out = execFileSync(join(root, 'tools/dev'), [`cmd:link token ${token}`], { encoding: 'utf8' })
console.log(out.includes('token kept') ? 'the Cardputer has the secret: it will find this Mac by itself on the same network' : 'the Cardputer did not answer: is it plugged in and running Wink?')
