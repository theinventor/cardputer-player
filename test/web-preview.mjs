import http from 'node:http';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = new URL('../', import.meta.url);
export function createPreviewServer() {
  const icons = Object.fromEntries(readdirSync(new URL('web/icons/', root)).filter(f => f.endsWith('.svg')).map(f => [f.slice(0, -4), readFileSync(new URL(`web/icons/${f}`, root), 'utf8')]));
  const html = readFileSync(new URL('web/index.html', root), 'utf8').replace('/*ICONS*/ {}', JSON.stringify(icons)).replace("let key = localStorage.getItem('cardtunes.key') || ''", "let key = 'preview-only'");
  const tracks = ['Morning Drive', 'Desert Radio', 'Pacific Coast', 'Late Arrival'].map((title, id) => ({id, title, artist:'Cardtunes Test Audio', album:'Road Sessions', path:`/Music/Road Sessions/${title}.mp3`}));
  const lists = new Map([[1, {id:1, name:'Road trip', ids:[2,0,1]}], [2, {id:2, name:'Evening', ids:[3,99]}]]);
  const state = {name:'Cardtunes', version:'0.2.0', state:'paused', track:tracks[2], position_ms:64000, duration_ms:240000, volume:35, shuffle:false, repeat:'all', sd_mounted:true, tracks:tracks.length, queue_count:0, active_playlist_id:1, playlist_name:'Road trip', playlist_tracks:3, battery_percent:82, ssid:'Preview network', bands:Array.from({length:16}, (_, i) => (i*17)%90), error:''};
  let queue = [], failNext = '';
  const requests = [];
  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://preview.invalid');
    const send = (status, data) => {res.writeHead(status, {'Content-Type':'application/json', 'Cache-Control':'no-store'});res.end(JSON.stringify(data));};
    if(url.pathname === '/') {res.writeHead(200, {'Content-Type':'text/html; charset=utf-8'});res.end(html);return;}
    if(req.headers.authorization !== 'Bearer preview-only') {send(401,{error:'Preview access key required'});return;}
    let body = '';for await(const chunk of req) {body += chunk;if(body.length>8192){send(413,{error:'Too large'});return;}}
    const form = new URLSearchParams(body);
    requests.push({path:url.pathname, method:req.method, params:Object.fromEntries(form)});
    if(failNext && req.method === 'POST') {const error=failNext;failNext='';send(400,{error});return;}
    if(url.pathname === '/api/status') {send(200,state);return;}
    if(url.pathname === '/api/cover') {
      try{const art=readFileSync(new URL('data/demo.jpg',root));res.writeHead(200,{'Content-Type':'image/jpeg'});res.end(art);}catch{send(404,{error:'No artwork'});}return;
    }
    if(url.pathname === '/api/library') {const q=(url.searchParams.get('q')||'').toLowerCase();send(200,{tracks:tracks.filter(t=>JSON.stringify(t).toLowerCase().includes(q)),total:tracks.length,next_offset:-1});return;}
    if(url.pathname === '/api/queue') {send(200,{tracks:queue.map(id=>tracks[id])});return;}
    if(url.pathname === '/api/playlists') {
      if(req.method === 'GET') {
        if(!url.searchParams.has('id')){send(200,{active_playlist_id:state.active_playlist_id,playlists:[...lists.values()].map(p=>({id:p.id,name:p.name,count:p.ids.length}))});return;}
        const p=lists.get(Number(url.searchParams.get('id')));if(!p){send(404,{error:'Playlist not found'});return;}
        const offset=Number(url.searchParams.get('offset')||0), limit=2;
        send(200,{id:p.id,name:p.name,count:p.ids.length,available:p.ids.filter(id=>tracks[id]).length,tracks:p.ids.slice(offset,offset+limit).map((id,i)=>({...tracks[id],id:tracks[id]?id:null,title:tracks[id]?.title||'Missing Song.mp3',path:tracks[id]?.path||'/Music/Missing Song.mp3',missing:!tracks[id],position:offset+i})),next_offset:offset+limit<p.ids.length?offset+limit:-1});return;
      }
      const action=form.get('action'),value=form.get('value'),id=Number(form.get('id'));
      let p=lists.get(id);
      if(action === 'create'){
        if([...lists.values()].some(p=>p.name.toLowerCase()===value.toLowerCase())){send(400,{error:'A playlist already has that name'});return;}
        p={id:Math.max(0,...lists.keys())+1,name:value,ids:[]};lists.set(p.id,p);
      }else if(!p){send(404,{error:'Playlist not found'});return;}
      else if(action === 'rename')p.name=value;
      else if(action === 'delete'){lists.delete(id);if(state.active_playlist_id===id){state.active_playlist_id=0;state.playlist_name='All music';state.state='stopped';}}
      else if(action === 'add'){
        if(p.ids.includes(Number(value))){send(400,{error:'Track is already in this playlist'});return;}
        p.ids.push(Number(value));
      }else if(action === 'remove')p.ids.splice(Number(value),1);
      else if(action === 'move'){const [track]=p.ids.splice(Number(value),1);p.ids.splice(Number(form.get('to')),0,track);}
      else{send(400,{error:'Unknown playlist action'});return;}
      if(state.active_playlist_id===p.id)state.playlist_name=p.name;
      send(200,{ok:true,id:p.id});return;
    }
    if(url.pathname === '/api/control') {
      const action=form.get('action'),value=form.get('value');
      if(action === 'playlist') {
        const p=value==='all'?null:lists.get(Number(value));
        if(value!=='all'&&(!p||!p.ids.some(id=>tracks[id]))){send(400,{error:'Playlist has no available tracks'});return;}
        state.active_playlist_id=p?.id||0;state.playlist_name=p?.name||'All music';state.track=tracks[p?.ids.find(id=>tracks[id])||0];state.state='playing';queue=[];
        if(form.has('track'))state.track=tracks[Number(form.get('track'))];
      }else if(action === 'play-library'){state.active_playlist_id=0;state.playlist_name='All music';state.track=tracks[Number(value)];state.state='playing';queue=[];}
      else if(action === 'play'){state.track=tracks[Number(value)];state.state='playing';}
      else if(action === 'shuffle')state.shuffle=value==='on';
      else if(action === 'repeat')state.repeat=value;
      else if(action === 'volume')state.volume=Number(value);
      else if(action === 'enqueue'){
        if(state.active_playlist_id&&!lists.get(state.active_playlist_id)?.ids.includes(Number(value))){send(400,{error:'Queue full or track outside the active playlist'});return;}
        queue.push(Number(value));
      }else if(action === 'clear-queue'||action === 'rescan')queue=[];
      else if(action === 'toggle')state.state=state.state==='playing'?'paused':'playing';
      state.queue_count=queue.length;send(202,{ok:true});return;
    }
    send(404,{error:'Not available in preview'});
  });
  return {server,lists,tracks,state,requests,failNext(message){failNext=message;}};
}
if(process.argv[1] === fileURLToPath(import.meta.url)) {
  const host=process.env.PREVIEW_HOST||'127.0.0.1',port=Number(process.env.PREVIEW_PORT||8175);
  const {server}=createPreviewServer();
  server.listen(port,host,()=>console.log(`Mock Cardtunes UI preview (no hardware): http://${host}:${port}`));
}
