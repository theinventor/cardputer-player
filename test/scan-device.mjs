import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';

const directory = process.platform === 'darwin' ? join(homedir(), 'Library/Application Support') : (process.env.XDG_CONFIG_HOME || join(homedir(), '.config'));
const config = JSON.parse(await readFile(join(directory, 'cardtunes/config.json'), 'utf8'));
const headers = {Authorization: `Bearer ${config.token}`};
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function api(path, form, expected = 200) {
  const response = await fetch(config.url + path, {headers, method: form ? 'POST' : 'GET',
    body: form ? new URLSearchParams(form) : undefined, signal: AbortSignal.timeout(15000)});
  const data = await response.json();
  assert.equal(response.status, expected, `${path}: ${JSON.stringify(data)}`);
  return data;
}
const status = () => api('/api/status');
const control = (action, value = '', expected = 202) => api('/api/control', {action, value: String(value)}, expected);
async function until(predicate, timeout = 60000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) { const s = await status(); if (predicate(s)) return s; await wait(400); }
  throw Error('Device did not reach expected state');
}
async function screenshot(name) {
  await mkdir('build/screenshots', {recursive: true});
  const response = await fetch(config.url + '/api/screen.bmp', {headers, signal: AbortSignal.timeout(15000)});
  assert(response.ok); const bmp = Buffer.from(await response.arrayBuffer());
  assert.equal(bmp.subarray(0, 2).toString(), 'BM');
  const width = bmp.readUInt32LE(18), height = bmp.readUInt32LE(22), offset = bmp.readUInt32LE(10);
  assert.equal(width, 240); assert.equal(height, 135);
  const pixel = (x, y) => [...bmp.subarray(offset + ((height - 1 - y) * width + x) * 3, offset + ((height - 1 - y) * width + x) * 3 + 3)];
  assert.notDeepEqual(pixel(215, 4), pixel(214, 4), 'Battery outline missing');
  assert.notDeepEqual(pixel(233, 7), pixel(235, 7), 'Battery terminal missing');
  await writeFile(`build/screenshots/${name}.bmp`, bmp);
}
const before = await until(s => s.version === (process.env.CARDTUNES_TEST_VERSION || '0.3.3') && s.state !== 'loading');
assert.equal(before.game.id, 'none'); assert.equal(before.scan.active, false);
assert(before.tracks >= Number(process.argv[2] || 800));
const started = Date.now(); await control('rescan');
assert(Date.now() - started < 2500, 'Rescan must acknowledge promptly');
await until(s => s.scan.active && s.scan.scanned >= 5);
assert.match((await control('rescan', '', 400)).error, /scan/i);
assert.match((await control('game', 'blocks', 400)).error, /scan/i);
assert.match((await api('/api/playlists', {action: 'create', value: 'Should not be created'}, 400)).error, /scan/i);
await screenshot('device-scanning');
await api('/api/input', {key: 'escape'});
const cancelled = await status();
assert.equal(cancelled.scan.active, false); assert.equal(cancelled.scan.error, 'Scan cancelled');
assert.equal(cancelled.tracks, before.tracks); assert(cancelled.uptime_ms > before.uptime_ms);
console.log(JSON.stringify({cancel: 'passed', previous_library_preserved: cancelled.tracks}));

await control('rescan');
let last = 0, lastReport = 0, completed, maxResponse = 0;
const end = Date.now() + 15 * 60000;
while (Date.now() < end) {
  const start = Date.now(), s = await status(); maxResponse = Math.max(maxResponse, Date.now() - start);
  assert(s.uptime_ms >= before.uptime_ms, 'Unexpected reboot during scan');
  assert(s.scan.scanned >= last); last = s.scan.scanned;
  if (!s.scan.active) { completed = s; break; }
  assert.equal(s.tracks, before.tracks, 'Do not expose a half-built index');
  if (Date.now() - lastReport >= 15000) {
    const page = await api('/api/library?limit=64&offset=200');
    assert.equal(page.tracks.length, 8, 'Large requests must return memory-bounded pages');
    console.log(JSON.stringify({scanned: last, elapsed_ms: s.scan.elapsed_ms, heap_free: s.heap_free, heap_min: s.heap_min})); lastReport = Date.now();
  }
  await wait(1000);
}
assert(completed, 'Full scan timed out');
assert.equal(completed.scan.succeeded, true, completed.scan.error);
assert.equal(completed.tracks, before.tracks); assert.equal(completed.scan.skipped, 0);
await screenshot('device-battery');
console.log(JSON.stringify({full_scan: 'passed', tracks: completed.tracks, elapsed_ms: completed.scan.elapsed_ms, max_status_response_ms: maxResponse}));

try {
  await control('volume', 0);
  const tracks = await api('/api/library?offset=1&limit=4');
  const song = tracks.tracks.find(t => t.path !== '/@demo.mp3'); assert(song);
  await control('play-library', song.id);
  const playing = await until(s => s.state === 'playing' && s.track?.path === song.path);
  await wait(1500); assert((await status()).position_ms > playing.position_ms);
  await control('pause');
  console.log(JSON.stringify({sd_playback: 'passed', track: song.title}));
} finally {
  if (before.track?.id >= 0) {
    const matches = await api(`/api/library?q=${encodeURIComponent(before.track.title.slice(0, 60))}`);
    const saved = matches.tracks.find(t => t.path === before.track.path);
    if (saved) {
      if (before.active_playlist_id) await api('/api/control', {action: 'playlist', value: String(before.active_playlist_id), entry: String(before.playlist_position)}, 202);
      else await control('play-library', saved.id);
      await until(s => s.state === 'playing');
      await control('seek', Math.floor(before.position_ms / 1000));
      if (before.state !== 'playing') await control('pause');
    } else await control('stop');
  } else await control('stop');
  await control('volume', before.volume);
}
