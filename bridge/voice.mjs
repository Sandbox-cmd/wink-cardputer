// Wink's voice, made on the Mac: macOS speech in the chosen voice, shaped for a very
// small speaker, as 16 kHz mu-law (one byte a sample). Used live by the bridge for the
// whole mind's replies and ahead of time by tools/make-voice.mjs for the small mind's lines.
import { execFile } from 'node:child_process'
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { promisify } from 'node:util'
import { treat } from '../sound/childlike.mjs'

const run = promisify(execFile)
const here = dirname(fileURLToPath(import.meta.url))
const settingsFile = join(here, 'voice.json')
export const settings = { voice: 'Samantha', rate: 0, childlike: 4, ...(existsSync(settingsFile) ? JSON.parse(readFileSync(settingsFile, 'utf8')) : {}) }
export const RATE = 16000

// the same text always finds the same recording: lowercase letters, digits,
// apostrophes and single spaces, nothing else
export const norm = (s) => String(s).toLowerCase().replace(/[^a-z0-9']+/g, ' ').trim()

export function hash(s) {   // FNV-1a, 32 bits; the device computes the same
  let h = 0x811c9dc5
  for (const c of Buffer.from(norm(s), 'latin1')) h = Math.imul(h ^ c, 0x01000193) >>> 0
  return h >>> 0
}

// trim: cut the silence macOS leaves before and after, for clips played back to back
export async function synth(text, { trim = false } = {}) {
  const dir = mkdtempSync(join(tmpdir(), 'wink-voice-'))
  try {
    const aiff = join(dir, 'a.aiff'), raw = join(dir, 'a.ul')
    const args = ['-v', settings.voice, '-o', aiff]
    if (settings.rate) args.push('-r', String(settings.rate))
    await run('say', [...args, text], { timeout: 12000, killSignal: 'SIGKILL' })
    // Wink's voice is a smaller, brighter Samantha (sound/childlike.py); set childlike to 0 in voice.json for the plain one
    if (settings.childlike) {
      await treat(aiff, raw, { trim, strength: settings.childlike })
      return readFileSync(raw)
    }
    const cut = 'silenceremove=start_periods=1:start_threshold=-48dB'
    const filters = ['highpass=f=160', ...(trim ? [cut, 'areverse', cut, 'areverse'] : []), 'alimiter=limit=0.95']
    await run('ffmpeg', ['-loglevel', 'error', '-y', '-i', aiff, '-af', filters.join(','), '-ar', String(RATE), '-ac', '1', '-f', 'mulaw', raw], { timeout: 30000 })
    return readFileSync(raw)
  } finally {
    rmSync(dir, { recursive: true, force: true })
  }
}
