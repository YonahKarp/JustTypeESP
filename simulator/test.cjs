const { chromium } = require("playwright");
const assert = require("node:assert/strict");
(async () => {
  const browser = await chromium.launch({
    channel: process.env.LEAF_BROWSER || "chrome",
    headless: true,
  });
  try {
    const context = await browser.newContext(),
      page = await context.newPage(),
      errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    const started = async () => {
      await page.waitForFunction(
        () =>
          typeof Module !== "undefined" &&
          Module._leaf_view &&
          !document.getElementById("menu").disabled,
      );
      await page.waitForTimeout(400);
    };
    await page.goto(process.env.LEAF_URL || "http://127.0.0.1:4173");
    await started();
    const state = () =>
      page.evaluate(() => Module.ccall("leaf_state", "string", [], []));
    const text = async () => (await state()).split("\n").slice(1).join("\n");
    const cursor = () => page.evaluate(() => Module._leaf_cursor());
    const view = () => page.evaluate(() => Module._leaf_view());
    async function key(name, wait = 160) {
      await page.keyboard.press(name);
      await page.waitForTimeout(wait);
    }
    async function type(value) {
      await page.keyboard.type(value, { delay: 20 });
      await page.waitForTimeout(100);
    }
    async function menu() {
      await page.locator("#menu").click();
      await page.waitForTimeout(550);
    }
    async function save() {
      await page.locator("#save").click();
      await page.waitForFunction(
        () =>
          document.getElementById("status").textContent ===
          "Saved in this browser.",
      );
    }
    await page.locator("#screen").focus();
    await type("hello leaf");
    assert.equal(await text(), "hello leaf");
    await key("Home");
    assert.equal(await cursor(), 0);
    await key("Shift+H");
    assert.equal(await text(), "Hhello leaf");
    await key("Delete");
    assert.equal(await text(), "Hello leaf");
    await key("End");
    await key("Backspace");
    assert.equal(await text(), "Hello lea");
    await key("Control+ArrowLeft");
    assert.equal(await cursor(), 6);
    await key("Control+a");
    await type("one two three");
    assert.equal(await text(), "one two three");
    await key("Control+Backspace");
    assert.equal(await text(), "one two ");
    await key("Control+a");
    await key("Control+c", 250);
    await type("copied ");
    await key("Control+v", 250);
    assert.equal(await text(), "copied one two ");
    const documentText = await text();
    await key("Control+f");
    await type("two");
    await key("Enter");
    await key("Escape");
    assert.equal(await text(), documentText);
    const before = await page.locator("#screen").screenshot();
    await menu();
    assert.equal(await view(), 1);
    assert.notDeepEqual(await page.locator("#screen").screenshot(), before);
    // Opening selects the current document, rather than New File.
    await key("Enter", 550);
    assert.equal(await view(), 0);
    assert.equal(await text(), documentText);
    await menu();
    for (let i=0; i<10; ++i) await key("ArrowUp");
    await key("Enter", 350);
    assert.equal(await view(), 3);
    await type("browser draft");
    await key("Enter", 600);
    assert.match(await state(), /^\/spiffs\/user_files\/browser draft\.txt\n/);
    if ((await view()) & 1) await key("Escape", 550);
    await type("a second document");
    await key("Shift+ArrowLeft");
    await key("Backspace");
    assert.equal(await text(), "a second documen");
    await type("t");
    await save();
    await menu();
    await key("Meta");
    assert.equal(await view(), 5);
    await key("ArrowRight");
    await key("ArrowDown");
    await key("ArrowRight");
    await key("ArrowDown");
    await key("ArrowDown");
    const brightness = () => page.locator("#screen").evaluate(el => el.style.filter);
    await key("ArrowLeft");
    assert.equal(await brightness(), "brightness(0.9)");
    // A duplicate Right usage from the prototype still means one increase.
    await page.evaluate(() => {
      Module._leaf_report(0,79,79,0,0,0,0);
      Module._leaf_report(0,0,0,0,0,0,0);
    });
    await page.waitForTimeout(200);
    assert.equal(await brightness(), "brightness(1)");
    for (let i=0; i<10; ++i) await key("Alt+Minus", 60);
    assert.equal(await brightness(), "brightness(0.1)");
    for (let i=0; i<10; ++i) await key("Alt+Equal", 60);
    assert.equal(await brightness(), "brightness(1)");
    await key("Alt+Minus");
    assert.equal(await brightness(), "brightness(0.9)");
    await page.waitForTimeout(1000);
    await page.screenshot({ path: "build/settings.png", fullPage: true });
    await key("Meta");
    assert.equal(await view(), 1);
    await key("Meta");
    assert.equal(await view(), 5);
    await key("Escape");
    await key("Escape", 550);
    await save();
    const settingsBefore = await page.evaluate(() =>
      Module.FS.readFile("/spiffs/user_files/.editor_state", {
        encoding: "utf8",
      }),
    );
    await page.reload();
    await started();
    assert.match(
      await state(),
      /^\/spiffs\/user_files\/browser draft\.txt\na second document$/,
    );
    assert.equal(await page.locator("#screen").evaluate(el => el.style.filter), "brightness(0.9)");
    assert.equal(
      await page.evaluate(() =>
        Module.FS.readFile("/spiffs/user_files/.editor_state", {
          encoding: "utf8",
        }),
      ),
      settingsBefore,
    );
    assert.equal(
      await page.evaluate(() =>
        Module.FS.readFile("/spiffs/user_files/file 1.txt", {
          encoding: "utf8",
        }),
      ),
      documentText,
    );
    await page.locator("#screen").focus();
    await key("End");
    await type(" saved");
    await page.locator("#restart").click();
    await page.waitForTimeout(1300);
    assert.equal(await text(), "a second document saved");
    await page.setViewportSize({ width: 390, height: 844 });
    await page.locator("#keyboard").click();
    await page.locator("#mobile-input").evaluate((el) =>
      el.dispatchEvent(
        new InputEvent("input", {
          data: " café 🌿",
          inputType: "insertText",
          bubbles: true,
        }),
      ),
    );
    await page.waitForTimeout(200);
    assert.equal(await text(), "a second document saved café 🌿");
    await page.locator("#mobile-input").evaluate((el) =>
      el.dispatchEvent(
        new InputEvent("beforeinput", {
          inputType: "deleteContentBackward",
          bubbles: true,
          cancelable: true,
        }),
      ),
    );
    await page.waitForTimeout(200);
    assert.equal(await text(), "a second document saved café ");
    await page.locator("#mobile-input").evaluate((el) => {
      el.dispatchEvent(
        new CompositionEvent("compositionstart", { bubbles: true }),
      );
      el.dispatchEvent(
        new InputEvent("input", {
          data: "é",
          isComposing: true,
          bubbles: true,
        }),
      );
      el.dispatchEvent(
        new CompositionEvent("compositionend", { data: "é", bubbles: true }),
      );
      el.dispatchEvent(new InputEvent("input", { data: "é", bubbles: true }));
    });
    await page.waitForTimeout(200);
    assert.equal(await text(), "a second document saved café é");
    assert.equal(
      await page.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth,
      ),
      true,
    );
    await page.screenshot({ path: "build/mobile.png", fullPage: true });
    await page.setViewportSize({ width: 1280, height: 1100 });
    await page.screenshot({ path: "build/desktop.png", fullPage: true }); // Exercise rename, folder creation and trash through firmware dialogs.
    await page.locator("#screen").focus();
    await menu();
    await key("ArrowRight", 350);
    for (let i = 0; i < 13; i++) await key("Backspace", 20);
    await type("renamed draft");
    await key("Enter", 600);
    assert.match(await state(), /^\/spiffs\/user_files\/renamed draft\.txt\n/);
    // Reopen to select the renamed current document in its new sorted position.
    await key("Escape", 550);
    await menu();
    await key("Delete");
    await key("ArrowLeft");
    await key("Enter", 350);
    assert.equal(
      await page.evaluate(() =>
        Module.FS.readdir("/spiffs/user_files").includes("renamed draft.txt"),
      ),
      false,
    );
    assert.ok(
      await page.evaluate(() =>
        Module.FS.readdir("/spiffs/user_files/recently_deleted").some((n) =>
          n.includes("renamed draft"),
        ),
      ),
    );
    await key("Escape", 550);
    await menu();
    for (let i=0; i<10; ++i) await key("ArrowUp");
    await key("ArrowLeft");
    await key("Enter", 350);
    await type("ideas");
    await key("Enter", 350);
    assert.ok(
      await page.evaluate(() =>
        Module.FS.isDir(Module.FS.stat("/spiffs/user_files/ideas").mode),
      ),
    );
    await key("Escape", 550);
    await save();
    await page.reload();
    await started();
    assert.ok(
      await page.evaluate(() =>
        Module.FS.isDir(Module.FS.stat("/spiffs/user_files/ideas").mode),
      ),
    );
    assert.deepEqual(errors, []);
    console.log(
      "PASS: real HID typing, uppercase, deletion, cursor/word navigation, selection, clipboard, search, menu rendering, document creation, two-document persistence, appearance persistence, restart, mobile UTF-8/IME and layout, rename, trash and folder persistence.",
    );
    await context.close();
    // A storage-denied visitor can still type, and Restart must retain the session.
    const denied = await browser.newContext();
    await denied.addInitScript(() =>
      Object.defineProperty(window, "indexedDB", {
        get() {
          throw new Error("storage blocked for test");
        },
      }),
    );
    const deniedPage = await denied.newPage();
    await deniedPage.goto(process.env.LEAF_URL || "http://127.0.0.1:4173");
    await deniedPage.waitForFunction(
      () => !document.getElementById("menu").disabled,
    );
    assert.match(
      await deniedPage.locator("#status").textContent(),
      /storage unavailable/,
    );
    await deniedPage.locator("#screen").focus();
    await deniedPage.keyboard.type("session only", { delay: 20 });
    await deniedPage.waitForTimeout(100);
    assert.match(
      await deniedPage.evaluate(() =>
        Module.ccall("leaf_state", "string", [], []),
      ),
      /session only$/,
    );
    await deniedPage.locator("#restart").click();
    await deniedPage.waitForFunction(() =>
      document
        .getElementById("status")
        .textContent.includes("Restart cancelled"),
    );
    await denied.close();
    console.log(
      "PASS: blocked-storage fallback and failed-save restart protection.",
    );
  } finally {
    await browser.close();
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
