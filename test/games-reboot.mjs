// Explicit OTA/reboot acceptance: requires a firmware path and an idle, paused player.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const firmware=process.argv[2];if(!firmware)throw Error('Pass the tested firmware .bin to reflash');
const dir=process.platform==='darwin'?join(homedir(),'Library/Application Support'):(process.env.XDG_CONFIG_HOME||join(homedir(),'.config'));
const config=JSON.parse(await readFile(join(dir,'cardtunes/config.json'),'utf8'));
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function api(path,form){const r=await fetch(config.url+path,{method:form?'POST':'GET',headers:{Authorization:`Bearer ${config.token}`,Connection:'close'},body:form?new URLSearchParams(form):undefined,signal:AbortSignal.timeout(10000)});assert(r.ok,`${path}: ${r.status}`);return r.json();}
const control=(action,value='')=>api('/api/control',{action,value});
const status=()=>api('/api/status');
const before=await status();assert.equal(before.state,'paused');assert.equal(before.game.id,'none');
const games={};
await api('/api/input',{key:'escape'});
try{
 for(const id of ['blocks','breakout','2048']){
  await control('game',id);let s=await status();if(!s.game.paused)await control('game-key','pause');
  games[id]=(await status()).game;await control('game-exit');assert.equal((await status()).game.save_error,'');
 }
 const upload=await promisify(execFile)('build/cardtunes',['firmware',firmware],{timeout:300000});console.log(upload.stdout.trim());
 let rebooted;
 for(let i=0;i<30;i++){
  try{const s=await status();if(s.uptime_ms<before.uptime_ms&&s.state==='paused'){rebooted=s;break;}}catch(error){if(i===29)throw error;}
  await wait(1000);
 }
 assert(rebooted,'Reboot must complete');assert.equal(rebooted.version,'0.3.0');assert.equal(rebooted.game.save_error,'');
 assert.equal(rebooted.tracks,before.tracks);assert.equal(rebooted.volume,before.volume);assert.equal(rebooted.track?.path,before.track?.path);assert.equal(rebooted.active_playlist_id,before.active_playlist_id);
 await api('/api/input',{key:'escape'});
 for(const id of Object.keys(games)){
  await control('game',id);const s=await status();assert.equal(s.game.score,games[id].score);assert.equal(s.game.best,games[id].best);await control('game-exit');
 }
 console.log(JSON.stringify({reboot_game_saves:'passed',scores:Object.fromEntries(Object.entries(games).map(([id,g])=>[id,g.score])),version:rebooted.version,tracks:rebooted.tracks,heap_free:rebooted.heap_free,heap_min:rebooted.heap_min}));
}finally{await control('game-exit');await api('/api/input',{key:'escape'});}
