const { chromium } = require('playwright');
const assert = require('node:assert/strict');
(async () => {
 const browser = await chromium.launch({channel:'chrome',headless:true});
 try {
  const context = await browser.newContext();
  const page = await context.newPage();
  const errors=[]; page.on('pageerror',e=>errors.push(e.message));
  const start=async()=>{await page.waitForFunction(()=>typeof Module!=='undefined' && Module._leaf_view && !document.querySelector('#menu').disabled);await page.waitForTimeout(350);};
  const key=async(k)=>{await page.keyboard.press(k);await page.waitForTimeout(120);};
  const file=async(path)=>page.evaluate(p=>Module.FS.readFile(p,{encoding:'utf8'}),path);
  const editorState=async()=>{await page.evaluate(()=>Module._leaf_save());return (await file('/spiffs/user_files/.editor_state')).split('\n');};
  const documentText=async()=> (await page.evaluate(()=>Module.ccall('leaf_state','string',[],[]))).split('\n').slice(1).join('\n');
  await page.goto(process.env.LEAF_URL || 'http://127.0.0.1:4174/');await start();
  // A v3 preference from deployed firmware must load and migrate on first save.
  await page.evaluate(()=>new Promise((resolve,reject)=>{Module.FS.writeFile('/spiffs/user_files/.keyboard','3 0 9 23 19 4 6 27 25 46 45 5 17 18\n');Module.FS.syncfs(false,e=>e?reject(e):resolve());}));
  await page.reload();await start();await page.locator('#screen').focus();
  await key('Control+a');await key('Backspace');
  const contentState=()=>page.evaluate(()=>[0,1].map(n=>{try{return JSON.parse(JSON.parse(Module.FS.readFile(`/spiffs/leaf_content/registry/${n}.json`,{encoding:'utf8'})).payload);}catch{return null;}}).filter(Boolean).sort((a,b)=>b.generation-a.generation)[0]);
  const assertThemeStep=async(binding,message)=>{
    const cs=await contentState();
    const options=['builtin:0','builtin:1','builtin:2','builtin:3','builtin:4',...(cs?.packages.filter(p=>p.type==='theme').map(p=>p.id)||[])];
    const active=cs?.theme||'builtin:'+Number((await editorState())[3]);
    const expected=options[(options.indexOf(active)+1)%options.length];
    await key(binding);
    assert.equal((await contentState())?.theme||'builtin:'+Number((await editorState())[3]),expected,message);
  };
  for (const theme of [2,3,4]) {await key('Control+t');assert.equal(Number((await editorState())[3]),theme,'Ctrl+T cycles built-in themes');}
  const installedThemes=(await contentState())?.packages.filter(p=>p.type==='theme')||[];
  for(const theme of installedThemes){await key('Control+t');assert.equal((await contentState()).theme,theme.id,'Ctrl+T includes installed themes');}
  for(const theme of [0,1]){await key('Control+t');assert.equal(Number((await editorState())[3]),theme,'Ctrl+T wraps to built-in themes');}
  await key('Meta'); assert.equal(await page.evaluate(()=>Module._leaf_view()),4);
  await key('Enter');await key('ArrowDown');await key('Escape');
  assert.equal(Number((await editorState())[3]),1,'Escape cancels dropdown selection');
  await key('Enter');await key('ArrowDown');await key('Enter');
  assert.equal(Number((await editorState())[3]),2,'theme dropdown selects Ocean');
  await key('ArrowRight');assert.equal(Number((await editorState())[3]),3,'right cycles theme');
  await key('ArrowDown');await key('ArrowRight');assert.equal(Number((await editorState())[2]),4,'right increases font');
  await key('Enter');await key('ArrowDown');await key('Enter');assert.equal(Number((await editorState())[2]),5,'font dropdown selects next size');
  await key('ArrowDown');await key('ArrowDown');await key('ArrowLeft');
  assert.equal(await page.locator('#screen').evaluate(el=>el.style.filter),'brightness(0.9)');
  await key('Tab');
  await key('ArrowRight');await key('ArrowLeft');
  assert.equal((await file('/spiffs/user_files/.keyboard')).split(' ')[1],'0','arrows do not change closed layout');
  await key('Enter');await key('ArrowDown');await key('ArrowDown');await key('Enter');
  let binding=(await file('/spiffs/user_files/.keyboard')).trim().split(/\s+/).map(Number);
  assert.equal(binding[0],5);assert.equal(binding[1],2,'layout dropdown selected');assert.equal(binding[2],0x109,'v3 Ctrl+F migrated');
  await key('ArrowDown');await key('Enter'); // key bindings popover
  await key('Enter');await key('q'); // unmodified key must not be captured
  assert.equal((await file('/spiffs/user_files/.keyboard')).trim().split(/\s+/)[2],'265');
  await key('Alt+Equal');assert.equal(await page.locator('#screen').evaluate(el=>el.style.filter),'brightness(0.9)','reserved binding cannot alter brightness during capture');
  await key('Escape');assert.equal(await page.evaluate(()=>Module._leaf_view()),4,'Escape from capture returns to Settings');
  await key('Enter');await key('Enter');
  await key('Alt+q');
  binding=(await file('/spiffs/user_files/.keyboard')).trim().split(/\s+/).map(Number);
  assert.equal(binding[2],0x214,'Opt+Q assigned to Search');
  await key('ArrowDown');await key('Enter');await key('Control+Alt+Enter');
  binding=(await file('/spiffs/user_files/.keyboard')).trim().split(/\s+/).map(Number);
  assert.equal(binding[3],0x328,'Ctrl+Opt+Enter assigned to theme');
  await key('ArrowDown');await key('Enter');await key('Control+Alt+ArrowRight');
  binding=(await file('/spiffs/user_files/.keyboard')).trim().split(/\s+/).map(Number);
  assert.equal(binding[4],0x34f,'Ctrl+Opt+Right assigned to Select all');
  await key('Escape');assert.equal(await page.evaluate(()=>Module._leaf_view()),4,'Esc returns to Settings');
  await key('Escape');assert.equal(await page.evaluate(()=>Module._leaf_view()),0,'Esc closes Settings');
  await key('Control+Alt+Enter');assert.equal(Number((await editorState())[3]),4,'Ctrl+Opt+Enter invokes theme without inserting a newline');assert.equal(await documentText(),'');
  await page.keyboard.type('probe');await page.waitForTimeout(200);
  assert.equal((await documentText()).length,5);
  await key('Control+Alt+ArrowRight');
  await page.keyboard.type('a');await page.waitForTimeout(200);
  assert.equal((await documentText()).length,1,'remapped arrow selects all');
  await key('Backspace');assert.equal(await documentText(),'');
  // Removing document QR export leaves Ctrl+P free and does not open a modal.
  await key('Control+p');assert.equal(await documentText(),'');
  assert.equal(await page.evaluate(()=>Module._leaf_view()),0);
  await key('Alt+q');await page.keyboard.type('find');await page.waitForTimeout(200);
  assert.equal(await documentText(),'','Opt binding opens search without inserting text');
  await key('Escape');await page.locator('#save').click();
  await page.waitForFunction(()=>document.querySelector('#status').textContent==='Saved in this browser.');
  await page.reload();await start();await page.locator('#screen').focus();
  await assertThemeStep('Control+Alt+Enter','binding survives reboot');assert.equal(await documentText(),'');
  await key('Alt+q');await page.keyboard.type('find');await page.waitForTimeout(200);assert.equal(await documentText(),'');
  await key('Escape');
  // Version 4 had 12 entries, with a custom QR action at index 2. Retain all
  // other custom actions and make the former QR shortcut free on migration.
  await page.evaluate(()=>new Promise((resolve,reject)=>{
    Module.FS.writeFile('/spiffs/user_files/.keyboard','4 0 532 808 847 260 262 283 281 302 301 261 273 274\n');
    Module.FS.syncfs(false,e=>e?reject(e):resolve());
  }));
  await page.reload();await start();await page.locator('#screen').focus();
  await assertThemeStep('Control+Alt+Enter','v4 custom theme binding retained');
  const previousText=await documentText();
  await key('Control+Alt+ArrowRight');assert.equal(await documentText(),previousText,'removed QR binding no longer runs');
  await key('Alt+q');await page.keyboard.type('find');await page.waitForTimeout(200);assert.equal(await documentText(),previousText,'v4 custom search binding retained');
  await key('Escape');await key('Meta');await key('Tab');await key('Enter');await key('ArrowDown');await key('Enter');
  binding=(await file('/spiffs/user_files/.keyboard')).trim().split(/\s+/).map(Number);
  assert.deepEqual(binding.slice(2),[532,808,260,262,283,281,302,301,261,273,274],'v4 bindings retain their actions after QR removal');
  assert.equal(binding[0],5,'upgraded format written');
  // Version 1 predates new/open actions. A custom Ctrl+N search remains in
  // place and new-file receives a free default during migration.
  await key('Meta');
  await page.evaluate(()=>new Promise((resolve,reject)=>{
    Module.FS.writeFile('/spiffs/user_files/.keyboard','1 0 17 23 19 4 6 27 25 46 45\n');
    Module.FS.syncfs(false,e=>e?reject(e):resolve());
  }));
  await page.reload();await start();await page.locator('#screen').focus();
  await key('Control+d');
  assert.match(await page.evaluate(()=>Module.ccall('leaf_state','string',[],[])), /\/New File\.txt\n$/,'v1 new-file action gets a free binding');
  await key('Control+n');await page.keyboard.type('find');await page.waitForTimeout(200);
  assert.equal(await documentText(),'','v1 custom Ctrl+N still opens search');
  assert.deepEqual(errors,[]);console.log('PASS: dropdowns, slider, theme cycling, key capture, Ctrl/Opt dispatch and v1/v3/v4 migration/persistence and QR removal');
  await context.close();
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
