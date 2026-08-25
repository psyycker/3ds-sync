# rom-sync

A standalone 3DS homebrew app for pulling arbitrary files/folders down from
Google Drive to the SD card. Unlike `save-sync` (the Checkpoint fork, which
manages save-file backups specifically), rom-sync manages generic **sync
channels**: a Drive path paired with a local SD card path. Each channel can
point at a single file or a whole folder (synced recursively).

Built on top of the OAuth device-flow + HTTPS + JSON groundwork proved out in
`../save-sync/spike-drive-api`, plus:

- `https_download_to_file` (`source/https.c`) - streams a response body
  straight to disk instead of buffering it in RAM, since ROMs/zips can be far
  larger than the spike's in-memory response buffer.
- `drive.c` - resolves a `/`-separated Drive path to a file/folder ID by
  walking one segment at a time, and recursively downloads folder contents.
- `channels.c` - loads/saves the list of configured sync channels to
  `sdmc:/3ds/rom-sync/channels.cfg`.

Note the OAuth scope is `drive.readonly`, not `drive.file` - rom-sync needs
to see files you already have in Drive by path, not just files the app
itself created.

## Setup

1. Copy `include/config.h.example` to `include/config.h` and fill in a
   Google Cloud OAuth client ID/secret (type "TVs and Limited Input
   devices"), same as the other apps in this repo.
2. `make` (requires devkitARM / devkitPro with `libctru`).
3. Copy `rom-sync.3dsx` to `/3ds/` on your SD card and launch it via the
   Homebrew Launcher.

## Using it

On first launch you'll be walked through the OAuth device flow (a URL + code
to enter on another device); after that a refresh token is saved to
`sdmc:/3ds/rom-sync/refresh_token.txt` so you won't need to sign in again.

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
