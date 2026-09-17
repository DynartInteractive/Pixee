# Pixee 0.4.0

Linux packaging. Pixee can now be built and installed as a **Flatpak**, which
brings with it the desktop integration it never had on Linux: an app icon, a
launcher entry, and metadata for software centres.

This is a quiet release if you're on Windows — the only thing you'll notice is
that the window finally has an icon. The full feature and keyboard list lives in
the [README](../README.md).

## Flatpak

```sh
flatpak install flathub org.kde.Platform//6.11 org.kde.Sdk//6.11 org.flatpak.Builder
flatpak run org.flatpak.Builder --user --install --force-clean \
    build packaging/net.dynart.Pixee.yml
flatpak run net.dynart.Pixee
```

The manifest builds from the working tree, so you don't need a tag or even a
commit to try one.

**Pixee is not on Flathub yet.** The packaging is done and verified, but
publishing needs a few things that aren't code — see
[`docs/flathub-requirements.md`](flathub-requirements.md).

### It reads more formats than the native build

The Flatpak runs on the KDE runtime, which bundles kimageformats. So it opens
**HEIC/HEIF, AVIF, JPEG XL, PSD/PSB, XCF, KRA, ORA, DDS, QOI and SVG** with no
extra work — the plugin juggling the Windows build needs
([`docs/windows-extra-image-formats.md`](windows-extra-image-formats.md)) has no
equivalent here. It's the single biggest practical difference between the two.

### What the sandbox changes

A Flatpak runs confined, and three things follow from that:

- **Pixee asks for full filesystem access**, because it's a file manager for
  images — it browses your folders with its own tree and moves files between
  them. The alternative sandbox mechanism hands over one file at a time, which
  can't express that.
- **It keeps its own data** under `~/.var/app/net.dynart.Pixee/`, separate from
  a native install's.
- **"Open with"** uses your desktop's application chooser instead of Pixee's own
  configurable program list. Inside a sandbox the programs on that list aren't
  reachable, so the desktop has to make the handoff.

## The Linux data folder moved

Pixee's thumbnail cache and user themes now live in the standard location —
**`~/.local/share/Dynart/Pixee`** instead of `~/.pixee`.

You don't have to do anything: your existing cache is carried across the first
time you run 0.4.0, so you keep the thumbnails you already have, and a user
theme left in `~/.pixee/themes/` still works.

Windows is unchanged — it keeps `~/.pixee`, where existing installs already
look.

The reason for the split is the Flatpak. It's granted access to your real home
so it can browse it, which means a path built from "your home folder" would
write into your actual home rather than the app's own storage. The standard
location resolves correctly in both cases.

## Installing from source on Linux

`make install` now follows the normal freedesktop layout under a configurable
prefix, so a build lands where a Linux system expects it — binary in `bin/`,
themes in `share/pixee/`, and the icon, launcher entry and metadata in their
usual places:

```sh
qmake PREFIX=/usr/local Pixee.pro
make
sudo make install
```

Previously the only install rule put the binary in `/opt/Pixee/bin` and left the
themes behind entirely.

## For contributors

- [`docs/flatpak.md`](flatpak.md) — the technical reference: what the sandbox
  changes about the app, which permissions it asks for and why, which linter
  errors are expected, and how to cut a release Flathub can build.
- [`docs/flathub-requirements.md`](flathub-requirements.md) — the submission
  checklist.
