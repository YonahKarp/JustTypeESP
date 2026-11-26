const {chromium}=require('playwright');const assert=require('node:assert/strict');
(async()=>{const browser=await chromium.launch({channel:'chrome',headless:true});try{
 const page=await browser.newPage();const errors=[],responses=[];page.on('pageerror',e=>errors.push(e.message));
 page.on('response',r=>{if(r.url().includes('/api/leaf/content/v1/'))responses.push([r.url(),r.status()]);});
 const key=async k=>{await page.keyboard.press(k);await page.waitForTimeout(150);};
 const state=()=>page.evaluate(()=>[0,1].map(n=>{try{return JSON.parse(JSON.parse(Module.FS.readFile(`/spiffs/leaf_content/registry/${n}.json`,{encoding:'utf8'})).payload);}catch{return null;}}).filter(Boolean).sort((a,b)=>b.generation-a.generation)[0]);
 await page.goto(process.env.LEAF_URL||'https://justtypeleaf.com/sim/');await page.waitForFunction(()=>typeof Module!=='undefined'&&Module._leaf_has_glyph&&!document.querySelector('#menu').disabled);await page.locator('#screen').focus();
 assert.equal(await page.evaluate(()=>Module._leaf_has_glyph(24,0x439)),0);
 await key('Meta');for(let i=0;i<3;i++)await key('Tab');await key('Control+ArrowDown');await key('Control+ArrowDown');await key('ArrowDown');await key('ArrowDown');await key('Enter');
 await page.waitForFunction(()=>Module.leafContentDownload?.status>=2,null,{timeout:30000});await page.waitForTimeout(350);
 await key('ArrowUp');await key('ArrowUp');await key('Enter');
 await page.waitForFunction(()=>Module.leafContentDownload?.status>=2,null,{timeout:60000});await page.waitForTimeout(500);
 assert((await state()).packages.some(p=>p.id==='org.justtype.layout.spanish'));assert((await state()).packages.some(p=>p.id==='org.justtype.glyphs.international.v2'));
 assert.equal(await page.evaluate(()=>Module._leaf_has_glyph(24,0x439)),1);
 assert(responses.filter(([u,s])=>u.includes('glyphs.international')&&s===200).length===6,'Six real glyph assets downloaded');
 await key('Escape');await key('Meta');for(let i=0;i<3;i++)await key('Tab');await key('Control+ArrowDown');await key('Control+ArrowDown');await key('ArrowDown');await key('ArrowDown');await key('Delete');await key('Enter');await page.waitForTimeout(400);
 assert(!(await state()).packages.some(p=>p.id==='org.justtype.glyphs.international.v2'));assert.equal(await page.evaluate(()=>Module._leaf_has_glyph(24,0x439)),0);
 assert.deepEqual(errors,[]);console.log('PASS: real Spanish download includes six glyph assets, which are available and removed with last layout');
}finally{await browser.close();}})().catch(e=>{console.error(e);process.exit(1);});
