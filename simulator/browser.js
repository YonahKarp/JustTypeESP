/* Browser integration only: all editing, menus, layout and pixels come from C++. */
"use strict";
const screen = document.getElementById("screen");
const input = document.getElementById("mobile-input");
const statusLine = document.getElementById("status");
const ctx = screen.getContext("2d", { alpha: false });
const image = ctx.createImageData(800, 480);
let ready = false;
let syncing = null;
let storageError = false;
let storageReady = false;
let composing = false;
let compositionCommit = null;
const held = new Set();
let modifiers = 0;
const usages = {
  Enter: 40,
  Escape: 41,
  Backspace: 42,
  Tab: 43,
  Space: 44,
  Minus: 45,
  Equal: 46,
  BracketLeft: 47,
  BracketRight: 48,
  Backslash: 49,
  Semicolon: 51,
  Quote: 52,
  Backquote: 53,
  Comma: 54,
  Period: 55,
  Slash: 56,
  CapsLock: 57,
  Home: 74,
  Delete: 76,
  End: 77,
  ArrowRight: 79,
  ArrowLeft: 80,
  ArrowDown: 81,
  ArrowUp: 82,
};
for (let i = 0; i < 26; i++)
  usages["Key" + String.fromCharCode(65 + i)] = 4 + i;
for (let i = 1; i <= 9; i++) usages["Digit" + i] = 29 + i;
usages.Digit0 = 39;
const modifierBits = {
  ControlLeft: 1,
  ShiftLeft: 2,
  AltLeft: 4,
  MetaLeft: 8,
  ControlRight: 16,
  ShiftRight: 32,
  AltRight: 64,
  MetaRight: 128,
};
function report() {
  if (!ready) return;
  const keys = [...held].slice(0, 6);
  Module._leaf_report(
    modifiers,
    ...Array.from({ length: 6 }, (_, i) => keys[i] || 0),
  );
}
function releaseAll() {
  held.clear();
  modifiers = 0;
  report();
}
function focusScreen() {
  screen.focus({ preventScroll: true });
}
function sendText(text) {
  if (ready && text) Module.ccall("leaf_text", null, ["string"], [text]);
}
function fail(message) {
  statusLine.textContent = message;
}
var Module = {
  preRun: [
    function () {
      Module.addRunDependency("leaf-storage");
      try {
        FS.mkdir("/spiffs");
        FS.mount(IDBFS, {}, "/spiffs");
        FS.syncfs(true, function (error) {
          storageReady = !error;
          storageError = !!error;
          if (error)
            fail(
              "Browser storage unavailable. Changes will only last for this session.",
            );
          Module.removeRunDependency("leaf-storage");
        });
      } catch (error) {
        storageError = true;
        fail(
          "Browser storage unavailable. Changes will only last for this session.",
        );
        Module.removeRunDependency("leaf-storage");
      }
    },
  ],
  onRuntimeInitialized() {
    ready = true;
    document.querySelectorAll("button").forEach((b) => (b.disabled = false));
    if (!storageError)
      statusLine.textContent =
        "Ready. Start typing — your documents save in this browser.";
    focusScreen();
  },
  onAbort(reason) {
    ready = false;
    fail("Leaf could not start. Reload to retry. " + reason);
  },
  printErr(message) {
    console.error(message);
  },
  present(pointer) {
    // Recreate the view after memory growth. Convert Leaf's RGB565, not its UI.
    const words = new Uint16Array(Module.HEAPU8.buffer, pointer, 800 * 480);
    const rgba = image.data;
    for (let i = 0, j = 0; i < words.length; i++, j += 4) {
      const v = words[i],
        r = v >> 11,
        g = (v >> 5) & 63,
        b = v & 31;
      rgba[j] = (r << 3) | (r >> 2);
      rgba[j + 1] = (g << 2) | (g >> 4);
      rgba[j + 2] = (b << 3) | (b >> 2);
      rgba[j + 3] = 255;
    }
    ctx.putImageData(image, 0, 0);
  },
  syncStorage() {
    if (!storageReady) return Promise.resolve(false);
    if (syncing) return syncing;
    syncing = new Promise((resolve) =>
      FS.syncfs(false, (error) => {
        if (error) {
          storageError = true;
          fail("Local save failed. Browser storage may be full or blocked.");
        }
        resolve(!error);
      }),
    ).finally(() => (syncing = null));
    return syncing;
  },
};
// Only capture keys while the writing surface has focus, so the surrounding page
// retains normal scrolling, keyboard access and browser controls.
window.addEventListener("keydown", (event) => {
  if (
    !ready ||
    (document.activeElement !== screen && document.activeElement !== input) ||
    event.isComposing
  )
    return;
  if (event.code in modifierBits) {
    modifiers |= modifierBits[event.code];
    report();
    event.preventDefault();
    return;
  }
  const usage = usages[event.code];
  if (!usage) return;
  // Mobile input events supply text; do not also generate HID text.
  if (
    document.activeElement === input &&
    !event.ctrlKey &&
    !event.metaKey &&
    (event.key.length === 1 || event.key === "Enter")
  )
    return;
  event.preventDefault();
  if (event.repeat) {
    // Native LVGL repeats navigation keys. The firmware HID decoder emits text
    // on edges, so synthesize another edge only for printable keys.
    if ((usage >= 44 && usage <= 56) || (usage >= 4 && usage <= 39)) {
      held.delete(usage);
      report();
      held.add(usage);
      report();
    }
    return;
  }
  held.add(usage);
  report();
});
window.addEventListener("keyup", (event) => {
  if (event.code in modifierBits) {
    modifiers &= ~modifierBits[event.code];
    report();
  } else if (usages[event.code]) {
    held.delete(usages[event.code]);
    report();
  }
});
window.addEventListener("blur", releaseAll);
screen.addEventListener("blur", releaseAll);
input.addEventListener("blur", releaseAll);
input.addEventListener("compositionstart", () => (composing = true));
input.addEventListener("compositionend", (event) => {
  composing = false;
  compositionCommit = event.data;
  sendText(event.data);
  input.value = "";
  // Some engines emit a final input event after compositionend in this turn.
  setTimeout(() => (compositionCommit = null), 0);
});
input.addEventListener("input", (event) => {
  if (composing || event.isComposing) return;
  if (compositionCommit !== null && event.data === compositionCommit) {
    compositionCommit = null;
    input.value = "";
    return;
  }
  sendText(event.data || input.value);
  input.value = "";
});
input.addEventListener("beforeinput", (event) => {
  if (
    event.inputType === "insertLineBreak" ||
    event.inputType === "insertParagraph"
  ) {
    event.preventDefault();
    sendText("\n");
  }
  if (event.inputType === "deleteContentBackward") {
    event.preventDefault();
    pulse(42);
  }
});
function pulse(usage) {
  Module._leaf_report(0, usage, 0, 0, 0, 0, 0);
  Module._leaf_report(0, 0, 0, 0, 0, 0, 0);
}
document.getElementById("menu").onclick = () => {
  releaseAll();
  // Leaf opens its sidebar on Meta; this button avoids OS-reserved Meta keys.
  Module._leaf_report(8, 0, 0, 0, 0, 0, 0);
  Module._leaf_report(0, 0, 0, 0, 0, 0, 0);
  focusScreen();
};
document.getElementById("keyboard").onclick = () => {
  releaseAll();
  input.focus({ preventScroll: true });
};
async function save() {
  statusLine.textContent = "Saving…";
  if (!Module._leaf_save()) {
    fail("Document save failed. Your session is still open.");
    return false;
  }
  // An earlier snapshot may be in flight; follow it with a fresh sync.
  if (syncing) await syncing;
  const saved = await Module.syncStorage();
  return saved;
}
document.getElementById("save").onclick = async () => {
  const saved = await save();
  statusLine.textContent = saved
    ? "Saved in this browser."
    : "Could not persist. Changes are only in this session.";
  focusScreen();
};
document.getElementById("restart").onclick = async () => {
  if (await save()) location.reload();
  else
    fail(
      "Restart cancelled because local saving failed. Your current session is still open.",
    );
};
document.addEventListener("visibilitychange", () => {
  if (document.hidden && ready) {
    releaseAll();
    Module._leaf_save();
    Module.syncStorage();
  }
});
window.addEventListener("pagehide", () => {
  if (ready) {
    Module._leaf_save();
    Module.syncStorage();
  }
});
