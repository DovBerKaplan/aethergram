# Aethergram

Aethergram is an unofficial Telegram client: a fork of tdesktop (v7.4.5)
restyled for the Aether KDE desktop. It is not affiliated with Telegram.

## Building

One-time, after cloning and after every upstream sync (needs docker and
poetry, `pipx install poetry`):

    tools/prepare-image.sh        # JOBS=4 by default

It wraps upstream's `Telegram/build/prepare/linux.sh` recipe: no debug/LTO
image, every `make -j` pinned to `JOBS` (upstream uses all cores, which
starves a 16 GB laptop), and an expat upgrade the Rocky 8 image needs.

Then, per build (needs the `tdesktop:centos_env` docker image):

    tools/build.sh                 # JOBS=2 by default: single files need up to 5 GB of RAM
    CONFIG=Release JOBS=4 tools/build.sh   # only with plenty of free RAM
    tools/build.sh --test          # public test API credentials, no env file

## API credentials

Get your own `api_id` / `api_hash` at <https://my.telegram.org> and put them in
`~/.config/aethergram/api.env`:

    TG_API_ID=1234567
    TG_API_HASH=0123456789abcdef0123456789abcdef

Then `chmod 600 ~/.config/aethergram/api.env`. `tools/build.sh` refuses to read
the file unless its mode is 600 (or 400). The credentials are passed to cmake
only as `-D TDESKTOP_API_ID=... -D TDESKTOP_API_HASH=...`; they are never
printed and never committed — `/api.env` is gitignored. Override the location
with `AETHERGRAM_API_ENV`.

## Build notes

- API credentials compile into one file (`Telegram/SourceFiles/aether/aether_api.cpp`), so changing them rebuilds in minutes instead of triggering a full rebuild of all ~1230 sources.

## Importing a session

Aethergram can start up already logged in by importing an MTProto session
(auth key) produced by an outside tool (`tg-session-gateway`), for example
with `tools/import-gateway-session.py` (to be added), which writes the file
described below.

Usage: `tools/import-gateway-session.py --pyrogram-file ~/.pyrogram/main.session --source-is-stopped` — also `--telethon-file`, `--pyrogram-string`/`--telethon-string` (read from stdin), `--docker-volume NAME --session NAME`, plus `--gateway-url URL --alias ALIAS` (pauses tg-session-gateway) instead of `--source-is-stopped`; see `--help`.

On startup, if the account is **not** logged in yet, Aethergram looks for:

    ~/.config/aethergram/import-session.json

(`$XDG_CONFIG_HOME/aethergram/import-session.json`) with mode **600**, containing:

```json
{
  "dc_id": 2,
  "user_id": 123456789012,
  "auth_key": "<base64 of exactly 256 bytes>",
  "is_bot": false,
  "test_mode": false
}
```

`dc_id` must be an integer in 1..5, `user_id` a positive int64 (number or
decimal string — strings are safer, JSON numbers lose precision above 2^53),
`is_bot`/`test_mode` optional and must be false/absent: bots cannot use a
user client. Anything else is rejected.

The file holds a secret: Aethergram refuses to read it if group or other can
read or write it (`chmod 600`), never logs the key, and after reading it —
whether the import succeeded or not — overwrites it with zeros and deletes
it, so it can never be imported twice. Fix the permissions and restart to
retry after a refusal.

**Warning:** an auth key must only ever be used by one client at a time.
Pause `tg-session-gateway` (or whatever produced the key) *before* starting
Aethergram with it. If the key is used from two places simultaneously,
Telegram reports `AUTH_KEY_DUPLICATED` and may revoke the key.

## Branch model

* `upstream-dev` mirrors the upstream tdesktop release tag, no local commits.
* `aether` is a linear patch stack on top of it (rebase only, no merges).
* Releases are tagged `vX.Y.Z-aether.N`.

To sync with a new upstream release tag `vNEW` (previous one `vOLD`):

    git fetch upstream --tags
    git rebase --onto vNEW vOLD aether
    git submodule update --init --recursive

## Artwork TODO

The Linux app id is `io.github.dovberkaplan.Aethergram`, but the artwork shipped
under that name is still upstream Telegram artwork. These files must be replaced
with non-Telegram artwork before any public release (same pixel size, same file
name, so the CMake install rules and the qrc aliases keep working):

Installed as the hicolor app icon (`io.github.dovberkaplan.Aethergram.png`,
`Telegram/CMakeLists.txt`), used by snap/flatpak packaging too:

* `Telegram/Resources/art/icon16.png` and `icon16@2x.png`
* `Telegram/Resources/art/icon32.png` and `icon32@2x.png`
* `Telegram/Resources/art/icon48.png` and `icon48@2x.png`
* `Telegram/Resources/art/icon64.png` and `icon64@2x.png`
* `Telegram/Resources/art/icon128.png` and `icon128@2x.png`
* `Telegram/Resources/art/icon256.png` and `icon256@2x.png`
* `Telegram/Resources/art/icon512.png` and `icon512@2x.png`

Installed as the symbolic tray icons
(`io.github.dovberkaplan.Aethergram{,-attention,-mute}-symbolic.svg`):

* `Telegram/Resources/icons/tray_monochrome.svg`
* `Telegram/Resources/icons/tray_monochrome_attention.svg`
* `Telegram/Resources/icons/tray_monochrome_mute.svg`

Bundled in-app fallbacks (qrc `:/gui/art/...`), including the one copied to
`~/.local/share/icons/hicolor/256x256/apps/` on first run:

* `Telegram/Resources/art/logo_256.png`
* `Telegram/Resources/art/logo_256_no_margin.png`

Other Telegram-styled icons that are still shipped (not referenced by
`lib/xdg`, listed for completeness):

* `Telegram/Resources/art/icon_green.png`
* `Telegram/Resources/art/iconbig_green.png`
* `Telegram/Resources/art/icon_round512@2x.png`

## Aether palette

The Aether desktop (`system/aether-accent-sync`, `system/aether-mode-sync`)
writes its live palette to `~/.local/share/aethergram/aether.tdesktop-palette`,
replacing the file atomically on every change.

Aethergram applies this palette on start if the currently selected theme is a
built-in one or is already this file. While the app is running, upstream's
theme watcher re-applies the file live, so accent and light-dark switches made
in the Aether desktop show up immediately.

Opt out by passing `-noaethertheme`. Choosing another theme in Settings stops
following the Aether palette until you switch back to a built-in theme and
restart Aethergram.

## Language and direction

Aethergram follows the Aether desktop language (`[Interface] Language` in
`~/.config/aetherislandrc`, read only), but only when that value changes:
a language you pick in Aethergram's Settings stays until Aether changes
its own. Switching while logged in restarts the app once (upstream
behaviour). Hebrew (and other right-to-left languages) turn on the
right-to-left layout; `AETHERGRAM_LAYOUT=ltr|rtl` forces it for testing.
