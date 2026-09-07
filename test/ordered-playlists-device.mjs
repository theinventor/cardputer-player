import assert from 'node:assert/strict';
import {readFile, readdir, writeFile, mkdir} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';

assert(process.argv[2], 'Pass a folder of already-imported JSONL definitions; no music is uploaded');
const directory = process.platform === 'darwin' ? join(homedir(), 'Library/Application Support') : (process.env.XDG_CONFIG_HOME || join(homedir(), '.config'));
const config = JSON.parse(await readFile(join(directory, 'cardtunes/config.json'), 'utf8'));
const headers = {Authorization: `Bearer ${config.token}`};
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function api(path, form, expected = 200) {
  const response = await fetch(config.url + path, {headers, method: form ? 'POST' : 'GET',
    body: form ? new URLSearchParams(form) : undefined, signal: AbortSignal.timeout(60000)});
  const data = await response.json();
  assert.equal(response.status, expected, `${path}: ${JSON.stringify(data)}`);
  return data;
}
const status = () => api('/api/status');
const control = (action, value = '') => api('/api/control', {action, value: String(value)}, 202);
async function until(predicate) {
  const end = Date.now() + 60000;
  while (Date.now() < end) { const s = await status(); if (predicate(s)) return s; await wait(400); }
  throw Error('Playback did not reach expected state');
}
const before = await until(s => s.version === '0.3.3' && s.state !== 'loading');
assert.equal(before.scan.active, false); assert.equal(before.game.id, 'none');
const lists = (await api('/api/playlists')).playlists;
const reports = []; let repeated;
for (const file of (await readdir(process.argv[2])).filter(f => f.endsWith('.jsonl'))) {
  const [header, ...paths] = (await readFile(join(process.argv[2], file), 'utf8')).trimEnd().split('\n').map(line => JSON.parse(line));
  const list = lists.find(p => p.name === header.name); assert(list, `Missing list ${header.name}`);
  const tracks = [], requests = []; let offset = 0, page;
  do {
    const start = Date.now(); page = await api(`/api/playlists?id=${list.id}&offset=${offset}`);
    requests.push(Date.now() - start); tracks.push(...page.tracks);
    assert(page.next_offset < 0 || page.next_offset > offset); offset = page.next_offset;
  } while (offset >= 0);
  assert.equal(page.count, header.count); assert.equal(tracks.length, paths.length);
  tracks.forEach((track, position) => { assert.equal(track.position, position); assert.equal(track.path, paths[position], `${header.name} position ${position}`); });
  reports.push({id: list.id, name: list.name, entries: page.count, available: page.available,
    first_page_ms: requests[0], max_later_page_ms: Math.max(0, ...requests.slice(1)), missing: tracks.filter(t => t.missing).map(t => t.title)});
  console.log(JSON.stringify(reports.at(-1)));
  if (!repeated) {
    const position = tracks.findIndex((t, i) => !t.missing && tracks.slice(0, i).some(prior => prior.id === t.id) && tracks.slice(i + 1).some(next => !next.missing));
    if (position >= 0) repeated = {list, tracks, position};
  }
}
assert(repeated, 'Expected a Spotify playlist with repeated entries');
try {
  await control('volume', 0); await control('shuffle', 'off');
  const {list, tracks, position} = repeated;
  await api('/api/control', {action: 'playlist', value: String(list.id), entry: String(position)}, 202);
  const selected = await until(s => s.state === 'playing' && s.playlist_position === position);
  assert.equal(selected.track.path, tracks[position].path);
  await wait(1100); assert((await status()).position_ms > selected.position_ms);
  const next = tracks.slice(position + 1).find(t => !t.missing);
  await control('next'); const advanced = await until(s => s.state === 'playing' && s.playlist_position === next.position);
  assert.equal(advanced.track.path, next.path);
  await control('seek', 0); await control('previous');
  await until(s => s.state === 'playing' && s.playlist_position === position);
  const invalid = new FormData(); invalid.append('file', new Blob(['invalid\n']), 'bad.jsonl');
  const response = await fetch(`${config.url}/api/playlist-import?size=8&id=${list.id}`, {headers, method: 'POST', body: invalid, signal: AbortSignal.timeout(15000)});
  assert.equal(response.status, 409, 'Active playlist replacement must be rejected');
  const after = await status(); assert(after.uptime_ms > before.uptime_ms, 'Unexpected reboot');
  reports.push({repeated_occurrence_playback: 'passed', next_previous: 'passed', active_import_guard: 'passed', heap_free: after.heap_free});
} finally {
  if (before.track?.id >= 0) {
    await api('/api/control', {action: 'playlist', value: String(before.active_playlist_id || 'all'), entry: String(before.playlist_position)}, 202);
    await until(s => s.state === 'playing'); await control('seek', Math.floor(before.position_ms / 1000));
    if (before.state !== 'playing') await control('pause');
  } else await control('stop');
  await control('volume', before.volume);
  await control('shuffle', before.shuffle ? 'on' : 'off');
}
await mkdir('build', {recursive: true});
await writeFile('build/ordered-playlists-acceptance.json', JSON.stringify(reports, null, 2) + '\n');
console.log('All imported paths and positions verified; duplicate occurrence playback passed.');
