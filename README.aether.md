# Aethergram

Aethergram is an unofficial Telegram client: a fork of tdesktop (v7.4.5)
restyled for the Aether KDE desktop. It is not affiliated with Telegram.

## Building

One-time, after cloning and after every upstream sync:

    Telegram/build/prepare/linux.sh

Recommended on a 16 GB RAM machine: `DEBUG= LTO= JOBS=6 ./Telegram/build/prepare/linux.sh`
(run as `bash Telegram/build/prepare/linux.sh` with those env vars set).

Then, per build (needs the `tdesktop:centos_env` docker image):

    tools/build.sh                 # CONFIG=Release JOBS=6 is a good default
    CONFIG=Release JOBS=8 tools/build.sh
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
