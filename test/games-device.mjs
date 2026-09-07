// Explicit hardware acceptance test: changes playback briefly, restores it in finally.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {homedir} from 'node:os';
import {join} from 'node:path';
const dir=process.platform==='darwin'?join(homedir(),'Library/Application Support'):(process.env.XDG_CONFIG_HOME||join(homedir(),'.config'));
const config=JSON.parse(await readFile(join(dir,'cardtunes/config.json'),'utf8'));
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function raw(path,form){return fetch(config.url+path,{method:form?'POST':'GET',headers:{Authorization:`Bearer ${config.token}`},body:form?new URLSearchParams(form):undefined,signal:AbortSignal.timeout(15000)});}
async function api(path,form){const r=await raw(path,form);assert(r.ok,`${path}: ${r.status} ${r.ok?'':await r.text()}`);return r.json();}
const control=(action,value='')=>api('/api/control',{action,value:String(value)});
const key=key=>api('/api/input',{key});
const status=()=>api('/api/status');
async function until(test){for(let i=0;i<60;i++){const s=await status();if(test(s))return s;await wait(250);}throw Error('Device state timeout');}
async function shot(name){const r=await raw('/api/screen.bmp');assert(r.ok);const bmp=Buffer.from(await r.arrayBuffer());assert.equal(bmp.length,54+240*135*3);const colors=new Map();for(let i=54;i<bmp.length;i+=3){const c=bmp.readUIntBE(i,3);colors.set(c,(colors.get(c)||0)+1);}await writeFile(`artifacts/games-${name}.bmp`,bmp);assert(colors.size>=3 && 240*135-Math.max(...colors.values())>200,'Device canvas must be nonblank');return bmp;}
const before=await status();
assert.equal(before.version,process.env.CARDTUNES_TEST_VERSION||'0.3.3');assert.equal(before.game.id,'none');assert(before.sd_mounted);
const queueBefore=(await api('/api/queue')).tracks;
let testList=0;
await mkdir('artifacts',{recursive:true});
try{
 await control('volume',10);await control('pause');await until(s=>s.state!=='playing'&&s.state!=='loading');
 await key('escape');await key('tab');await key('tab');await key('tab');await shot('menu');
 for(const id of ['blocks','breakout','2048']){
  await control('game',id);await key('p');await key('p');
  const started=await status();assert.equal(started.game.id,id);assert.equal(started.state,'paused');
  for(const action of ['play','toggle','next','rescan','playlist'])assert.equal((await raw('/api/control',{action,value:action==='playlist'?'all':''})).status,400);
  assert.equal((await raw('/api/playlists',{action:'create',value:'Blocked game edit'})).status,400);
  const a=await shot(id+'-before');
  await key(id==='2048'?'.':' ');await wait(250);const b=await shot(id);assert(!a.equals(b),`${id} device input must change screen`);
  await key('p');await until(s=>s.game.paused);const paused=await shot(id+'-paused');await wait(300);assert(paused.equals(await shot(id+'-paused-check')));
  await key('lock');await control('game-key','pause');await wait(100);assert.equal((await status()).game.paused,true,'Locked game cannot run');await key('lock');
  const savedScore=(await status()).game.score;
  await key('`');let closed=await until(s=>s.game.id==='none');assert.equal(closed.state,'paused');assert.equal(closed.game.save_error,'');
  await control('game',id);assert.equal((await status()).game.score,savedScore);await control('game-exit');
 }
 const library=await api('/api/library');const sd=library.tracks.filter(t=>t.path.startsWith('/Music/')).slice(0,2);assert.equal(sd.length,2);
 testList=(await api('/api/playlists',{action:'create',value:`Game QA ${Date.now()}`})).id;
 for(const t of sd)await api('/api/playlists',{action:'add',id:String(testList),value:String(t.id)});
 await control('playlist',testList);await until(s=>s.state==='playing'&&s.active_playlist_id===testList);
 await control('enqueue',sd[1].id);await control('seek',15);const playing=await until(s=>s.state==='playing'&&s.position_ms>=15000);
 await control('game','2048');const inGame=await until(s=>s.state==='paused');await wait(400);const held=await status();
 assert.equal(held.position_ms,inGame.position_ms);assert.equal(held.active_playlist_id,testList);assert.equal(held.queue_count,1);
 await control('game-exit');const resumed=await until(s=>s.state==='playing');assert.equal(resumed.track.id,playing.track.id);assert.equal(resumed.active_playlist_id,testList);assert.equal(resumed.queue_count,1);assert(resumed.position_ms>=inGame.position_ms);
 const detail=await api(`/api/playlists?id=${testList}`);assert.equal(detail.count,2);
 await control('pause');await until(s=>s.state==='paused');
 console.log(JSON.stringify({games:'3 real game screens + input/pause/exit',save_error:held.game.save_error,playlist_resume:'passed',queue_preserved:true,heap_free:held.heap_free,heap_min:held.heap_min,uptime_ms:held.uptime_ms}));
}finally{
 await control('game-exit');await control('pause');
 if(testList)await api('/api/playlists',{action:'delete',id:String(testList)});
 await control('playlist',before.active_playlist_id||'all');await control('pause');
 if(before.track){await control('play',before.track.id);await until(s=>s.state==='playing');await control('seek',Math.floor(before.position_ms/1000));await control('pause');await until(s=>s.state==='paused');}
 await control('clear-queue');for(const t of queueBefore)await control('enqueue',t.id);
 await control('volume',before.volume);await key('escape');
 if(before.state==='playing')await control('play');
 console.log('Original music selection, queue, volume and playback state restored.');
}
