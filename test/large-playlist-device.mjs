import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';

const directory = process.platform === 'darwin' ? join(homedir(), 'Library/Application Support') : (process.env.XDG_CONFIG_HOME || join(homedir(), '.config'));
const config = JSON.parse(await readFile(join(directory, 'cardtunes/config.json'), 'utf8'));
async function api(path, form, expected = 200) {
  const response = await fetch(config.url + path, {
    method: form ? 'POST' : 'GET', headers: {Authorization: `Bearer ${config.token}`},
    body: form ? new URLSearchParams(form) : undefined, signal: AbortSignal.timeout(60000)
  });
  const data = await response.json();
  assert.equal(response.status, expected, `${path}: ${JSON.stringify(data)}`);
  return data;
}
const status = () => api('/api/status');
const edit = (action, value = '', to = '', expected = 200) => api('/api/playlists', {action, id: '16', value: String(value), to: String(to)}, expected);
const control = (action, value = '') => api('/api/control', {action, value: String(value)}, 202);
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(predicate) {
  const end = Date.now() + 30000;
  while (Date.now() < end) { const s = await status(); if (predicate(s)) return s; await wait(300); }
  throw Error('Playback did not reach expected state');
}
const before = await status();
assert.equal(before.playlist_track_limit, 1000);
assert.equal(before.library_track_limit, 10000);
assert.equal(before.game.id, 'none');
const lists = await api('/api/playlists');
assert.equal(lists.playlists.find(p => p.id === 16)?.name, 'Cardtunes capacity test');
const start = Date.now();
const page = await api('/api/playlists?id=16&offset=0&limit=32');
assert.equal(page.count, 1000); assert.equal(page.tracks.length, 16); assert.equal(page.next_offset, 16);
const last = await api('/api/playlists?id=16&offset=992');
assert.equal(last.tracks.length, 8); assert.equal(last.next_offset, -1);
assert.equal(last.tracks[7].position, 999); assert.equal(last.tracks[7].path, '/@demo.mp3');
assert.equal(last.available, 1);
console.log(JSON.stringify({capacity: 1000, first_and_last_page: 'passed', page_check_ms: Date.now() - start, heap_free: (await status()).heap_free}));

const library = await api('/api/library');
const sd = library.tracks.filter(t => t.path.startsWith('/Music/'));
assert.ok(sd.length >= 2);
await edit('move', 999, 0);
assert.equal((await api('/api/playlists?id=16&offset=0')).tracks[0].path, '/@demo.mp3');
await edit('move', 0, 999);
await edit('remove', 998);
await edit('add', sd[0].id);
assert.equal((await api('/api/playlists?id=16&offset=992')).count, 1000);
assert.match((await edit('add', sd[1].id, '', 400)).error, /limit/i);
assert.match((await edit('add', sd[0].id, '', 400)).error, /already/i);
await edit('rename', 'Cardtunes capacity verified');
console.log(JSON.stringify({large_edits: 'passed', overflow_and_duplicates: 'rejected', heap_free: (await status()).heap_free}));

try {
  await control('volume', 0);
  await control('playlist', 16);
  const playing = await until(s => s.state === 'playing');
  assert.equal(playing.active_playlist_id, 16); assert.equal(playing.playlist_tracks, 2);
  await wait(1200);
  const progressed = await status(); assert.ok(progressed.position_ms > playing.position_ms);
  await control('pause');
  console.log(JSON.stringify({large_playlist_playback: 'passed', heap_free: progressed.heap_free, heap_min: progressed.heap_min}));
} finally {
  if (before.track?.id >= 0) {
    if (before.active_playlist_id) await api('/api/control', {action: 'playlist', value: String(before.active_playlist_id), track: String(before.track.id)}, 202);
    else await control('play-library', before.track.id);
    await until(s => s.state === 'playing');
    await control('seek', Math.floor(before.position_ms / 1000));
    if (before.state !== 'playing') await control('pause');
  } else { await control('playlist', 'all'); await control('stop'); }
  await control('volume', before.volume);
}
console.log('Install normal firmware next, verify slot 16 survived reboot, then delete the fixture.');
