# Getting Pixee onto Flathub — what you need to do

The packaging itself is done and verified: `packaging/` holds the manifest, the
desktop entry and the AppStream metadata, the build works and the app runs in the
sandbox. See [`docs/flatpak.md`](flatpak.md) for how that all fits together.

This file is the other half — the steps **only you** can do. Roughly in order.

---

## 1. Put a Pixee page on dynart.net

**Blocks everything else. Do it first.**

The app ID is `net.dynart.Pixee`, and Flathub requires the ID to be built from a
domain you control that carries the app. A reviewer will open `dynart.net` and
look for Pixee. A single page is enough — name, a sentence, a screenshot, a link
to the GitHub repo.

If you'd rather not touch the website, the alternative is renaming the app to
`io.github.DynartInteractive.Pixee`, which needs no proof beyond the public repo
already existing. That means changing the ID in five places: the manifest, the
metainfo `<id>`, the three `packaging/` filenames, the icon filename, and
`setDesktopFileName` in `src/Pixee.cpp`. Say the word and I'll do it — but the
domain ID is the nicer long-term identity, so the website page is worth the hour.

## 2. Create a GitHub account for the submission, if you want one separate

Flathub submissions happen as GitHub PRs and the merged app repo is tied to the
account that submitted. Whichever account you use becomes the maintainer of
`flathub/net.dynart.Pixee`. Using your normal account is fine — just be aware
this is the one that gets the review notifications and the update PRs forever.

## 3. Cut a release

Flathub builds from an immutable tarball, and none of the packaging files exist
at `v0.3.0` — so you need a new tag after this work is committed.

1. Decide the number. These changes are features plus a behaviour change, so
   pre-1.0 that's a **minor bump: 0.4.0**. Edit `VERSION.txt` — it's the single
   source, everything else reads it.
2. Add a `<release>` entry at the top of
   `packaging/net.dynart.Pixee.metainfo.xml` with that version and the date.
   This text becomes the "What's new" in software centres, so a sentence or two,
   written for users rather than for a changelog.
3. Validate it: `appstreamcli validate packaging/net.dynart.Pixee.metainfo.xml`
4. Update `CHANGELOG.md` and `README.md` as usual.
5. Commit, then `git tag v0.4.0 && git push --tags`.
6. Create the GitHub release so the tarball URL exists.

## 4. Pin the manifest to that tarball

Take the sha256:

```sh
curl -sL https://github.com/DynartInteractive/Pixee/archive/refs/tags/v0.4.0.tar.gz \
    | sha256sum
```

In the copy you submit (**not** the one in this repo — that one stays a `dir`
source so local builds stay easy), replace the `sources:` block with:

```yaml
    sources:
      - type: archive
        url: https://github.com/DynartInteractive/Pixee/archive/refs/tags/v0.4.0.tar.gz
        sha256: <the hash>
```

Then build *that* version once locally and run it, so you know the tarball builds
as cleanly as the working tree did.

## 5. Open the submission PR

1. Fork <https://github.com/flathub/flathub>.
2. Branch from **`new-pr`** — not `master`. A PR against `master` is closed
   without review.
3. Add a single file at the repo root: `net.dynart.Pixee.yml`, the pinned
   manifest from step 4. One app per PR, nothing else in the branch.
4. Open the PR against `new-pr`.

**In the PR description, ask for the `--filesystem=host` exception up front.**
This is the one thing that will otherwise stall the review. The linter flags
broad filesystem access as an error by design, and a reviewer has to grant an
exception against the app ID. Explain it in your own words, but the substance is:

> Pixee is a file manager for images, not a viewer. It browses the filesystem
> through its own folder tree and copies, moves, renames and deletes files
> between arbitrary directories. The document portal hands over one file at a
> time, which cannot express browsing a directory tree or moving a selection
> between two folders. The app requests no network access and stores its own
> data under its app data directory.

It's also worth saying you tested it: the app writes nothing to the user's real
home, and the thumbnail cache lives in the app data dir.

## 6. Work the review

- The buildbot builds your PR and posts a link to a test build. **Install it and
  actually use it** before saying it's ready — browse a big folder, open the
  viewer, copy a file, check "Open with" goes to the desktop's chooser.
- A reviewer will comment. Budget a round or two; `--filesystem=host` is the
  likely topic. Answer plainly and don't argue the linter — it's doing its job.
- Turnaround is usually days to a couple of weeks, nearly all of it waiting.

## 7. After it's merged

You get a `flathub/net.dynart.Pixee` repo with maintainer rights. From then on:

- **Each release** is a PR to that repo bumping the tag, the sha256 and the
  metainfo `<release>` entry. Same three edits every time.
- Add the Flathub badge and install line to `README.md`'s Download section, and
  drop the "not on Flathub yet" note in the Building chapter.
- Consider enabling Flathub's build bot to open update PRs for you.

---

## Quick pre-flight

Before opening the PR, all of these should hold:

```sh
# builds and runs
flatpak run org.flatpak.Builder --user --install --force-clean \
    build packaging/net.dynart.Pixee.yml
flatpak run net.dynart.Pixee

# metadata valid
appstreamcli validate packaging/net.dynart.Pixee.metainfo.xml
desktop-file-validate packaging/net.dynart.Pixee.desktop

# linters — see docs/flatpak.md for which errors are expected
flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
    manifest packaging/net.dynart.Pixee.yml
```

The only lint errors that should remain are `finish-args-host-filesystem-access`
(step 5's exception) and, on a repo lint, the two
`appstream-*screenshot*` ones, which can only pass on Flathub's own builder.
