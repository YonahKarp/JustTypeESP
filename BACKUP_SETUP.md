# Leaf Google Drive backup

Leaf backs up saved UTF-8 `.txt` documents to a dedicated `Leaf Backup - XXXXXX`
folder in your Google Drive. Subfolders are preserved. This is one-way backup:
Drive content never replaces a local document. Local deletions and renames do not
delete older Drive copies. Hidden files, application state and `recently_deleted`
are excluded.

## 1. Create your Google OAuth client

1. Open [Google Cloud Console](https://console.cloud.google.com/) and create or
   select a project.
2. Enable **Google Drive API** in **APIs & Services → Library**.
3. Configure Google Auth Platform branding, audience and contact information. For
   a personal prototype, use a testing audience and add your Google account as a
   test user. Organization administrators may restrict authorization.
4. In Data Access, add only
   `https://www.googleapis.com/auth/drive.file`. Leaf requests this scope, which
   limits access to files created or authorized for this app.
5. Create an OAuth client with application type **TVs and Limited Input devices**.
   Save its client ID and client secret locally. A web or desktop client does not
   support this device authorization flow.

See Google's [device authorization instructions](https://developers.google.com/identity/protocols/oauth2/limited-input-device)
and [credential setup](https://developers.google.com/workspace/guides/create-credentials).
Google's testing and account policies can require reauthorization; Leaf reports
this in Backup settings instead of discarding documents.

## 2. Configure and build Leaf

From the `JustTypeESP` directory, copy the template once:

```powershell
Copy-Item main/backup/backup_secrets.example.h main/backup/backup_secrets.h
```

Edit the two quoted values in that local header:

```cpp
#define LEAF_GOOGLE_CLIENT_ID "YOUR_CLIENT_ID.apps.googleusercontent.com"
#define LEAF_GOOGLE_CLIENT_SECRET "YOUR_CLIENT_SECRET"
```

`backup_secrets.h` is ignored by Git. Keep it and build artifacts containing it
private. The Google client credentials identify the application; they are not
your Google password and do not replace protecting each user's refresh token.

```powershell
. C:/Users/karpy/esp/esp-idf-v5.5.6/export.ps1
idf.py reconfigure
idf.py build
```

The normal firmware flashing procedure installs this build. Building without the
local header is supported: Backup settings will explain that OAuth setup is
missing. Do not paste credentials into chat or commit them.

## 3. Connect from Leaf

1. Open the sidebar gear, then the **Backup** tab. Tab switches settings tabs;
   arrows select rows and Enter activates them. Escape or backtick closes.
2. Choose **Wi-Fi**, enter a 2.4 GHz SSID and password, then Enter to save and
   test the connection. Tab switches fields. The form stays open and reports
   connection success or an initialization, authentication, network-not-found,
   or DHCP error. A successful test confirms Wi-Fi association and an assigned
   IP address; Google/TLS access is checked when linking or backing up.
   Saved passwords are loaded back into the masked field when the form reopens.
   An empty password explicitly selects an open network. Credentials are saved
   even if the router is offline, so you can retry without retyping them.
   Captive portals and enterprise Wi-Fi are not supported.
3. Choose **Google Drive: Link with your phone**. Scan the QR code with your
   phone, then enter the authorization code displayed beside it. The exact URL
   also remains visible for manual entry. Approve access using your Google
   account. Leaf never asks for your Google password.
4. Leave Leaf awake while signing in. Codes expire; choose Link again if needed.
   Escape, backtick or Close dismisses the QR popup while sign-in continues.
   The popup closes automatically when sign-in finishes or expires. Closing
   Settings leaves sign-in running; **Forget Google account** cancels it.
5. Successful linking enables automatic backup. Choose **Back up now**, or press
   **Ctrl+B**, to push changed content. The shortcut can be changed on the
   Keyboard tab. **Verify backup copies** explicitly checks all documents
   against Drive and repairs missing or externally changed copies.

The Backup page displays connection, authorization, progress, retry and error
messages. **Forget Google account** removes Leaf's local authorization without
deleting documents or Drive backups. To revoke Google's authorization as well,
remove the app from your Google account's connected applications.

## Credential storage and this prototype

**Wi-Fi credentials and Google OAuth refresh tokens are saved in ordinary NVS
and restored after reboot**, as explicitly accepted for this at-home prototype.
Writes are committed before reporting that credentials were saved. The current
prototype has neither flash encryption nor encrypted NVS enabled, so stored
credentials can be physically extracted. Access tokens remain in RAM and are
renewed using the stored refresh token. Credentials are never intentionally
logged. Forget Google account removes the stored refresh token.

The `leaf_backup` NVS namespace and standard NVS APIs are used for both ordinary
and encrypted storage. Enabling properly provisioned encrypted NVS for production
does not require changing the backup credential format or save/load logic.
Existing plaintext credentials must be securely removed or reprovisioned during
that transition; enabling encryption is not an in-place migration of old NVS.

For production, provision encrypted NVS with protected encryption keys, flash
encryption, and an appropriate boot/debug policy. The default partition table
has no `nvs_keys` partition. Follow Espressif's
[NVS encryption guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/storage/nvs_encryption.html)
and [flash encryption guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/security/flash-encryption.html)
when preparing production hardware. This feature does not change partitions,
enable encryption or burn eFuses on the attached prototype.

HTTPS uses certificate and hostname verification with the ESP-IDF certificate
bundle. Leaf synchronizes its clock before the first HTTPS connection after
boot. If time synchronization fails, backup fails safely; TLS verification is
never disabled. HTTP redirects are disabled and requests are restricted to the
Google OAuth and Drive API hosts.

## Scheduling, incremental updates and failures

- Automatic backup checks saved documents every 30 minutes while awake, after
  at least 30 seconds without keyboard activity. It does not wake a sleeping
  device. Manual backup first saves the current editor document.
- Persistent per-file dirty records in `.backup_dirty` identify saved edits.
  Normal automatic and manual backups inspect file metadata and dirty flags;
  clean documents are not read, hashed or checked on Drive. SHA-256 comparison
  skips dirty files whose content has reverted to the last backed-up version.
  If no content changed, Wi-Fi stays off. New, renamed and restored documents
  are included. Obsolete dirty records for deleted paths are pruned.
- Dirty records are committed before document replacement. If that journal
  cannot be written, the document replacement is postponed and the editor
  retains its pending save; the prior saved document is preserved. Repeated
  saves of an already dirty document do not append more journal entries.
  Interrupted/corrupt journals trigger conservative reconciliation. The first
  backup after upgrading from the previous index format rechecks local hashes
  once, then subsequent backups use the persistent dirty state.
- **Verify backup copies** is the explicit full Drive check. Normal backups no
  longer automatically audit every file after reboot or daily. Drive's checksum
  avoids uploading matching content; missing or externally changed copies are
  repaired during verification or the next changed-file upload. Drive edits
  are never downloaded. Device-side edits remain the source of truth.
- Normal network sessions have a two-minute budget, then Wi-Fi stops. Large
  batches resume from per-file checkpoints on subsequent attempts. Device
  authorization can keep Wi-Fi active for up to ten minutes. Wi-Fi modem power
  saving is enabled while connected.
  HTTPS connections are reused within a session and released before Wi-Fi stops;
  API calls no longer repeat a TLS handshake for every file operation. Drive IDs
  are obtained in batches of 32, and newly allocated IDs are saved before create
  without an unnecessary existence lookup. A session reaching its budget reports
  a resumable pause and the completed-file count, rather than a network error.
- Offline and transient errors retry with exponential backoff from about one
  minute to one hour, plus jitter. Server throttling is respected. Manual backup
  retries immediately. Automatic retry requires Automatic backup to be enabled.
  Permission/configuration failures disable automatic attempts and show a
  message; correct the issue, relink if requested, and enable automatic backup.
- Drive file IDs are allocated and saved before creation to avoid duplicate
  copies when a network response is lost. Metadata is checked before updating
  an existing item. Files are uploaded atomically as complete text payloads;
  Leaf never creates an empty placeholder before uploading document content.
- The worker performs networking outside the LVGL task. The UI task services
  filesystem requests in bounded slices. One changed document is staged in the
  hidden `.backup_snapshot` file using 2 KiB copy slices. SHA-256/MD5 hashing and
  upload then stream that immutable snapshot in buffers of at most 4 KiB. No
  document-sized RAM allocation is used. The snapshot is deleted after use and
  discarded on reboot; originals are never replaced by it.
  Snapshot creation is canceled before a source mutation. Once the snapshot is
  complete, typing/autosaves can continue during hashing and upload. A dirty
  flag is cleared only after checksum verification, successful index storage
  and a matching local version. Newer edits remain queued for another backup.
  Temporary flash space for one document is required in addition to normal
  document/autosave storage. A staging-space failure leaves originals intact
  and the document dirty for retry.
  While storage work is pending, the UI waits at most 1 ms before the next slice;
  input processing still runs first.

Current bounds: 2,048 documents per scan, 16 nested folder levels, 480-byte
relative paths, 4 MiB per staged snapshot, 4,096 indexed items and a 512 KiB serialized
index. Allocation, capacity and storage errors are reported without truncating
documents. The editor's existing document-size limit still applies. No RAM
buffer is reserved for the maximum document size. Index metadata is still
allocated on demand; document hash/upload buffers are fixed-size.

## Validation

```powershell
cmd /c tools\keyboard_tests\run.cmd
cmd /c tools\backup_tests\run.cmd
idf.py build
```

Host tests cover path/host restrictions, input validation, polling/backoff,
keyboard shortcut defaults, and the actual filesystem handoff implementation:
nested scans, filtering, empty/large staged files, cancellation, dirty-editor
rejection, immutable chunk reads while editing, and failed index commits.
Dirty-journal tests cover reboot persistence, stale-version rejection, UTF-8,
torn records, compaction and repeated-save coalescing. Transport tests exercise
bounded multipart streaming, connection reuse, credential isolation and failures.

USB serial diagnostics can trigger `JTWIFITEST` followed by a newline to test
the already configured credentials without exposing their values. Results are
logged under `leaf_wifi`.
`JTBACKUP` followed by a newline requests a normal manual backup. Sanitized
`leaf_backup` logs report request latency, HTTP status, TLS connection count and
completed-file count without printing tokens, document contents or file names.

Wi-Fi/LwIP prefer PSRAM for ordinary allocations, while DMA buffers stay
internal; the driver uses bounded buffer counts to
coexist with the RGB display and Bluetooth keyboard.
Ordinary allocations above 1 KiB, dynamic Bluetooth host data and TLS buffers
prefer PSRAM. Wi-Fi's optional IRAM speed optimizations are disabled to recover
internal SRAM for simultaneous display, Bluetooth and Wi-Fi operation. Task
stacks and DMA allocations remain internal.
The RGB display uses two 12-line bounce buffers, preserving 25.6 KiB more DMA
memory than the former 20-line buffers for simultaneous Wi-Fi operation.

Before relying on the backup, use a configured client and the physical device to
check phone authorization, UTF-8 files and subfolders, unchanged backup, edited
files, offline retries, restarting mid-upload, deleting a Drive copy and pressing
Back up now, and typing during a backup. Live Google authorization and uploads
cannot be verified by the build or host tests alone.
