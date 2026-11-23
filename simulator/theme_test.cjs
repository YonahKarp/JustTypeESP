const { chromium } = require('playwright');
const assert = require('node:assert/strict');
(async () => {
  const browser = await chromium.launch({channel: 'chrome', headless: true});
  try {
    const page = await browser.newPage();
    await page.goto('http://127.0.0.1:4179');
    await page.waitForFunction(() => typeof Module !== 'undefined' && Module._leaf_state && !document.getElementById('menu').disabled);
    await page.locator('#screen').focus();
    const pixel = (x, y) => page.locator('#screen').evaluate((canvas, xy) => Array.from(canvas.getContext('2d').getImageData(xy[0], xy[1], 1, 1).data), [x,y]);
    const wait = () => page.waitForTimeout(500);
    const dark = await pixel(5,200);
    await page.keyboard.press('Control+t'); await wait();
    const light = await pixel(5,200);
    assert(light[0] > 200 && dark[0] < 70, `margin must change: ${dark} -> ${light}`);
    await page.locator('#menu').click(); await wait();
    await page.locator('#screen').screenshot({path: 'simulator/build/theme-light.png'});
    const titlePixels = () => page.locator('#screen').evaluate(canvas => {
      const d = canvas.getContext('2d').getImageData(10, 10, 65, 35).data;
      let dark = 0, bright = 0;
      for (let i = 0; i < d.length; i += 4) { if (d[i] < 100) dark++; if (d[i] > 180) bright++; }
      return {dark, bright};
    });
    const lightTitle = await titlePixels();
    assert(lightTitle.dark > 20, `light theme Files label must be dark: ${JSON.stringify(lightTitle)}`);
    await page.keyboard.press('Escape'); await wait();
    await page.keyboard.press('Control+t'); await wait();
    await page.locator('#menu').click(); await wait();
    const darkTitle = await titlePixels();
    assert(darkTitle.bright > 20, `dark theme Files label must be light: ${JSON.stringify(darkTitle)}`);
    console.log('PASS: screen margin and Files label update in light and dark themes.');
  } finally { await browser.close(); }
})().catch(e => { console.error(e); process.exitCode = 1; });
