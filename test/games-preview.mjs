import {createServer} from 'node:http';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve} from 'node:path';

export function createGamesPreview() {
  const child=spawn(resolve('build/game-driver'),[],{stdio:['pipe','pipe','inherit']});
  const pending=[];
  createInterface({input:child.stdout}).on('line',line=>pending.shift()?.resolve(line));
  child.on('exit',()=>{for(const p of pending.splice(0))p.reject(new Error('Game driver stopped'));});
  const html=readFileSync(new URL('./games-preview.html',import.meta.url));
  const server=createServer(async(req,res)=>{
    if(req.url==='/' && req.method==='GET'){res.setHeader('Content-Type','text/html');res.end(html);return;}
    if(req.url!=='/frame'||req.method!=='POST'){res.writeHead(404).end();return;}
    try{
      let body='';for await(const part of req){body+=part;if(body.length>1024)throw Error('Too large');}
      const input=JSON.parse(body);
      const result=await new Promise((resolve,reject)=>{pending.push({resolve,reject});child.stdin.write(JSON.stringify(input)+'\n');});
      res.setHeader('Content-Type','application/json');res.end(result);
    }catch{res.writeHead(400).end();}
  });
  server.on('close',()=>child.stdin.end());
  return server;
}
if(process.argv[1]===fileURLToPath(import.meta.url)) {
  const host=process.env.PREVIEW_HOST;
  if(!host)throw Error('Set PREVIEW_HOST to this computer\'s Tailscale IP');
  const server=createGamesPreview();server.listen(8176,host,()=>console.log(`Game preview: http://${host}:8176`));
}
