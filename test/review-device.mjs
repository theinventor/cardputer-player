import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';

const directory = process.platform === 'darwin' ? join(homedir(), 'Library/Application Support') : (process.env.XDG_CONFIG_HOME || join(homedir(), '.config'));
const config = JSON.parse(await readFile(join(directory, 'cardtunes/config.json'), 'utf8'));
const headers = {Authorization: `Bearer ${config.token}`};
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function api(path, form, expected = 200) {
  const response = await fetch(config.url + path, {headers, method: form ? 'POST' : 'GET',
    body: form ? new URLSearchParams(form) : undefined, signal: AbortSignal.timeout(60000)});
  const data = await response.json(); assert.equal(response.status, expected, JSON.stringify(data)); return data;
}
const status = () => api('/api/status');
const control = (action, value = '') => api('/api/control', {action, value: String(value)}, 202);
async function until(predicate) {
  for (let i = 0; i < 100; ++i) { const s = await status(); if (predicate(s)) return s; await wait(300); }
  throw Error('Device state timed out');
}
async function upload(path, bytes, expected) {
  const body = new FormData(); body.append('file', new Blob([bytes]), 'test.jsonl');
  const response = await fetch(config.url + path, {headers, method: 'POST', body, signal: AbortSignal.timeout(300000)});
  const data = await response.json(); assert.equal(response.status, expected, JSON.stringify(data)); return data;
}
async function pages(path) {
  const tracks = []; let offset = 0;
  do {
    const page = await api(`${path}${path.includes('?') ? '&' : '?'}offset=${offset}`);
    assert(page.tracks.length <= 16); tracks.push(...page.tracks);
    assert(page.next_offset < 0 || page.next_offset > offset); offset = page.next_offset;
  } while (offset >= 0);
  return tracks;
}
const before = await status();
assert.equal(before.scan.active, false); assert.equal(before.game.id, 'none');
assert.equal(before.queue_count, 0, 'Run with an empty queue');
let temporary;
try {
  await control('volume', 0); await control('shuffle', 'off');
  const songs = (await api('/api/library?offset=1&limit=4')).tracks;
  assert(songs.length >= 3);
  await control('playlist', 'all');
  for (let i = 0; i < 64; ++i) await control('enqueue', songs[i % 3].id);
  const queued = await pages('/api/queue'); assert.equal(queued.length, 64);
  queued.forEach((song, i) => assert.equal(song.id, songs[i % 3].id));
  console.log(JSON.stringify({stage: 'queue', heap_min: (await status()).heap_min}));
  await control('clear-queue');
  const malformed = '{"version":3,"extra":' + '['.repeat(150) + '0' + ']'.repeat(150) + '}\n';
  await upload(`/api/playlist-import?size=${Buffer.byteLength(malformed)}`, malformed, 400);
  const body = JSON.stringify({version: 3, name: 'Cardtunes review test', count: 4}) + '\n' +
    [songs[0], songs[1], songs[0], songs[2]].map(song => JSON.stringify(song.path)).join('\n') + '\n';
  temporary = (await upload(`/api/playlist-import?size=${Buffer.byteLength(body)}`, body, 201)).id;
  await api('/api/control', {action: 'playlist', value: String(temporary), entry: '2'}, 202);
  await until(s => s.state === 'playing' && s.playlist_position === 2);
  await api('/api/playlists', {action: 'remove', id: String(temporary), value: '1'});
  assert.equal((await status()).playlist_position, 1);
  await control('next'); await until(s => s.state === 'playing' && s.track.id === songs[2].id);
  await control('play-entry', 1); await until(s => s.state === 'playing' && s.playlist_position === 1);
  await api('/api/playlists', {action: 'move', id: String(temporary), value: '1', to: '0'});
  assert.equal((await status()).playlist_position, 0);
  await api('/api/playlists', {action: 'remove', id: String(temporary), value: '0'});
  await until(s => s.state === 'stopped' && s.playlist_position === -1);
  console.log(JSON.stringify({stage: 'edits', heap_min: (await status()).heap_min}));
  console.log('64-entry paginated queue, deep JSON rejection, and repeated-entry edits passed.');

  await control('playlist', 'all');
  const large = JSON.stringify({version: 3, name: 'Cardtunes review test', count: 1000}) + '\n' +
    (JSON.stringify(songs[0].path) + '\n').repeat(1000);
  await upload(`/api/playlist-import?size=${Buffer.byteLength(large)}&id=${temporary}`, large, 201);
  const largePage = await api(`/api/playlists?id=${temporary}&offset=992`);
  assert.equal(largePage.count, 1000); assert.equal(largePage.available, 1000);
  assert.equal(largePage.next_offset, -1); assert.equal(largePage.tracks.at(-1).position, 999);
  await api('/api/control', {action: 'playlist', value: String(temporary), entry: '999'}, 202);
  await until(s => s.state === 'playing' && s.playlist_position === 999);
  await api(`/api/playlists?id=${temporary}&offset=0`);
  console.log(JSON.stringify({stage: '1000-entry-playlist', heap_min: (await status()).heap_min}));

  if (process.argv[2]) {
    const [, , file, destination, playlist] = process.argv; assert(file && destination && playlist);
    const entries = await pages(`/api/playlists?id=${playlist}`);
    const target = entries.find(t => t.path === destination); assert(target?.missing, 'Upload test requires a missing playlist entry');
    console.log(JSON.stringify({stage: 'playlist-pages', heap_min: (await status()).heap_min}));
    await control('playlist', playlist); await until(s => s.state === 'playing');
    const prior = await status(); await control('enqueue', prior.track.id);
    const bytes = await readFile(file);
    await upload(`/api/upload?size=${bytes.length}&path=${encodeURIComponent(destination)}`, bytes, 201);
    const refreshed = await status(); assert.equal(refreshed.playlist_position, prior.playlist_position);
    console.log(JSON.stringify({stage: 'uploaded', heap_min: refreshed.heap_min}));
    assert.equal(refreshed.queue_count, 1, 'Upload must retain the queue');
    await control('play-entry', target.position);
    await until(s => s.state === 'playing' && s.track.path === destination);
    console.log('Uploading a missing song refreshes the active playlist immediately and retains the queue.');
  }
  const after = await status(); assert(after.uptime_ms > before.uptime_ms, 'Unexpected reboot');
  console.log(JSON.stringify({heap_free: after.heap_free, heap_min: after.heap_min, uptime_ms: after.uptime_ms}));
} finally {
  await control('clear-queue');
  if (temporary) await api('/api/playlists', {action: 'delete', id: String(temporary)});
  if (before.track?.id >= 0) {
    await api('/api/control', {action: 'playlist', value: String(before.active_playlist_id || 'all'), entry: String(before.playlist_position)}, 202);
    await until(s => s.state === 'playing'); await control('seek', Math.floor(before.position_ms / 1000));
    if (before.state !== 'playing') await control('pause');
  } else await control('stop');
  await control('volume', before.volume); await control('shuffle', before.shuffle ? 'on' : 'off');
}
