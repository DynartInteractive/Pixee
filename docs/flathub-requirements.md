# Getting Pixee onto Flathub — what you need to do

The packaging itself is done and verified: `packaging/` holds the manifest, the
desktop entry and the AppStream metadata, the build works and the app runs in the
sandbox. See [`docs/flatpak.md`](flatpak.md) for how that all fits together.

This file is the other half — the steps **only you** can do. Roughly in order.

**Where it stands:** steps 1–5 are done — **the submission PR is open**:
<https://github.com/flathub/flathub/pull/10328> (2026-09-21, pinned to v0.5.0).
What remains is step 6, working the review, which is mostly waiting. Its CI is
**red on purpose**; see step 6 before reading that as a problem. Everything
else outstanding is optional and listed where it belongs — Windows binaries
(step 3) and a Pixee entry on the `dynart.net` apex page (step 1). Domain
verification is done.

---

## 1. Serve the landing page at pixee.dynart.net — **done**

The app ID is `net.dynart.Pixee`, so Flathub expects `dynart.net` to carry the
app — a reviewer opens the ID's domain and looks for Pixee. That now holds, and
the redirect runs the right way round: the durable name serves the page and the
disposable one points at it.

Verified live:

```
pixee.dynart.net  →  200        (Apache, serves website/)
pixee.cc          →  301        →  https://pixee.dynart.net/
dynart.net        →  200        (the Dynart projects page)
```

`website/index.html`'s `<link rel="canonical">` and its `og:url` / `og:image`
name `pixee.dynart.net` too, and that version is deployed — the served page was
re-checked after the rsync and carries the new tags.

The apex `dynart.net` serves the Dynart projects page, and **it does not mention
Pixee**. Nothing requires it to: Flathub's domain rule is satisfied by a
subdomain of the ID's domain, and the metainfo's `<url type="homepage">` points
at `https://pixee.dynart.net`. But a reviewer checking that `net.dynart.Pixee`
really belongs to you may well trim the hostname, and adding Pixee to that
page's project list would make the connection obvious for the cost of one entry.

When checking any of this, **use GET, not HEAD**: `curl -I https://dynart.net`
returns `404` while a plain GET returns `200` and the full page — something in
that site's stack doesn't answer HEAD. Checking with `-I` is how this doc
briefly came to claim the apex was dead.

## 2. Pick the submitting account — **done: the usual one**

Flathub submissions happen as GitHub PRs and the merged app repo is tied to the
account that submitted, so that account becomes the maintainer of
`flathub/net.dynart.Pixee` and gets the review notifications and the update PRs
from then on. Decision: the normal account (`gopher.hu@gmail.com`, the one
behind every commit here and a public member of DynartInteractive). Nothing
separate to create.

## 3. Cut a release — **done: v0.4.0, superseded by v0.5.0**

Flathub builds from an immutable tarball, and none of the packaging files existed
at `v0.3.0`, so this needed a new tag. `v0.4.0` is tagged and pushed, which is
all the manifest needs: GitHub generates the source tarball for any tag
automatically, with no Release object required.

**v0.5.0 is what the submission is pinned to** — it was tagged before the PR
went out, so the notes below are v0.4.0 history; the two bullets still apply
unchanged to 0.5.0. Two things worth knowing:

- **There are no Windows binaries for it.** Building those needs your Windows
  machine, and 0.4.0 changes nothing there beyond the window icon, so the
  README's download links still point at the 0.3.0 assets. If you'd rather the
  release page not look half-finished, build and attach the 0.4.0 installer and
  portable before announcing it — Flathub neither needs nor looks at them.
- **The GitHub Release object exists** — published as "Pixee 0.4.0", marked
  pre-release like the others, no assets attached. Flathub doesn't need it, but
  it is what makes the metainfo's `<url type="details">` show notes rather than
  a bare tag page.

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
6. If you publish a GitHub Release, its body is `docs/release-<version>.md`
   **minus the leading `# Pixee <version>` heading** — that's what the title
   already says, and it is the transform v0.3.0 got. Absolutise the README
   link while you're there: `](../README.md)` resolves to
   `/releases/README.md` on a release page and 404s, as it does on the live
   0.3.0 notes. `https://github.com/DynartInteractive/Pixee/blob/v<version>/README.md`
   pins it to the matching tag. Mark it a pre-release, as every release so far
   is — the website's download buttons depend on that (`website/README.md`).

## 4. Pin the manifest to that tarball — **done: pinned to v0.5.0, verified**

The submission manifest is `packaging/net.dynart.Pixee.yml` with its `sources:`
block replaced by the pinned release tarball. Keep the repo copy as a `dir`
source — that's what makes local builds convenient — and change it only in the
copy you put in the Flathub PR:

```yaml
    sources:
      - type: archive
        url: https://github.com/DynartInteractive/Pixee/archive/refs/tags/v0.5.0.tar.gz
        sha256: f53926141b58f19a17500b477c6245c6854eda0fbb6483907f213ed7b00a6d67
```

That hash was taken from the published tarball (9.4 MB) and is what the
buildbot will check against.

**The 0.5.0 pin was verified on 2026-09-21** — the pre-flight at the bottom of
this file, run on `mini` against the pinned manifest rather than the repo copy,
so the tarball URL and hash were exercised too. It builds clean, the app runs
in the sandbox, and `flatpak-builder-lint manifest` reports exactly one error,
`finish-args-host-filesystem-access`.

Worth knowing why that re-verify was not a formality. Between 0.4.0 (the only
manifest ever built before) and 0.5.0, `Pixee.pro` gained a **`QMAKE_SUBSTITUTES`
step generating `BuildInfo.h` into `$$OUT_PWD`** for the build timestamp. That
is new code generation running under flatpak-builder's shadow build, and it had
never been exercised there. It works — but "only the `sources:` block changed"
was not true, and would not have been the thing that broke.

Two other 0.5.0 changes fail *only* on Flathub's builder and pass everywhere
locally, so check them from any machine before submitting:

```sh
# the icon SVG's `<svg` must be within the first 256 bytes or AppStream compose
# refuses it and the Flatpak build fails — the SVG was rewritten in 0.5.0
python3 -c "print(open('resources/icons/net.dynart.Pixee.svg','rb').read().find(b'<svg'))"

# every metainfo screenshot URL must resolve — 0.5.0 renamed all four
for u in $(grep -o 'https://raw.githubusercontent.com[^<]*' packaging/net.dynart.Pixee.metainfo.xml); do
    echo "$(curl -s -o /dev/null -w '%{http_code}' "$u")  $u"
done
```

For a future release, regenerate the hash with:

```sh
curl -sL https://github.com/DynartInteractive/Pixee/archive/refs/tags/v<version>.tar.gz \
    | sha256sum
```

## 5. Open the submission PR — **done: PR #10328**

Opened 2026-09-21 against `new-pr`, pinned to v0.5.0:
<https://github.com/flathub/flathub/pull/10328>. What the process actually
involved, for the next time:

1. Fork <https://github.com/flathub/flathub> with **"Copy the master branch
   only" unchecked**. Flathub's docs say so for a concrete reason: the branch
   you need is `new-pr`, and a master-only fork does not have it to branch
   from.
2. Branch from **`new-pr`** — not `master`. A PR against `master` is closed
   without review. Name the branch after the app ID
   (`git checkout -b net.dynart.Pixee new-pr`); the docs only show a
   placeholder, but the ID matches the file, the eventual
   `flathub/net.dynart.Pixee` repo, and reads clearly in the PR list.
3. Add a single file at the repo root: `net.dynart.Pixee.yml`, the pinned
   manifest from step 4. One app per PR, nothing else in the branch. Only the
   manifest is copied — the desktop entry, metainfo and icon ship inside the
   release tarball and are installed by `Pixee.pro`'s `unix` block during the
   build, so Flathub reads them out of the built result.
4. Open the PR against `new-pr`. On GitHub's compare page the **base branch
   dropdown defaults to `master`** — changing it is the single easiest thing
   to miss in the whole process. Note your fork also has a `new-pr` branch
   (that is what you branched *from*); the target is **upstream's**.

**The PR template is a checklist and two of its boxes need real thought.**

- It asks for a **video of the app running as a Flatpak**. GitHub caps uploads
  at 10 MB — and does not say whether it means 10 MiB or 10,000,000 bytes, so
  aim well under. A 48 s 720p60 screen capture came to 12.7 MB raw; dropping
  the (silent) audio track and a two-pass x264 at 1100 kbps put it at 6.5 MB
  with the UI still crisp:

  ```sh
  ffmpeg -y -i in.mp4 -c:v libx264 -b:v 1100k -maxrate 1300k -bufsize 2600k -preset slow -pass 1 -an -f mp4 /dev/null
  ffmpeg -y -i in.mp4 -c:v libx264 -b:v 1100k -maxrate 1300k -bufsize 2600k -preset slow -pass 2 -an -movflags +faststart out.mp4
  ```

- It has **two AI checkboxes, and they are not the same question.** AI used
  *in the app* is permitted **if disclosed** — for Pixee that disclosure is
  substantial and was made plainly (141 of 170 commits carry a
  `Co-Authored-By: Claude` trailer, and `CLAUDE.md` ships inside the tarball,
  so a reviewer sees it either way). AI used to **write the submission PR or
  its review replies** is not permitted at all. So the PR prose and every
  answer to a reviewer have to be your own words. Get facts from wherever you
  like; write the sentences yourself.

**Domain verification — done 2026-09-21.** A bot comments on the PR offering
it: upload an empty `org.flathub.VerifiedApps.txt` to
`https://dynart.net/.well-known/org.flathub.VerifiedApps.txt`. That is the
**apex**, not `pixee.dynart.net`. It earns a *verified* badge and, more useful
during review, it is independent proof the app ID is yours — which is the
trust question sitting under a `--filesystem=host` request.

Nothing had to be configured: `/.well-known/` on the apex already returned
**403** rather than 404, i.e. the directory existed (certbot's ACME challenges)
and Apache was already serving it, so an empty `touch` in the `dynart.net`
vhost's document root was the whole job. Verify with GET, not HEAD (step 1):

```sh
curl -s -o /dev/null -w '%{http_code} %{size_download}' https://dynart.net/.well-known/org.flathub.VerifiedApps.txt; echo
# prints: 200 0
```

Placing the file *is* the confirmation — no reply to the bot needed. The badge
itself gets claimed on flathub.org once the app is published.

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

## 6. Work the review — **in progress**

**The PR's CI is red, and for this app that is the expected resting state.**
The buildbot's `validate-manifest` job runs `flatpak-builder-lint` and
**gates the build on it**, so a lint error means the build job is *skipped*
entirely. On 2026-09-21 the first run failed in exactly that way, with a single
annotation:

```
'finish-args-host-filesystem-access' error found in linter manifest check.
```

Everything before it passed, including "Detect app ID of the new submission" —
so the filename, location and app ID are right. The consequence is the part
that is not obvious from the outside: **there will be no test build to install
until a reviewer grants the exception.** Do not re-run `bot, build`, and do not
go looking for something to fix. Read the run's "Dump linter log" step in the
browser to confirm only that one error is present (the GitHub API refuses job
logs without admin on `flathub-infra/vorarbeiter`; the web UI shows them fine),
then wait.

- Once the exception is granted the build runs and posts a test build.
  **Install it and actually use it** before saying it's ready — browse a big
  folder, open the viewer, copy a file, check "Open with" goes to the desktop's
  chooser.
- A reviewer will comment. Budget a round or two; `--filesystem=host` is the
  likely topic. Answer plainly and don't argue the linter — it's doing its job.
  **In your own words** — see the AI note in step 5; it covers review replies
  too.
- A red X with no comment reads as unattended. One short line saying the
  failure is the expected host-filesystem lint error, and that the exception is
  requested in the description, costs nothing.
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
