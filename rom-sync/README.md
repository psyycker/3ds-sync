# rom-sync

A standalone 3DS homebrew app for pulling arbitrary files/folders down from
Google Drive to the SD card. Unlike `save-sync` (the Checkpoint fork, which
manages save-file backups specifically), rom-sync manages generic **sync
channels**: a Drive path paired with a local SD card path. Each channel can
point at a single file or a whole folder (synced recursively).

Built on top of the HTTPS + JSON groundwork proved out in
`../save-sync/spike-drive-api`, plus:

- `auth.c` - authenticates as a **service account** (the same one
  `save-sync/checkpoint` uses) by building and RS256-signing a JWT with the
  service account's private key and exchanging it for an access token
  (JWT-bearer grant, via mbedtls). No interactive sign-in, no refresh token
  to persist - it just re-signs on every launch.
- `https_download_to_file` (`source/https.c`) - streams a response body
  straight to disk instead of buffering it in RAM, since ROMs/zips can be far
  larger than the spike's in-memory response buffer.
- `drive.c` - resolves a `/`-separated Drive path to a file/folder ID by
  walking one segment at a time, and recursively downloads folder contents.
- `channels.c` - loads/saves the list of configured sync channels to
  `sdmc:/3ds/rom-sync/channels.cfg`.

The Drive scope requested is `drive.readonly` - rom-sync only ever reads.
Note that a service account has **no access to your Drive by default**: you
must explicitly share whatever folders/files you want to sync with its
client email address (Drive web UI -> right-click -> Share) - same
requirement as save-sync.

## Setup

1. If you already set up a service account for `save-sync`, reuse it: copy
   `save-sync/checkpoint/3ds/include/DriveServiceAccount.hpp` to
   `include/DriveServiceAccount.h` (same file format, only the extension
   differs). Otherwise copy `include/DriveServiceAccount.h.example` to
   `include/DriveServiceAccount.h` and fill in a new service account's
   fields from its downloaded JSON key.
2. Make sure the service account's client email has been shared on whatever
   Drive folders/files you plan to sync.
3. `make` (requires devkitARM / devkitPro with `libctru`, `mbedtls`,
   `mbedx509`, `mbedcrypto` - install the latter three via
   `dkp-pacman -S 3ds-mbedtls` if you don't already have them from building
   `save-sync/checkpoint`).
4. Copy `rom-sync.3dsx` to `/3ds/` on your SD card and launch it via the
   Homebrew Launcher.

## Using it

Each launch, rom-sync signs in as the service account automatically (no
prompts) - the access token it gets is only good for an hour, so it's
refreshed on every run.

From the main menu:

- **Add new channel** - give it a name, a Drive path (e.g.
  `Games/3DS/homebrew`), and a local SD path (e.g. `sdmc:/roms/3ds`).
- Select a channel + **A** to sync just that one; **Sync all channels now**
  runs every configured channel in sequence.
- **X** on a selected channel deletes it (with a confirmation prompt).

## Known limitations (first pass)

- Folder listing only fetches the first 100 entries per folder (no
  pagination yet) - fine for most folders, but very large ones will be
  truncated.
- Sync is always a fresh full download; there's no diffing/skip-if-unchanged
  yet, so re-syncing re-downloads everything.
