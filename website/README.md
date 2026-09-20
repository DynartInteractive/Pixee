# pixee.dynart.net

The Pixee landing page. Static, no build step and no dependencies — edit the
files and upload them.

```
website/
├── index.html        the whole page
├── style.css         light/dark via prefers-color-scheme
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

## After a release

The download buttons point at pinned release assets rather than
`/releases/latest/`, because every release so far is marked pre-release and
GitHub's `latest` redirect skips those. So they need a manual bump:

1. In `index.html`, update the two `href`s under `<div class="downloads">` and
   the version/size in the matching `.btn-sub` lines. There is a comment above
   them marking the spot.
2. If the screenshots changed, re-copy them:

   ```sh
   cp ../docs/screenshot-list-v2.jpg  screenshots/list.jpg
   cp ../docs/screenshot-view-v2.jpg  screenshots/view.jpg
   cp ../docs/screenshot-crop-v3.jpg  screenshots/edit.jpg
   ```

   The `data-w` / `data-h` attributes on the gallery tabs are the images'
   real pixel sizes — they reserve the right height so the page doesn't jump
   when you switch shots. Update them if a new screenshot is a different size.

The icon is generated from `resources/icons/net.dynart.Pixee.svg` by
`scripts/make-icon.bat`; re-copy `icon.png` and `favicon.ico` if it changes.

## Previewing

Open `index.html` directly, or serve the folder:

```sh
python -m http.server -d website 8000
```
