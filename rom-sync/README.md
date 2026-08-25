# rom-sync

A standalone 3DS homebrew app for syncing specific files down from Google
Drive to the SD card. Unlike `save-sync` (the Checkpoint fork, which manages
save-file backups specifically), rom-sync manages generic **sync channels**:
one Drive file, browsed and picked (not typed), synced into one destination
folder on the SD card under its original Drive filename.

This is a graphical, touch-capable citro2d app - a lightweight sibling of
`save-sync/checkpoint` reusing several of its framework pieces directly
(`Screen`/`Overlay`, `ListPickerOverlay`, `ModalChrome`, `clickable`, `gui`,
`TextPool`, `KeyboardManager`, the sprite atlas) plus its Drive networking
(`DriveApi`, `DriveAuth`, `httpcall`, vendored `json.hpp`) copied over
unmodified where possible. Pieces that were too entangled with Checkpoint's
save-manager app (its `util.hpp`/`Configuration`/`Archive` grab-bag) were
replaced with small standalone equivalents instead: `stringutils.*`,
`localfs.*`, `logging.*`, `httpcall.*`. See the header comments on those
files for exactly what each one trims and why.

**Caveat:** this was built and verified to compile/link cleanly with
devkitARM, but has not been run on real hardware or an emulator - the actual
on-screen look and touch behavior are unverified. Try it and report back
anything that looks wrong.

## How it works

- **Sign-in**: a **service account** (the same one `save-sync/checkpoint`
  uses) - `DriveAuth` builds and RS256-signs a JWT with its private key and
  exchanges it for an access token (JWT-bearer grant, via mbedtls). No
  interactive sign-in, no refresh token to persist; it just re-signs on every
  launch. A service account has **no access to your Drive by default**: you
  must explicitly share whatever files you want to sync with its client
  email address (Drive web UI -> right-click -> Share) - same requirement as
  save-sync.
- **Add channel**: opens `DriveFilePickerOverlay` (ported from checkpoint) to
  browse your Drive and pick a file - **A** opens a folder / picks a file,
  **X** creates a new empty file here, **B** goes up a level (or cancels at
  the root). Picking a file then opens `FolderBrowserOverlay` to browse the
  SD card and pick a destination folder - **A** descends, **X** uses the
  current folder, **B** goes up/cancels. The channel is saved as
  `{driveFileId, driveFileName, localFolder}`; the local filename always
  matches the Drive filename, so there's nothing left to type.
- **Sync**: selecting a channel + **A** downloads just that file; "Sync all
  channels" downloads every configured channel in sequence. **X** on a
  selected channel deletes it (no confirmation prompt).

Channels persist to `sdmc:/3ds/rom-sync/channels.cfg`.

## Setup

1. If you already set up a service account for `save-sync`, reuse it: copy
   `save-sync/checkpoint/3ds/include/DriveServiceAccount.hpp` to
   `include/DriveServiceAccount.hpp` (identical file format). Otherwise copy
   `include/DriveServiceAccount.hpp.example` to
   `include/DriveServiceAccount.hpp` and fill in a new service account's
   fields from its downloaded JSON key.
2. Make sure the service account's client email has been shared on whatever
   Drive files you plan to sync.
3. `make` (requires devkitARM/devkitPro with `libctru`, `citro2d`, `citro3d`,
   `curl`, `mbedtls`, `mbedx509`, `mbedcrypto`, `zlib` - the same portlibs
   `save-sync/checkpoint` needs; `(dkp-)pacman -S 3ds-curl 3ds-mbedtls
   3ds-zlib citro2d citro3d` if you don't already have them).
4. Copy `rom-sync.3dsx` to `/3ds/` on your SD card and launch it via the
   Homebrew Launcher.

## Known limitations (first pass)

- Downloads buffer the whole file in RAM before writing it to disk (matches
  how checkpoint's own `DriveApi::downloadFile` works) - fine for typical
  ROM sizes, but a very large file could exhaust available heap on an Old
  3DS. A streaming download is possible but not implemented yet.
- No diffing/skip-if-unchanged: re-syncing a channel always re-downloads the
  file, even if it hasn't changed on Drive.
- `FolderBrowserOverlay` can only pick a folder that already exists - there's
  no "create folder" option in the local picker (only `DriveFilePickerOverlay`
  has one, for Drive-side files), so create the destination folder ahead of
  time via a file manager or FTP if it doesn't exist yet.
- Drive folder listings cap at 200 entries (checkpoint's `DriveApi::listChildren`
  default) with no pagination - only matters if you're browsing an
  enormous folder to find the file to pick.
