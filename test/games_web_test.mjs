import assert from 'node:assert/strict';
import {mkdirSync} from 'node:fs';
import {createGamesPreview} from './games-preview.mjs';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE||'playwright');
const server=createGamesPreview();await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({headless:true});mkdirSync('build/screenshots',{recursive:true});
try{
 const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(`http://127.0.0.1:${server.address().port}`);
 const acknowledged=async()=>{const sequence=await page.evaluate(()=>gameSequence());await page.waitForFunction(n=>gameFrame().sequence>=n,sequence);};
 await page.locator('[data-game="blocks"]').focus();await page.keyboard.press('Tab');assert(await page.locator('[data-game="breakout"]').evaluate(e=>e===document.activeElement));
 for(const [name,size]of [['desktop',{width:1280,height:900}],['mobile',{width:390,height:844}],['narrow',{width:320,height:740}]]){
  await page.setViewportSize(size);
  assert.equal(await page.locator('#pad button').count(),5);
  for(const id of ['blocks','breakout','2048']){
   await page.locator(`[data-game="${id}"]`).click();await page.waitForFunction(id=>gameFrame().game===id,id);
   await page.locator('#restart').click();await acknowledged();
   const before=await page.locator('canvas').evaluate(c=>c.toDataURL());
   await page.keyboard.press(id==='2048'?'ArrowDown':'Space');await acknowledged();
   const after=await page.locator('canvas').evaluate(c=>c.toDataURL());assert.notEqual(before,after,`${id} must respond`);
   const colors=await page.locator('canvas').evaluate(c=>{const d=c.getContext('2d').getImageData(0,0,240,135).data;const s=new Set();for(let i=0;i<d.length;i+=4)s.add(`${d[i]},${d[i+1]},${d[i+2]}`);return s.size;});assert(colors>5,`${id} canvas nonblank`);
   await page.keyboard.press('p');await page.waitForFunction(()=>gameFrame().paused);
   await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await acknowledged();assert(await page.evaluate(()=>gameFrame().paused));
   await page.screenshot({path:`build/screenshots/game-${id}-${name}-paused.png`,fullPage:true});
   await page.keyboard.press('p');await page.waitForFunction(()=>!gameFrame().paused);
   await page.screenshot({path:`build/screenshots/game-${id}-${name}.png`,fullPage:true});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'No horizontal overflow');
   await page.keyboard.press('Escape');await page.waitForFunction(()=>gameFrame().game==='none');
  }
 }
 assert.deepEqual(errors,[]);console.log('Native games browser QA: all 3 games, real input/motion/pause/exit, canvas pixels, 3 viewports passed.');
}finally{await browser.close();await new Promise(r=>server.close(r));}
