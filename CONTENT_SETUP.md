# Downloadable content

Leaf supports removable dictionaries, bitmap fonts, color themes and packs. There
is no executable extension loader. Settings > Content browses a cached catalog,
installs/updates packages, enables/disables them, removes them, refreshes the
catalog and cancels downloads. Arrow keys select an action; Enter opens the item
dropdown or performs the action. Esc or backtick returns to the editor.

Exactly one dictionary, font family, downloaded theme and keyboard layout can be selected. No
selected/usable dictionary means spellcheck (including suggestions) is off.
English is a normal removable package, not a firmware fallback. Personal words
remain in `/spiffs/user_files/.spelling` when dictionaries are disabled/removed.
Downloaded fonts can also be selected in Appearance > Font. Missing sizes and
glyphs fall back to the built-in editor font. UI fonts remain built in. Installed themes appear in Appearance > Theme. Left/Right and Ctrl+T cycle
through built-in and installed themes; choosing a built-in clears the downloaded
theme selection.

## Storage and first provisioning

The new 16 MiB layout preserves NVS at 0x9000 and PHY data at 0xf000, moves the
single application to 0x10000 with 4 MiB capacity, and puts an 0xbf0000-byte
(11.9375 MiB) FAT/WL filesystem at 0x410000. Legacy SPIFFS and its migration code
are removed. The existing `/spiffs` VFS mount name is retained to avoid changing
all document, keyboard, backup and export paths; it now refers only to FAT.
Firmware updates are over USB; this layout has no OTA application slots.

**This is a fresh-filesystem layout. Do not flash it over the previous layout
expecting documents to migrate.** Export any wanted documents before provisioning.
Normal `idf.py flash` writes firmware and partition metadata, not a filesystem
image. A nonblank unmountable filesystem is never silently formatted.

In an activated ESP-IDF PowerShell, from the repository root:

```powershell
./tools/content/provision.ps1
```

This only builds `build/content/documents.bin`. It neither flashes nor erases.
To provision a connected device deliberately, first flash the new firmware, then
write this **fresh filesystem image** with:

```powershell
python -m esptool --chip esp32s3 --port COM6 --baud 460800 write_flash 0x410000 build/content/documents.bin
```

The seed installs the existing 49,663-word English dictionary and selects
English on first initialization. Ink and other optional themes are downloadable. The English starter pack is
available in the catalog but is not installed by default. Subsequent
boots use the installed registry; uninstalling English does not restore it from
the seed. Devices initialized without a seed start with spellcheck off.

## API contract for justtypeleaf.com

The firmware uses certificate-validated HTTPS at:

* `GET /api/leaf/content/v1/catalog`: JSON schema 1, maximum 64 KiB and 64 items.
* `GET /api/leaf/content/v1/files/<id>/<version>/<filename>`: exact asset bytes.

No Google account/token or device credential is sent to this API. It is a trusted
first-party public catalog. HTTPS authenticates the service; SHA-256 from the
catalog verifies payload integrity. API responses must return 200 directly;
cross-origin/HTTP redirects are not followed. Return correct Content-Length if
present; chunked responses are supported. Files and version IDs are immutable.
Publish a new version whenever an asset or its metadata changes.

Generate publishing fixtures with:

```powershell
python tools/content/build_packages.py
```

Output: `build/content/catalog.json`, `build/content/files/`, the device seed and
an English redistribution notice. The license notice must accompany published
English content. The output is build data and is not committed.

An API function is optional: Vercel can rewrite these API URLs onto static files
under your site's `public/leaf-content/`. Copy the generated catalog/files and
English license there, and merge these rewrites into the site's configuration
before any application catch-all:

```json
{
  "rewrites": [
    { "source": "/api/leaf/content/v1/catalog", "destination": "/leaf-content/catalog.json" },
    { "source": "/api/leaf/content/v1/files/:id/:version/:file", "destination": "/leaf-content/files/:id/:version/:file" }
  ]
}
```

Serve the catalog with revalidation/short caching and versioned assets with long
immutable caching. Upload versioned files first, then update the catalog. Keep
old versions available for packs that pin them. A browser hitting Vercel login
protection is not a usable firmware API: these routes must be publicly readable.
The actual site endpoint/deployment is not created by building ESP firmware.

Catalog example (the builder supplies real sizes/hashes):

```json
{
  "schema": 1,
  "packages": [{
    "id": "org.justtype.dictionary.es",
    "version": "1.0.0",
    "format": 1,
    "type": "dictionary",
    "name": "Spanish",
    "language": "es",
    "description": "Spanish word-existence spellcheck",
    "requires": [],
    "files": [{ "name": "dictionary.bin", "size": 1234, "sha256": "<64 lowercase hex characters>" }]
  }]
}
```

`schema` versions the catalog/registry protocol; `format` versions the decoder
capability for each package type. This firmware accepts only version 1 of each.
Unknown fields are optional; incompatible schema/format/type/language is rejected.
There is no firmware semantic-version comparison or executable package support.

## Building more content

Supply `--spec path/to/packages.json` to the builder. Paths are relative to the
specification, for example:

```json
{
  "packages": [
    { "id": "org.justtype.dictionary.es", "version": "1.0.0", "type": "dictionary", "name": "Spanish", "language": "es", "wordlist": "spanish.txt" },
    { "id": "org.justtype.font.example", "version": "1.0.0", "type": "font", "name": "Example font", "sources": { "24.bin": "example-24.bin" } },
    { "id": "org.justtype.theme.paper", "version": "1.0.0", "type": "theme", "name": "Paper", "sources": { "theme.json": "paper.json" } },
    { "id": "org.justtype.pack.spanish", "version": "1.0.0", "type": "pack", "name": "Spanish pack", "requires": ["org.justtype.dictionary.es@1.0.0", "org.justtype.font.example@1.0.0"] }
  ]
}
```

Dictionary languages are `en`, `es`, and `ru`. Custom word lists are UTF-8, one
word per line, normalized/sorted offline; maximum 32 characters/96 bytes per
word, 100,000 words and 1 MiB per file. `LTXT\n` prefixes the sorted text format.
English's existing compact `LDWG` binary format is supported separately. Runtime
case folding handles ASCII, Spanish precomposed accented Latin letters and
Russian (including Ё). Decomposed accents and more general Unicode casing are
not supported by this first format. Suggestions use bounded edit-distance work;
large text word lists take longer than the compact English automaton.
Words in scripts unsupported by the selected language are skipped.
No Spanish/Russian vocabulary is bundled; provide licensed source word lists.

Fonts are LVGL 9.2.2 binary bitmap files, named `<pixel-size>.bin` (14..40), each
at most 128 KiB. Generate with `lv_font_conv --format bin --no-compress
--no-kerning`, selecting licensed fonts and the required character ranges. This
release accepts only bounded, uncompressed, unkerned fonts; every table/glyph is
checked before passing the file to LVGL. At most 2,048 glyphs and 32 cmap tables
are accepted. One selected editor font size is loaded into RAM on demand.
International fallback sizes are loaded lazily as UI/editor sizes are used and
remain cached until support is removed. Active dictionary data/index also uses
PSRAM; unused dictionaries consume storage only.

Themes are JSON objects mapping existing `Theme` field names to `#RRGGBB` values;
at most 4 KiB. Unspecified fields inherit Dark. For example:

```json
{ "edit_bg": "#fff5df", "edit_text": "#332b23", "cursor": "#7a4020" }
```

Only supported color properties are applied. Image backgrounds, arbitrary CSS,
inline rich-text styling and font effects are not part of this release.

Packs contain no payload and layouts may also declare pinned `id@version` dependencies. Installing a
pack records ownership of its dependency set. Removal retains items installed
independently or needed by another pack. Updates that conflict with another
pack's pinned version are rejected. One version of a package can be installed.

## Reliability and performance

The existing network worker serializes content operations and Google backup,
reusing saved Wi-Fi credentials and secure clock setup. No catalog check occurs
at boot and no periodic content polling is added. The last valid catalog is
cached for offline browsing. Refresh/install need Wi-Fi; enable/disable/remove do
not. Errors retain the old installation and can be retried explicitly.

Downloads use a 2 KiB buffer, SHA-256 while writing, timeouts, a three-minute
per-file deadline, a four-minute overall radio budget, up to three transient-error attempts and cancellation. Staging is outside `user_files`, so content
does not appear in document export or Drive backups. At least 1 MiB of shared
space is reserved during installation for document saves/backup staging; a larger
backup snapshot can still require more free space than this reserve.

The registry has two generation-numbered slots with CRC checks. A transaction
journal allows interrupted install files to be cleaned up on the next boot.
The old registry/version is retained until a complete installation is verified.
Known old asset paths are removed only after commit; no package controls a path
outside its ID/version directory. These measures do not make FAT power-loss
atomic or isolate documents from physical flash/filesystem failure.

## Verification

`tools/content_tests/run.cmd` builds the host package-manager tests with VS and
the local ESP-IDF cJSON source. First generate `build/content-test-font.bin` with
a licensed local TTF and the font converter as described above (24 px, ASCII).
Tests cover removable English, persistence across a fresh process, one active
dictionary, Russian UTF-8, packs, interrupted/cancelled installs, metadata bounds,
path traversal and binary font checks. Firmware and simulator use the same
package manager. The browser simulator fetches the same-origin API asynchronously,
checks SHA-256 with Web Crypto, then installs through that package manager.
Settings stays open during transfers, with typing/paste discarded. Escape,
backtick or Cancel stops the transfer without closing Settings. Completion also
leaves Settings open. Downloads stream to temporary files; hashing uses a bounded buffer for one asset
(up to 1 MiB). Browser content shares IndexedDB with documents and is constrained
by an approximate Leaf-sized filesystem budget, the document reserve and browser
quota. HTTPS or localhost is required. Local previews need the API served or
proxied at `/api/leaf/content/v1/` on their own origin.

## Downloadable keyboard layouts and themes

US QWERTY and the Light/Dark themes are built in. Dvorak, right-hand Dvorak,
Spanish and Russian are versioned `layout` packages. Install them from Content
and select one in Settings > Keyboard. Removing the selected layout returns Leaf
to US QWERTY. Spanish and Russian install the shared international glyph package;
removing the last dependent layout removes it. Existing Spanish/Russian 1.0
installs need updating to 1.1 to install glyph support. Without the package,
foreign text stays intact but glyphs may appear as placeholders.
Layout packages contain a bounded, validated `layout.json` with format version 1,
two arrays of 49 Unicode code points in Leaf HID row order, and the Spanish
dead-key/AltGr flag. Existing layout files are generated from xkeyboard-config
input mappings and shipped in `tools/content/layouts/`.

Ocean, Forest and Sunset are removable theme packages in the catalog. Ink is
also removable; Light and Dark remain available without downloads. Existing
editor state that referenced a formerly built-in optional theme falls back to
Dark until a downloaded theme is selected. Existing keyboard shortcut bindings
are preserved; an old selection for a now-optional keyboard falls back to US
until its layout is installed and selected.

The editor offers 16, 20, 24 and 28 px. Older saved sizes map to the nearest
choice (ties select the smaller size), while stored legacy indices remain compatible.
