// The childlike Samantha: turns what `say` wrote into 16 kHz mono mu-law for the device,
// with the same high-pass (160 Hz) and the same limiter (0.95, last) as bridge/voice.mjs,
// and the voice made smaller between them (sound/childlike.py: formants up, pitch up
// more, a touch quicker, a +3 dB shelf above 3 kHz). Written 17 November 2026.
//
//   import { treat } from '../sound/childlike.mjs'
//   await treat('/tmp/a.aiff', '/tmp/a.ul')                  // as voice.mjs's synth() without trim
//   await treat('/tmp/a.aiff', '/tmp/a.ul', { trim: true })  // with voice.mjs's silence trim
//
// It runs the project's Python (train/.venv, which has praat-parselmouth installed).
// Cost on this Mac, measured by sound/childlike_check.py: a 3.9 s sentence takes about
// 205 ms against 39 ms for voice.mjs's plain ffmpeg call, so about 165 ms more per call
// (86 ms of it starting Python and loading the library), of which about 6 ms per second
// of speech is the change itself. The output is not bit-repeatable between runs.
import { execFile } from 'node:child_process'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { promisify } from 'node:util'

const run = promisify(execFile)
const here = dirname(fileURLToPath(import.meta.url))
const project = dirname(here)
export const PYTHON = join(project, 'train', '.venv', 'bin', 'python')
export const SCRIPT = join(here, 'childlike.py')

// strength: 0 (only the top shelf) to 8; leave it out for the chosen one (childlike.py CHOSEN)
export async function treat(aiffPath, outUlawPath, { trim = false, strength } = {}) {
  const args = ['-I', SCRIPT, aiffPath, outUlawPath]
  if (strength !== undefined || trim) args.push(String(strength ?? 'chosen'))
  if (trim) args.push('trim')
  await run(PYTHON, args, { timeout: 30000, killSignal: 'SIGKILL' })
  return outUlawPath
}
