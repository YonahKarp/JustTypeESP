const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');

(async () => {
  const browser = await chromium.launch({ channel: process.env.LEAF_BROWSER || 'chrome', headless: true });
  try {
    const context = await browser.newContext();
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(process.env.LEAF_URL || 'http://127.0.0.1:4175');
    await page.waitForFunction(() => typeof Module !== 'undefined' && Module._leaf_state && !document.getElementById('menu').disabled);
    await page.waitForTimeout(400);
    await page.locator('#screen').focus();
    const state = () => page.evaluate(() => Module.ccall('leaf_state', 'string', [], []));
    const filename = async () => (await state()).split('\n')[0];
    const text = async () => (await state()).split('\n').slice(1).join('\n');
    const key = async name => { await page.keyboard.press(name); await page.waitForTimeout(150); };
    const type = async value => { await page.keyboard.type(value, { delay: 15 }); await page.waitForTimeout(80); };
    const fsRead = name => page.evaluate(name => Module.FS.readFile('/spiffs/user_files/' + name, { encoding: 'utf8' }), name);
    const original = await filename();
    await type('pending original');
    await key('Control+n');
    assert.equal(await filename(), '/spiffs/user_files/New File.txt');
    assert.equal(await text(), '');
    assert.equal(await fsRead(original.split('/').pop()), 'pending original');
    await type('first draft');
    await key('Control+n');
    assert.equal(await filename(), '/spiffs/user_files/New File 1.txt');
    assert.equal(await text(), '');
    assert.equal(await fsRead('New File.txt'), 'first draft');
    await page.evaluate(() => {
      const fs = Module.FS, root = '/spiffs/user_files/';
      fs.mkdir(root + 'New File 2.txt'); // Directories cannot be overwritten either.
      fs.writeFile(root + 'New File 3.txt.tmp', 'recover me');
      fs.mkdir(root + 'Stories');
      fs.mkdir(root + 'Stories/Nested');
      for (const [name, body, year] of [
        ['A newest.txt', 'newest body', 2032],
        ['B second.txt', 'second body', 2031],
        ['Stories/Banana.txt', 'monkey story', 2030],
        ['Stories/Nested/Deep.txt', 'nested story', 2029],
        ['Generic.txt', 'content-only-secret', 2028],
      ]) {
        fs.writeFile(root + name, body);
        fs.utime(root + name, new Date(year, 0, 1), new Date(year, 0, 1));
      }
      fs.writeFile(root + '.hidden.txt', 'hidden');
      fs.writeFile(root + 'recently_deleted/Deleted.txt', 'trash');
    });
    await key('Control+n');
    assert.equal(await filename(), '/spiffs/user_files/New File 4.txt');
    await type('keep me');
    await key('Control+o');
    await page.locator('#screen').screenshot({ path: path.join(__dirname, 'build/quick-open-recent.png') });
    await key('Enter');
    assert.equal(await filename(), '/spiffs/user_files/A newest.txt');
    assert.equal(await text(), 'newest body');
    assert.equal(await fsRead('New File 4.txt'), 'keep me');
    await key('Control+o');
    await key('ArrowDown');
    await key('Enter');
    assert.equal(await filename(), '/spiffs/user_files/B second.txt');
    await key('Control+o');
    await type('sToRiEs'); // Match the folder name, preserving recent order.
    await page.locator('#screen').screenshot({ path: path.join(__dirname, 'build/quick-open-filter.png') });
    assert.equal(await text(), 'second body');
    await key('Enter');
    assert.equal(await filename(), '/spiffs/user_files/Stories/Banana.txt');
    await key('Control+o');
    await type('nested');
    await key('Enter');
    assert.equal(await filename(), '/spiffs/user_files/Stories/Nested/Deep.txt');
    await key('Control+o');
    await type('BANANAx');
    await key('Backspace');
    await key('Enter');
    assert.equal(await filename(), '/spiffs/user_files/Stories/Banana.txt');
    await key('Control+o');
    await type('wrong query');
    await key('Control+a');
    await type('nested');
    await key('Enter');
    assert.equal(await filename(), '/spiffs/user_files/Stories/Nested/Deep.txt');
    await key('Control+o');
    await type('banana');
    await key('Enter');
    for (const query of ['content-only-secret', '.hidden', 'Deleted', 'nonexistent']) {
      await key('Control+o');
      await type(query);
      await key('Enter'); // Empty results must not switch documents.
      assert.equal(await filename(), '/spiffs/user_files/Stories/Banana.txt');
      await key('Escape');
      assert.equal(await text(), 'monkey story');
    }
    await key('Control+o');
    await type('ignored');
    await key('Backquote');
    assert.equal(await text(), 'monkey story');
    assert.equal(await page.evaluate(() => Module._leaf_view()), 0);
    await key('Shift+Digit1');
    assert.equal(await text(), 'monkey story!');
    // Opening from the sidebar closes it and restores editor focus afterwards.
    await key('Escape');
    await page.waitForTimeout(250);
    await key('Control+o');
    await key('Escape');
    assert.equal(await page.evaluate(() => Module._leaf_view()), 0);
    await key('Shift+Slash');
    assert.equal(await text(), 'monkey story!?');
    // A failed save must keep the original buffer and avoid creating a file.
    await page.evaluate(() => Module.FS.rename('/spiffs/user_files/Stories', '/spiffs/user_files/Held'));
    await type('unsaved');
    await key('Control+n');
    assert.equal(await filename(), '/spiffs/user_files/Stories/Banana.txt');
    assert.equal(await text(), 'monkey story!?unsaved');
    assert.equal(await page.evaluate(() => Module.FS.analyzePath('/spiffs/user_files/New File 5.txt').exists), false);
    await key('Escape');
    await page.evaluate(() => Module.FS.rename('/spiffs/user_files/Held', '/spiffs/user_files/Stories'));
    // Version-2 preferences retain their old custom bindings during migration.
    // Search is already on Ctrl+N, so the new-file action chooses free Ctrl+D.
    await page.evaluate(() => Module.FS.writeFile('/spiffs/user_files/.keyboard', '2 0 17 23 19 4 6 27 25 46 45 5\n'));
    await page.locator('#save').click();
    await page.waitForFunction(() => document.getElementById('status').textContent === 'Saved in this browser.');
    await page.locator('#restart').click();
    await page.waitForFunction(() => !document.getElementById('menu').disabled);
    await page.waitForTimeout(500);
    await page.locator('#screen').focus();
    await key('Control+d');
    assert.equal(await filename(), '/spiffs/user_files/New File 5.txt');
    await key('Control+o');
    await type('banana');
    await key('Enter');
    assert.equal(await text(), 'monkey story!?unsaved');
    assert.deepEqual(errors, []);
    console.log('PASS: unique blank files, pending-save preservation, recent ordering, nested folder/name filters, no-result and hidden/trash exclusions, backspace/select-all, modal close/focus, save-failure protection, saved shortcut migration.');
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
