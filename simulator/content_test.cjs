const {chromium} = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({channel:'chrome',headless:true});
 try {
  const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
  const ready=async()=>{await page.waitForFunction(()=>typeof Module!=='undefined' && Module._leaf_view && !document.querySelector('#menu').disabled);await page.waitForTimeout(300);};
  const key=async k=>{await page.keyboard.press(k);await page.waitForTimeout(150);};
  const registry=()=>page.evaluate(()=>{const records=[0,1].map(n=>{try{return JSON.parse(JSON.parse(Module.FS.readFile(`/spiffs/leaf_content/registry/${n}.json`,{encoding:'utf8'})).payload);}catch{return null;}}).filter(Boolean);return records.sort((a,b)=>b.generation-a.generation)[0];});
  await page.goto(process.env.LEAF_URL||'http://127.0.0.1:4174');await ready();
  const base=path.resolve(__dirname,'../build/content');
  const catalog=JSON.parse(fs.readFileSync(path.join(base,'catalog.json')));
  catalog.packages=catalog.packages.filter(p=>p.type!=='pack' && p.language!=='ru');
  const font=fs.readFileSync(path.resolve(__dirname,'../build/content-test-font.bin'));
  const fontPackage={id:'org.justtype.font.test',version:'1.0.0',format:1,type:'font',name:'Test font',language:'',description:'Local test fixture',requires:[],files:[{name:'24.bin',size:font.length,sha256:crypto.createHash('sha256').update(font).digest('hex')}]};
  catalog.packages.push(fontPackage);
  const files=[];
  for(const p of catalog.packages)for(const f of p.files){const bytes=p.type==='font'?font:fs.readFileSync(path.join(base,'files',p.id,p.version,f.name));files.push([`/spiffs/leaf_content/packages/${p.id}/${p.version}/${f.name}`,bytes.toString('base64')]);}
  files.push(['/spiffs/leaf_content/seed.json',Buffer.from(JSON.stringify(catalog)).toString('base64')]);
  await page.evaluate(async files=>{
    for(const [p,b64]of files){Module.FS.mkdirTree(p.slice(0,p.lastIndexOf('/')));Module.FS.writeFile(p,Uint8Array.from(atob(b64),c=>c.charCodeAt(0)));}
    await new Promise((resolve,reject)=>Module.FS.syncfs(false,e=>e?reject(e):resolve()));
  },files);
  await page.reload();await ready();await page.locator('#screen').focus();
  assert.equal((await registry()).dictionary,'org.justtype.dictionary.en');
  await key('Meta');await key('Tab');await key('Tab');await key('Tab');
  await page.locator('#screen').screenshot({path:'simulator/build/content-settings.png'});
  await key('ArrowDown');await key('ArrowDown');await key('ArrowDown');await key('Enter');
  assert.equal((await registry()).dictionary,'','Disable turns spellcheck off');
  await key('ArrowUp');await key('Enter');assert.equal((await registry()).dictionary,'org.justtype.dictionary.en');
  await key('ArrowDown');await key('ArrowDown');await key('Enter');assert.equal((await registry()).dictionary,'');
  assert(!(await registry()).packages.some(p=>p.id==='org.justtype.dictionary.en'));
  await key('Escape');await key('Meta');await key('ArrowDown');await key('ArrowDown');await key('Enter');await key('ArrowDown');await key('Enter');
  assert.equal((await registry()).font,fontPackage.id,'Appearance font dropdown enables installed font');
  await key('Escape');await page.evaluate(()=>Module._leaf_save());await page.waitForTimeout(200);
  await page.evaluate(()=>new Promise((resolve,reject)=>Module.FS.syncfs(false,e=>e?reject(e):resolve())));
  await page.reload();await ready();assert.equal((await registry()).dictionary,'');assert.equal((await registry()).font,fontPackage.id);
  assert.deepEqual(errors,[]);console.log('Content tab, offline disable/remove, font selection and reboot persistence passed');
 } finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
