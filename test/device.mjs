import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';

const configDir = process.platform === 'darwin' ? join(homedir(), 'Library/Application Support') : (process.env.XDG_CONFIG_HOME || join(homedir(), '.config'));
const config = JSON.parse(await readFile(join(configDir, 'cardtunes/config.json'), 'utf8'));
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function request(path, form) {
  const response = await fetch(config.url + path, {
    method: form ? 'POST' : 'GET', headers: {Authorization: `Bearer ${config.token}`},
    body: form ? new URLSearchParams(form) : undefined, signal: AbortSignal.timeout(15000)
  });
  assert.ok(response.ok, `${path}: ${response.status} ${response.ok ? '' : await response.text()}`);
  return response.json();
}
const key = value => request('/api/input', {key: value});
const control = (action, value = '') => request('/api/control', {action, value: String(value)});
let before = await request('/api/status');
assert.equal((await fetch(config.url + '/api/status')).status, 401);
assert.equal((await request('/api/library')).tracks.length > 0, true);
await control('play');
await wait(1000);
await key('escape');
await key(' ');
await wait(300);
assert.equal((await request('/api/status')).state, 'paused');
await key(' ');
await wait(300);
assert.equal((await request('/api/status')).state, 'playing');
await key(']');
assert.equal((await request('/api/status')).volume, Math.min(100, before.volume + 5));
await control('volume', before.volume);
await key('tab');
await mkdir('artifacts', {recursive: true});
let shot = await fetch(config.url + '/api/screen.bmp', {headers: {Authorization: `Bearer ${config.token}`}});
let bmp = Buffer.from(await shot.arrayBuffer());
assert.equal(bmp.length, 54 + 240 * 135 * 3);
await writeFile('artifacts/device-library.bmp', bmp);
await key('escape');
console.log('Wi-Fi authentication, playback, volume, library, and screenshot checks passed. Waiting for screen sleep.');
await wait(65000);
const idle = await request('/api/status');
await key(' ');
await wait(300);
const after = await request('/api/status');
assert.equal(after.state, 'paused', 'First key after screen sleep must perform its action');
assert.ok(after.uptime_ms > idle.uptime_ms, 'Device must not reboot');
console.log(JSON.stringify({screen_sleep_key: 'passed', loop_gap_ms: idle.loop_gap_ms, keyboard_events: after.keyboard_events, heap_free: after.heap_free}));
await control('play');
