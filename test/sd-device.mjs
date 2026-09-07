import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';

const configDir = process.platform === 'darwin' ? join(homedir(), 'Library/Application Support') : (process.env.XDG_CONFIG_HOME || join(homedir(), '.config'));
const config = JSON.parse(await readFile(join(configDir, 'cardtunes/config.json'), 'utf8'));
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function api(path, form) {
  const response = await fetch(config.url + path, {
    method: form ? 'POST' : 'GET', headers: {Authorization: `Bearer ${config.token}`},
    body: form ? new URLSearchParams(form) : undefined, signal: AbortSignal.timeout(15000)
  });
  assert.ok(response.ok, `${path}: ${response.status}`);
  return response.json();
}
const control = (action, value = '') => api('/api/control', {action, value: String(value)});
const status = () => api('/api/status');
async function until(predicate, timeout = 30000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    try { const s = await status(); if (predicate(s)) return s; } catch {}
    await wait(500);
  }
  throw new Error('Device did not reach expected state');
}

assert.equal((await status()).sd_mounted, true);
const path = process.argv[2] || '/Music/Redbone/Come and Get Your Love.mp3';
async function findTrack() {
  let offset = 0;
  while (offset >= 0) {
    const page = await api(`/api/library?offset=${offset}`);
    const match = page.tracks.find(track => track.path === path);
    if (match) return match;
    offset = page.next_offset;
  }
  throw new Error('Expected microSD track is missing');
}
let track = await findTrack();
assert.ok(track.path.startsWith('/Music/'), 'Test must use microSD, not the flash demo');
await control('play-library', track.id);
let playing = await until(s => s.state === 'playing' && s.track?.path === path);
assert.ok(playing.duration_ms > 65000, 'Use a track at least 65 seconds long');
await control('seek', 30);
await until(s => s.position_ms >= 30000 && s.position_ms < 32000);
await control('pause');
const paused = await until(s => s.state === 'paused');
await wait(500);
assert.equal((await status()).position_ms, paused.position_ms);
await control('rescan');
const scanned = await until(s => !s.scan?.active, 900000);
assert.notEqual(scanned.scan?.succeeded, false, scanned.scan?.error);
track = await findTrack();
await control('play-library', track.id);
await until(s => s.state === 'playing' && s.track?.path === path);
await control('seek', 60);
await until(s => s.position_ms >= 60000 && s.position_ms < 62000);
await control('pause');
const checkpoint = await until(s => s.state === 'paused');
console.log(JSON.stringify({sd_playback: 'passed', seek: 'passed', rescan: 'passed', path, checkpoint_ms: checkpoint.position_ms}));

if (process.argv[3]) {
  const result = await promisify(execFile)('build/cardtunes', ['firmware', process.argv[3]], {timeout: 180000});
  console.log(result.stdout.trim());
  const rebooted = await until(s => s.uptime_ms < checkpoint.uptime_ms && s.state === 'paused' && s.track?.path === path, 90000);
  assert.equal(rebooted.sd_mounted, true);
  assert.ok(Math.abs(rebooted.position_ms - checkpoint.position_ms) <= 1);
  console.log(JSON.stringify({reboot_resume: 'passed', version: rebooted.version, path: rebooted.track.path, position_ms: rebooted.position_ms}));
}
await control('play');
playing = await until(s => s.state === 'playing');
await wait(2000);
const final = await status();
assert.equal(final.error, '');
assert.ok(final.position_ms > playing.position_ms);
assert.equal(final.track.path, path);
console.log(JSON.stringify({playing_from_sd: true, position_ms: final.position_ms, heap_free: final.heap_free}));
