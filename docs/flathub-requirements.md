# Getting Pixee onto Flathub — what you need to do

The packaging itself is done and verified: `packaging/` holds the manifest, the
desktop entry and the AppStream metadata, the build works and the app runs in the
sandbox. See [`docs/flatpak.md`](flatpak.md) for how that all fits together.

This file is the other half — the steps **only you** can do. Roughly in order.

---

## 1. Serve the landing page at pixee.dynart.net

**Blocks the submission. It is a hosting change, not a code one.**

The app ID is `net.dynart.Pixee`, so Flathub expects `dynart.net` to carry the
app — a reviewer opens the ID's domain and looks for Pixee. The page exists
(`website/`), but it is served the wrong way round:

```
pixee.dynart.net  →  301  →  pixee.cc   (200)
dynart.net        →  404
```

`pixee.dynart.net` is the permanent address and `pixee.cc` may not always be
around, so the redirect currently points the durable name at the disposable one.
If `pixee.cc` ever lapses, the ID's domain stops resolving to anything — exactly
the failure the domain-matching rule exists to prevent.

**Flip it:** serve `website/` from `pixee.dynart.net` and 301 `pixee.cc` to it.
Then update `<link rel="canonical">` and the `og:url` meta in
`website/index.html`, which still name `pixee.cc`.

A redirect *to* the app's real home would probably pass review as-is — it does
prove you control the domain. The reason to do it properly is the permanence, not
the reviewer.

The metainfo's `<url type="homepage">` already points at `https://pixee.dynart.net`,
so it keeps working either way, before and after the flip.

## 2. Create a GitHub account for the submission, if you want one separate

Flathub submissions happen as GitHub PRs and the merged app repo is tied to the
account that submitted. Whichever account you use becomes the maintainer of
`flathub/net.dynart.Pixee`. Using your normal account is fine — just be aware
this is the one that gets the review notifications and the update PRs forever.

## 3. Cut a release — **done: v0.4.0**

Flathub builds from an immutable tarball, and none of the packaging files existed
at `v0.3.0`, so this needed a new tag. `v0.4.0` is tagged and pushed, which is
all the manifest needs: GitHub generates the source tarball for any tag
automatically, with no Release object required.

Two things about v0.4.0 are worth knowing before you submit:

- **There are no Windows binaries for it.** Building those needs your Windows
  machine, and 0.4.0 changes nothing there beyond the window icon, so the
  README's download links still point at the 0.3.0 assets. If you'd rather the
  release page not look half-finished, build and attach the 0.4.0 installer and
  portable before announcing it — Flathub neither needs nor looks at them.
- **Creating the GitHub Release object is optional** for Flathub, but it's what
  makes the `<url type="details">` link in the metainfo resolve, and it's where
  release notes live for humans. `docs/release-0.4.0.md` is written and ready to
  paste in.

For future releases the procedure is:

1. Decide the number — pre-1.0, features bump the minor, fixes the patch. Edit
   `VERSION.txt`; it's the single source, everything else reads it.
2. Add a `<release>` entry at the top of
   `packaging/net.dynart.Pixee.metainfo.xml` with that version and the date.
   This text becomes the "What's new" in software centres, so a sentence or two,
   written for users rather than for a changelog.
3. Validate it: `appstreamcli validate packaging/net.dynart.Pixee.metainfo.xml`
   — it fetches the screenshot URLs too, so this is what catches a renamed
   screenshot before the Flathub builder does.
4. Update `CHANGELOG.md`, `README.md` and add a `docs/release-<version>.md`.
5. Commit, then `git tag v<version> && git push --tags`.

## 4. Pin the manifest to that tarball — **done and verified**

The submission manifest is `packaging/net.dynart.Pixee.yml` with its `sources:`
block replaced by the pinned release tarball. Keep the repo copy as a `dir`
source — that's what makes local builds convenient — and change it only in the
copy you put in the Flathub PR:

```yaml
    sources:
      - type: archive
        url: https://github.com/DynartInteractive/Pixee/archive/refs/tags/v0.4.0.tar.gz
        sha256: 393907fbb8545e20f1562b107ac23e37cbb6a0ad77c91e5a21ff34f916099ae3
```

That exact manifest has been built and run: it produces a working Pixee 0.4.0,
and both linters return only the expected errors (see below). So the buildbot
should have nothing to say that a reviewer doesn't.

For a future release, regenerate the hash with:

```sh
curl -sL https://github.com/DynartInteractive/Pixee/archive/refs/tags/v<version>.tar.gz \
    | sha256sum
```

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
