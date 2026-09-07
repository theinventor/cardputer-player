import assert from 'node:assert/strict';
import { mkdirSync } from 'node:fs';
import { createPreviewServer } from './web-preview.mjs';
const {chromium} = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const mock=createPreviewServer();
await new Promise(resolve=>mock.server.listen(0,'127.0.0.1',resolve));
const origin=`http://127.0.0.1:${mock.server.address().port}`;
const browser=await chromium.launch({headless:true});
mkdirSync('build/screenshots',{recursive:true});
let page;
try {
  page=await browser.newPage({viewport:{width:1280,height:900}});
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.goto(origin);
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===4);
  await page.getByRole('button',{name:'New playlist',exact:true}).click();
  await page.locator('#playlistName').fill('Weekend & friends');
  await page.getByRole('button',{name:'Save',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#libraryTitle').textContent==='Weekend & friends');
  const id=Number(await page.locator('#playlistPicker').inputValue());
  assert.equal(mock.lists.get(id).name,'Weekend & friends');
  assert.equal(await page.locator('#empty').innerText(),'Playlist is empty');
  await page.getByRole('button',{name:'Play selected list',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#notice').textContent==='Playlist has no available tracks');
  assert.equal(mock.state.active_playlist_id,1);
  await page.locator('#playlistPicker').selectOption('0');
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===4);
  for(const title of ['Morning Drive','Desert Radio','Pacific Coast']) {
    await page.getByRole('button',{name:`Add ${title} to playlist`,exact:true}).click();
    await page.locator('#playlistDestination').selectOption(String(id));
    await page.getByRole('button',{name:'Add',exact:true}).click();
    await page.waitForFunction(()=>!document.querySelector('#playlistDialog').open);
    await page.waitForFunction(()=>!document.querySelector('#savePlaylist').disabled);
  }
  assert.deepEqual(mock.lists.get(id).ids,[0,1,2]);
  await page.locator('#playlistPicker').selectOption(String(id));
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===3);
  assert.deepEqual(await page.locator('#tracks .name').allTextContents(),['Morning Drive','Desert Radio','Pacific Coast']);
  const controlsBefore=mock.requests.filter(r=>r.path==='/api/control').length;
  await page.getByRole('button',{name:'Play Pacific Coast',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#title').textContent==='Pacific Coast'&&document.querySelector('#playbackScope').textContent.includes('Weekend & friends'));
  assert.equal(mock.requests.filter(r=>r.path==='/api/control').length,controlsBefore+1,'Switch scope and play chosen track in one request');
  assert.equal(mock.state.track.id,2);
  assert.equal(mock.state.playlist_position,2);
  mock.lists.get(id).ids.push(2);
  await page.locator('#playlistPicker').selectOption('0');
  await page.locator('#playlistPicker').selectOption(String(id));
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===4);
  await page.getByRole('button',{name:'Play Pacific Coast',exact:true}).nth(1).click();
  await page.waitForFunction(()=>document.querySelector('#tracks li.current')?.dataset.position==='3');
  assert.equal(mock.state.playlist_position,3); assert.equal(mock.state.track.id,2);
  assert.equal(await page.locator('#tracks li.current').count(),1);
  assert.equal(mock.requests.filter(r=>r.path==='/api/control').at(-1).params.action,'play-entry');
  await page.getByRole('button',{name:'Remove Pacific Coast',exact:true}).nth(1).click();
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===3);
  await page.getByRole('button',{name:'Move Pacific Coast up',exact:true}).click();
  await page.waitForFunction(()=>document.querySelectorAll('#tracks .name')[1]?.textContent==='Pacific Coast');
  assert.deepEqual(mock.lists.get(id).ids,[0,2,1]);
  await page.getByRole('button',{name:'Remove Desert Radio',exact:true}).click();
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===2);
  await page.getByRole('button',{name:'Rename playlist',exact:true}).click();
  await page.locator('#playlistName').fill('A'.repeat(63));
  await page.getByRole('button',{name:'Save',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#libraryTitle').textContent.length===63);
  await page.getByRole('button',{name:'Play selected list',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#playbackScope').textContent.includes('A'.repeat(63)));
  assert.equal(mock.state.active_playlist_id,id);
  await page.getByRole('button',{name:'Shuffle',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#shuffle').getAttribute('aria-pressed')==='true');
  await page.locator('#repeat').selectOption('off');
  await page.waitForFunction(()=>document.querySelector('#repeat').value==='off');
  await page.reload();
  await page.waitForFunction(()=>document.querySelectorAll('#playlistPicker option').length===4);
  await page.locator('#playlistPicker').selectOption(String(id));
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===2);
  for(const [name,viewport] of [['desktop',{width:1280,height:900}],['mobile',{width:390,height:844}],['narrow',{width:320,height:740}]]) {
    await page.setViewportSize(viewport);
    await page.screenshot({path:`build/screenshots/playlists-${name}.png`,fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,`${name} horizontal overflow`);
    const overlaps=await page.locator('#tracks li').evaluateAll(rows=>rows.flatMap(row=>{
      const boxes=[...row.children].map(el=>({name:el.className,r:el.getBoundingClientRect()}));
      return boxes.flatMap((a,i)=>boxes.slice(i+1).filter(b=>Math.min(a.r.right,b.r.right)-Math.max(a.r.left,b.r.left)>1&&Math.min(a.r.bottom,b.r.bottom)-Math.max(a.r.top,b.r.top)>1).map(b=>`${a.name} overlaps ${b.name}`));
    }));assert.deepEqual(overlaps,[],`${name} row overlap`);
    const pixels=await page.locator('#spectrum').evaluate(canvas=>[...canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data].filter((v,i)=>i%4===3&&v>0).length);
    assert(pixels>100,'Spectrum must render');
  }
  await page.locator('#playlistPicker').selectOption('2');
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===2&&document.querySelector('#libraryTitle').textContent==='Evening');
  assert.equal(await page.getByRole('button',{name:'Play Missing Song.mp3',exact:true}).isDisabled(),true);
  assert.equal(await page.locator('#playlistCount').innerText(),'2 tracks / 1 available');
  await page.getByRole('button',{name:'Rename playlist',exact:true}).click();
  await page.locator('#playlistName').fill('Failure test');
  mock.failNext('Cannot write playlist; check microSD space');
  await page.getByRole('button',{name:'Save',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#dialogError').textContent.includes('Cannot write playlist'));
  assert.equal(await page.locator('#playlistDialog').evaluate(el=>el.open),true);
  assert.equal(mock.lists.get(2).name,'Evening');
  await page.getByRole('button',{name:'Cancel',exact:true}).click();
  await page.locator('#playlistPicker').selectOption(String(id));
  await page.getByRole('button',{name:'Delete playlist',exact:true}).click();
  await page.getByRole('button',{name:'Delete',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#playlistPicker').value==='0');
  assert.equal(mock.lists.has(id),false);assert.equal(mock.tracks.length,4);
  await page.getByRole('button',{name:'Rescan microSD',exact:true}).click();
  await page.waitForFunction(()=>!document.querySelector('#scanProgress').hidden);
  assert(await page.locator('#rescan').isDisabled());
  assert(await page.locator('#addFiles').isDisabled());
  assert(await page.getByRole('button',{name:'Play Morning Drive',exact:true}).isDisabled());
  mock.state.scan.scanned=740;mock.state.scan.elapsed_ms=25000;
  await page.waitForFunction(()=>document.querySelector('#scanText').textContent==='Scanning: 740 tracks / 25s');
  await page.screenshot({path:'build/screenshots/scan-mobile.png',fullPage:true});
  await page.getByRole('button',{name:'Cancel scan',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#scanProgress').hidden&&document.querySelector('#notice').textContent==='Scan cancelled');
  assert(!await page.locator('#rescan').isDisabled());
  await page.getByRole('button',{name:'Rescan microSD',exact:true}).click();
  await page.waitForFunction(()=>!document.querySelector('#scanProgress').hidden);
  const readsBefore=mock.requests.filter(r=>r.path==='/api/library').length;
  mock.state.scan.active=false;mock.state.scan.succeeded=true;mock.state.scan.error='';
  await page.waitForFunction(()=>document.querySelector('#scanProgress').hidden&&document.querySelector('#notice').textContent==='Library updated');
  assert(mock.requests.filter(r=>r.path==='/api/library').length>readsBefore);
  assert(!await page.getByRole('button',{name:'Play Morning Drive',exact:true}).isDisabled());
  assert.equal(mock.state.active_playlist_id,0);

  for(let id=4;id<33;id++)mock.tracks.push({...mock.tracks[0],id,title:`Page track ${id}`});
  mock.state.tracks=mock.tracks.length;
  const capped=await page.evaluate(()=>request('/api/library?limit=64'));
  assert.equal(capped.tracks.length,8);assert.equal(capped.next_offset,8);
  await page.evaluate(()=>loadLibrary());
  for(const count of [8,16,24,32]){
    assert.equal(await page.locator('#tracks li').count(),count);
    await page.locator('#more').click();
    await page.waitForFunction(count=>document.querySelectorAll('#tracks li').length>count,count);
  }
  assert.equal(await page.locator('#tracks li').count(),33);
  assert(await page.locator('#more').isHidden());
  await page.locator('#search').fill('Page track');
  await page.waitForFunction(()=>document.querySelector('#tracks .name')?.textContent==='Page track 4');
  await page.locator('#more').click();
  await page.waitForFunction(()=>document.querySelectorAll('#tracks li').length===16);
  assert.deepEqual(await page.locator('#tracks .name').allTextContents(),Array.from({length:16},(_,i)=>`Page track ${i+4}`));
  mock.tracks.splice(4);mock.state.tracks=4;
  await page.evaluate(()=>{document.querySelector('#search').value='';return loadLibrary();});

  const queueTitles=()=>page.locator('#queue li').evaluateAll(rows=>rows.map(row=>row.firstChild.textContent));
  for(const total of [64,9,8,1,0]){
    const ids=Array.from({length:total},(_,i)=>[2,0,2,1][i%4]);
    mock.setQueue(ids);
    const before=mock.requests.length;
    await page.evaluate(()=>loadQueue());
    assert.deepEqual(await queueTitles(),ids.map(id=>mock.tracks[id].title),'Queue must retain order and duplicates');
    assert.equal(await page.locator('#queueEmpty').isHidden(),total>0);
    assert.deepEqual(mock.requests.slice(before).filter(r=>r.path==='/api/queue').map(r=>r.query.offset),Array.from({length:Math.max(1,Math.ceil(total/8))},(_,i)=>String(i*8)));
  }
  mock.setQueue(Array.from({length:64},(_,i)=>i%4));
  await page.evaluate(()=>loadQueue());
  await page.locator('#clearQueue').click();
  await page.waitForFunction(()=>document.querySelectorAll('#queue li').length===0);
  assert.equal(mock.state.queue_count,0);

  const queueRoute='**/api/queue*';
  for(const pageSize of [16,64]){
    let calls=0;
    const expected=Array.from({length:64},(_,i)=>mock.tracks[i%3]);
    await page.route(queueRoute,route=>{
      const offset=Number(new URL(route.request().url()).searchParams.get('offset'));calls++;
      const json={tracks:expected.slice(offset,offset+pageSize)};
      if(pageSize===16){json.total=64;json.next_offset=offset+pageSize<64?offset+pageSize:-1;}
      return route.fulfill({json});
    });
    await page.evaluate(()=>loadQueue());
    assert.deepEqual(await queueTitles(),expected.map(t=>t.title));
    assert.equal(calls,64/pageSize,pageSize===64?'Older firmware without a cursor must finish in one request':'16-track pages must aggregate');
    await page.unroute(queueRoute);
  }
  const previousQueue=await queueTitles();
  for(const [json,status,notice] of [
    [{tracks:[],next_offset:8},200,'Invalid queue cursor'],
    [{tracks:[],next_offset:-2},200,'Invalid queue cursor'],
    [{tracks:[],next_offset:64},200,'Invalid queue cursor'],
    [{tracks:[],next_offset:8.5},200,'Invalid queue cursor'],
    [{tracks:[],next_offset:'16'},200,'Invalid queue cursor'],
    [{},200,'Invalid queue page'],
    [{tracks:Array(65).fill(mock.tracks[0]),next_offset:-1},200,'Invalid queue page'],
    [{error:'Queue unavailable'},503,'Queue unavailable'],
  ]){
    let calls=0;
    await page.route(queueRoute,route=>{
      calls++;
      return route.fulfill(calls===1?{json:{tracks:[mock.tracks[0]],next_offset:8}}:{json,status});
    });
    await page.evaluate(()=>loadQueue());
    assert.equal(calls,2,'Invalid pages must not loop');
    assert.equal(await page.locator('#notice').innerText(),notice);
    assert.deepEqual(await queueTitles(),previousQueue,'Later-page failure must not replace the queue with partial data');
    await page.unroute(queueRoute);
  }
  let pending,release;
  const held=new Promise(resolve=>pending=resolve),gate=new Promise(resolve=>release=resolve);
  let calls=0;
  await page.route(queueRoute,async route=>{
    if(++calls===1){pending();await gate;await route.fulfill({json:{tracks:[mock.tracks[0]],next_offset:8}});}
    else await route.fulfill({json:{tracks:[],total:0,next_offset:-1}});
  });
  await page.evaluate(()=>{window.pendingQueue=loadQueue();});
  await held;
  await page.evaluate(()=>loadQueue());
  release();await page.evaluate(()=>window.pendingQueue);
  assert.deepEqual(await queueTitles(),[],'A stale queue request must not overwrite the latest refresh');
  assert.equal(calls,2,'A stale refresh must not fetch additional pages');
  await page.unroute(queueRoute);
  assert.deepEqual(errors,[]);
  console.log('Web playlist CRUD, library/queue pagination (8/16 tracks), legacy queue, duplicate order, failed/stale queue reads, scope, missing files, errors, and responsive screenshots passed (mock API; no device used).');
} catch(error) {
  console.error('Mock state:',mock.state);
  console.error('Recent controls:',mock.requests.filter(r=>r.path==='/api/control').slice(-8));
  if(page){
    console.error('Browser state:',await page.evaluate(()=>({state,viewedPlaylist,hidden:document.hidden,notice:document.querySelector('#notice').textContent,rows:[...document.querySelectorAll('#tracks li')].map(e=>({id:e.dataset.id,position:e.dataset.position,current:e.classList.contains('current')}))})));
    await page.screenshot({path:'build/screenshots/web-failure.png',fullPage:true});
  }
  throw error;
} finally {
  await browser.close();
  await new Promise(resolve=>mock.server.close(resolve));
}
