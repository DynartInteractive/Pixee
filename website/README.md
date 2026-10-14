# pixee.dynart.net

The Pixee landing page. Static, no build step and no dependencies — edit the
files and upload them.

```
website/
├── index.html        the whole page
├── style.css         the app's dark theme, flat (see the header comment)
├── main.js           screenshot gallery + lightbox
├── icon.png          copy of docs/pixee-icon-256.png
├── favicon.ico       copy of resources/icons/Pixee.ico
└── screenshots/      copies of the docs/ screenshots
```

## Hosting

Live at <https://pixee.dynart.net>, served by Apache from the host's docroot.
`pixee.cc` 301s to it. That direction matters: `pixee.dynart.net` is the
permanent address and the one the app ID `net.dynart.Pixee` is built from, so
it is the name that has to keep resolving — `pixee.cc` is a convenience alias
and may lapse. The `<link rel="canonical">` and `og:` tags in `index.html`
name `pixee.dynart.net` for the same reason. Deploying is just copying the
folder's contents up — there is no CNAME file because that is a GitHub Pages
mechanism and this is not GitHub Pages.

```sh
rsync -av --delete website/ <user>@<host>:/var/www/pixee.cc/
```

(The docroot path is whatever the `pixee.dynart.net` vhost points at — it was
`/var/www/pixee.cc/` before the domains were swapped and may not have moved.)

## Feature images

The six rows under "What's in it" each show `screenshots/feature-<name>.jpg`,
a 3:2 crop of the app at 1600x1067 (the page shows it at half size so it
is sharp on high-DPI screens). The files there now are grey placeholders;
overwrite each with the real crop under the same name and nothing else
needs changing. Names: fast, viewer, edit, metadata, files, formats.

## After editing style.css or main.js

Both are linked from `index.html` with a `?v=<date>` query string so browsers
fetch the new file instead of a cached one. Bump the date in both links
whenever either file changes.

## After a release

The download buttons point at pinned release assets rather than
`/releases/latest/`, because every release so far is marked pre-release and
GitHub's `latest` redirect skips those. So they need a manual bump:

1. In `index.html`, update the two `href`s under `<div class="downloads">` and
   the version/size in the matching `.btn-sub` lines. There is a comment above
   them marking the spot.
2. If the screenshots changed, re-copy them:

   ```sh
   cp ../docs/screenshot-list-v3.jpg  screenshots/list.jpg
   cp ../docs/screenshot-view-v3.jpg  screenshots/view.jpg
   cp ../docs/screenshot-crop-v4.jpg  screenshots/edit.jpg
   ```

   The `width` / `height` attributes on the three carousel `<img>`s are the
   images' real pixel sizes — they reserve the right height before the files
   load. Update them if a new screenshot is a different size, and the `alt`
   text if the shot shows something else.

The icon is generated from `resources/icons/net.dynart.Pixee.svg` by
`scripts/make-icon.bat`; re-copy `icon.png` and `favicon.ico` if it changes.

## Previewing

Open `index.html` directly, or serve the folder:

```sh
python -m http.server -d website 8000
```
