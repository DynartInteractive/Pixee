# Flatpak & Flathub

Pixee's Linux packaging lives in `packaging/`:

| File | What it is |
|---|---|
| `net.dynart.Pixee.yml` | The Flatpak manifest |
| `net.dynart.Pixee.desktop` | Desktop entry (launcher, MIME associations) |
| `net.dynart.Pixee.metainfo.xml` | AppStream metadata — the app-store listing |

The icon is `resources/icons/net.dynart.Pixee.svg`, which is both the installed
hicolor icon and (via `resources.qrc`, as `:/icons/app.svg`) the window icon.

The app ID is **`net.dynart.Pixee`**. Flathub requires the ID to be a domain you
control, so `dynart.net` must serve something recognisably about Pixee — a
project page is enough. Get that up before submitting; it is the one prerequisite
that isn't in this repo.

**This file is the technical reference.** For the ordered list of things a person
has to do to get Pixee published — the website page, the release, the PR, the
permission exception — see [`docs/flathub-requirements.md`](flathub-requirements.md).

## Building locally

```sh
flatpak install flathub org.kde.Platform//6.11 org.kde.Sdk//6.11
flatpak install flathub org.flatpak.Builder

flatpak run org.flatpak.Builder --user --install --force-clean \
    build packaging/net.dynart.Pixee.yml
flatpak run net.dynart.Pixee
```

The manifest's `sources` is a `type: dir` pointing at the repo root, so a local
build packages your working tree — no commit or tag needed while iterating.

Lint before submitting:

```sh
flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
    manifest packaging/net.dynart.Pixee.yml
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
appstreamcli validate packaging/net.dynart.Pixee.metainfo.xml
desktop-file-validate packaging/net.dynart.Pixee.desktop
```

### The lint output you should expect

Three linter errors are known and expected here. Don't "fix" them:

- **`finish-args-host-filesystem-access`** — `--filesystem=host`. This is the one
  real gate. Flathub treats broad filesystem access as needing a **manually
  granted exception**: you ask for it in the submission PR, explaining why the
  app can't work through the document portal, and a reviewer grants it against
  the app ID. Until it is granted the linter keeps failing, which is by design —
  it is a review conversation, not a config flag. The argument for Pixee: it is a
  file manager for images, with its own folder tree, copying and moving files
  between arbitrary directories; the portal's one-file-at-a-time model cannot
  express that.
- **`appstream-screenshots-not-mirrored-in-ostree`** and
  **`appstream-external-screenshot-url`** — these only appear in a *local* build.
  Flathub's buildbot downloads the screenshots, mirrors them to
  `dl.flathub.org/media` and rewrites the URLs; that step doesn't run on your
  machine, so the check can never pass locally. They disappear on the buildbot.

A `runtime-update-available` warning means a newer KDE runtime has shipped —
bump `runtime-version` in the manifest and rebuild. Reviewers prefer the latest.

## What the sandbox changes about the app

Four things behave differently inside the sandbox, and the code already accounts
for all of them:

- **Data location.** `Config::userFolder()` uses `QStandardPaths::AppDataLocation`
  on Linux rather than `~/.pixee`, so the thumbnail DB lands in the app's own
  data dir (`~/.var/app/net.dynart.Pixee/data/...`) instead of the user's real
  home — which `--filesystem=host` would otherwise expose to it. An existing
  `~/.pixee/thumbnails.s3db` seeds it once, on first run. Note the sandbox
  **copies** where a native build renames: inside the sandbox that file belongs
  to a natively installed Pixee that is still using it, and the first version of
  this code moved it, silently stealing the native install's cache. Anything
  else that ever reaches back into the host's home needs the same care.
- **Themes.** `Config::themeSearchPaths()` includes `<prefix>/share/pixee`, which
  is where `make install` puts the `themes/` tree. The binary-adjacent location
  still works, so the Windows and portable layouts are unaffected.
- **Open with.** The configured-programs list can't work in a sandbox: the stored
  paths point at host binaries that don't exist inside it. `OpenWithDialog::
  isSandboxed()` detects `/.flatpak-info`, and the context menu then offers a
  single **Open with...** that goes through `QDesktopServices::openUrl` — i.e. the
  xdg-desktop-portal OpenURI call, which runs the user's chosen app outside the
  sandbox without Pixee needing host-spawn permission. Deliberately *not*
  `--talk-name=org.freedesktop.Flatpak` + `flatpak-spawn --host`: Flathub reviewers
  read that as a sandbox escape and it would hold up the submission.
- **Window icon.** Wayland takes the icon from the desktop entry, matched to the
  window by `QGuiApplication::setDesktopFileName` (in `Pixee.cpp`). X11 uses
  `setWindowIcon`, which is set from the same SVG.

### The permissions, and why

- `--filesystem=host` — Pixee browses the filesystem through its own folder tree
  and copies, moves and renames across it. The document portal hands over one
  file at a time and cannot express that. Expect the reviewer to ask; this is the
  answer.
- `--filesystem=xdg-run/gvfs` — desktop-mounted network shares (SMB), a
  first-class case for this app.
- `--socket=wayland` + `--socket=fallback-x11`, `--share=ipc`, `--device=dri` —
  ordinary desktop app needs.

No network permission: Pixee doesn't use one.

## Cutting a release that Flathub can build

Flathub builds from an immutable source, so the submitted manifest points at a
tagged tarball rather than a branch. For each release:

1. Bump `VERSION.txt`.
2. Add a `<release>` entry at the top of `packaging/net.dynart.Pixee.metainfo.xml`
   with that version and the release date, and re-run `appstreamcli validate`.
   Flathub shows these as the "What's new" text, so keep them to a sentence or two.
3. Update `CHANGELOG.md` and `README.md` as usual, then tag: `git tag v<version>`.
4. Get the tarball hash:
   ```sh
   curl -sL https://github.com/DynartInteractive/Pixee/archive/refs/tags/v<version>.tar.gz \
       | sha256sum
   ```
5. In the **Flathub** copy of the manifest, replace the `type: dir` source with:
   ```yaml
   - type: archive
     url: https://github.com/DynartInteractive/Pixee/archive/refs/tags/v<version>.tar.gz
     sha256: <the hash from step 4>
   ```

Keep the `type: dir` source in *this* repo's copy — it's what makes local builds
convenient. Only the Flathub copy is pinned.

## Submitting to Flathub

1. Fork <https://github.com/flathub/flathub>.
2. Branch from **`new-pr`** (not `master` — the PR is rejected otherwise).
3. Add `net.dynart.Pixee.yml` (the pinned copy) at the repo root. One app per PR.
4. Open the PR against `new-pr`. The buildbot builds it and posts a link; install
   that test build and actually use it before saying it's ready.
5. A reviewer comments. Budget for a round or two — `--filesystem=host` is the
   likely topic.
6. On merge you get a `flathub/net.dynart.Pixee` repo with maintainer rights.
   Later releases are PRs to *that* repo: bump the tag, the sha256 and the
   metainfo release entry.

Timeline is usually days to a couple of weeks, almost all of it waiting on review.

## Still open

- **Runtime version.** Pinned to KDE 6.11, the current runtime, and verified
  against it. Runtimes keep moving — re-run the manifest lint before you submit,
  and if it reports `runtime-update-available`, bump `runtime-version` and
  rebuild. It's a one-line change.
- **Extra image formats — already solved, for free.** The KDE runtime bundles
  kimageformats, so the flatpak reads *more* formats than a native build without
  any manifest module: HEIC/HEIF, AVIF, JPEG XL, PSD/PSB, XCF, KRA, ORA, DDS,
  QOI, SVG, EXR-adjacent formats and more. Compare for yourself — the
  "Supported image formats" line the app logs at startup, run natively vs. in
  the flatpak. Nothing to do here; `thirdparty/imageformats/` stays Windows-only.
- **Exiv2.** Not built into the flatpak. It isn't in the runtime, so it would need
  its own manifest module — and it's GPLv2+ against Pixee's MIT, which makes the
  *distributed binary* GPL-encumbered. That's a licensing decision, not a build
  problem; see `docs/metadata.md`. Without it the metadata panel shows the Qt-only
  basics plus the embedded PNG text chunks, which work in every build.
- **Screenshots.** The metainfo points at `main` so they track the current UI.
  Repoint them at a release tag if you'd rather freeze them per version.
