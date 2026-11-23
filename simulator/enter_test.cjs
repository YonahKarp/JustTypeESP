const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({channel:'chrome',headless:true});
 try {
  for (const mobile of [false,true]) {
   const ctx=await browser.newContext(mobile ? {viewport:{width:390,height:844},hasTouch:true,isMobile:true} : {});
   const page=await ctx.newPage(), errors=[];
   page.on('pageerror',e=>errors.push(e.message));
   await page.goto(process.env.LEAF_URL || 'http://127.0.0.1:4174/sim/');
   await page.waitForFunction(()=>typeof Module!=='undefined'&&Module._leaf_view&&!document.querySelector('#enter').disabled);
   await page.waitForTimeout(400);
   const text=async()=> (await page.evaluate(()=>Module.ccall('leaf_state','string',[],[]))).split('\n').slice(1).join('\n');
   const view=()=>page.evaluate(()=>Module._leaf_view());
   const pause=async()=>page.waitForTimeout(550);
   assert.equal(await page.locator('#restart').count(),0);
   assert.deepEqual(await page.locator('.toolbar button').evaluateAll(es=>es.map(e=>e.id)),['menu','keyboard','enter','tab','save']);
   await page.locator('#screen').focus();
   await page.keyboard.press('Control+a');await page.keyboard.press('Backspace');await page.waitForTimeout(150);
   await page.keyboard.type('abc');
   await page.keyboard.press('Enter'); await pause();
   assert.equal(await text(),'abc\n','physical Enter inserts one newline');
   await page.keyboard.press('NumpadEnter'); await pause();
   assert.equal(await text(),'abc\n\n','numpad Enter inserts newline');
   await page.locator('#enter').click(); await pause();
   assert.equal(await text(),'abc\n\n\n','on-page Enter inserts newline');
   await page.locator('#keyboard').click();
   await page.keyboard.press('Enter'); await pause();
   assert.equal(await text(),'abc\n\n\n\n','physical Enter with mobile input inserts exactly once');
   await page.locator('#mobile-input').evaluate(el=>el.dispatchEvent(new InputEvent('beforeinput',{inputType:'insertLineBreak',bubbles:true,cancelable:true})));
   await pause(); assert.equal(await text(),'abc\n\n\n\n\n','virtual keyboard newline event');
   await page.locator('#menu').click(); await pause(); assert.equal(await view(),1);
   await page.locator('#enter').click(); await pause(); assert.equal(await view(),0,'button activates selected item');
   await page.locator('#menu').click(); await pause(); assert.equal(await view(),1);
   await page.locator('#keyboard').click();
   await page.locator('#mobile-input').evaluate(el=>el.dispatchEvent(new InputEvent('beforeinput',{inputType:'insertParagraph',bubbles:true,cancelable:true})));
   await pause(); assert.equal(await view(),0,'virtual keyboard Enter activates selected item');
   assert.equal(await text(),'abc\n\n\n\n\n');
   await page.locator('#save').click();
   await page.waitForFunction(()=>document.querySelector('#status').textContent==='Saved in this browser.');
   await page.reload(); await page.waitForFunction(()=>typeof Module!=='undefined'&&Module._leaf_view&&!document.querySelector('#enter').disabled); await pause();
   assert.equal(await text(),'abc\n\n\n\n\n','saved document persists');
   if (mobile) {
    const enterBox=await page.locator('#enter').boundingBox(), tabBox=await page.locator('#tab').boundingBox();
    assert.ok(tabBox && Math.abs(tabBox.x-enterBox.x)<1 && tabBox.y>=enterBox.y+enterBox.height, 'Tab sits under Enter');
    await page.locator('#screen').focus();
    await page.keyboard.press('Meta'); await pause(); assert.equal(await view(),4);
    const before=await page.locator('#screen').screenshot();
    await page.locator('#tab').click(); await pause();
    const after=await page.locator('#screen').screenshot();
    assert.notDeepEqual(after,before,'Tab changes the active Settings tab');
    assert.equal(await view(),4);
    assert.equal(await text(),'abc\n\n\n\n\n');
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
   } else assert.equal(await page.locator('#tab').isVisible(),false,'Tab is mobile only');
   assert.deepEqual(errors,[]); await ctx.close();
   console.log('PASS:',mobile?'touch layout':'desktop','Enter sources, menu activation and persistence');
  }
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
