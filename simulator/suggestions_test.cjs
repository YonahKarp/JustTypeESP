const { chromium } = require('playwright');
const assert = require('node:assert/strict');
(async () => {
  const browser = await chromium.launch({channel:'chrome', headless:true});
  try {
    const page = await browser.newPage();
    await page.goto('http://127.0.0.1:4176');
    await page.waitForFunction(() => typeof Module !== 'undefined' && Module._leaf_state && !document.getElementById('menu').disabled);
    await page.locator('#screen').focus();
    await page.keyboard.type('helo', {delay:30});
    await page.waitForTimeout(1200);
    await page.keyboard.press('Control+Space');
    await page.waitForTimeout(1200);
    await page.locator('#screen').screenshot({path:'simulator/build/suggestions.png'});
    await page.keyboard.press('Enter');
    await page.waitForTimeout(300);
    const text = await page.evaluate(() => Module.ccall('leaf_state','string',[],[]).split('\n').slice(1).join('\n'));
    assert.equal(text, 'hello');
    console.log('Suggestion selection replaced helo with hello');
  } finally { await browser.close(); }
})().catch(e => { console.error(e); process.exitCode=1; });
