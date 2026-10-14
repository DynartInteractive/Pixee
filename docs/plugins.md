# Plugins

Pixee runs plugins written in JavaScript. A plugin is a folder with a
`plugin.json` and a script; Pixee finds it at startup, runs it once, and adds
whatever it registered to the **Plugins** menu.

This is the first iteration and the API is deliberately small — enough to
register a menu entry and read what the user has selected. Async HTTP, plugin
UI and long-running jobs are not here yet; see *Not yet* at the bottom.

## Where plugins live

Searched in this order, first hit per folder name wins:

| Location | Use |
|---|---|
| `<data dir>/plugins/` | yours; survives reinstalls |
| `~/.pixee/plugins/` | legacy dot-folder, still read |
| `<app dir>/plugins/` | ships next to the binary (Windows, portable) |
| `<prefix>/share/pixee/plugins/` | Linux `make install` and the Flatpak |

`<data dir>` is `~/.local/share/Dynart/Pixee` on Linux and
`%LOCALAPPDATA%\Dynart\Pixee` on Windows.

Because the first hit wins, dropping a folder named `hello` into your data
directory **shadows** the bundled `hello` rather than clashing with it — which
is how you modify a bundled plugin without editing the install.

The folder name is the plugin's id. It is not read from the manifest, so two
plugins in one directory cannot claim the same id, and renaming a folder (or
moving it aside) disables the plugin.

## Layout

```
plugins/
  hello/
    plugin.json
    main.js
```

`plugin.json`:

```json
{
    "name": "Hello",
    "version": "1.0.0",
    "description": "Example plugin. Shows what the pixee object exposes.",
    "author": "Dynart Interactive",
    "entry": "main.js"
}
```

Every field is optional except that the file must exist and parse. `name`
falls back to the folder name, `entry` to `main.js`. `entry` must stay inside
the plugin folder — a path that climbs out of it is refused.

## Writing one

The script is evaluated **once, at startup**. Register menu entries then; the
callbacks run later, each time the user picks the entry.

```js
pixee.addAction("Count selected files", function () {
    var files = pixee.selectedFiles();
    pixee.log("selected: " + files.length);
});
```

### The `pixee` object

| Call | Returns | Notes |
|---|---|---|
| `pixee.addAction(title, fn)` | — | Adds an entry to the Plugins menu. `fn` must be a function. |
| `pixee.selectedFiles()` | array of strings | Absolute paths of the file-list selection. Empty if nothing is selected. |
| `pixee.currentFolder()` | string | Absolute path of the folder being browsed. |
| `pixee.log(message)` | — | Writes to the application log, prefixed with the plugin id. |

`console.log` also works and goes to the same place.

Each plugin gets **its own JavaScript engine**, so your globals are yours: you
cannot see another plugin's variables and it cannot overwrite yours or the
`pixee` object.

## When a plugin is broken

A plugin that fails is skipped with a warning; the rest still load and Pixee
starts normally. There is no way for a plugin to prevent the app coming up.

What gets reported, all as warnings on the application log:

- a `plugin.json` that is not valid JSON, with the parser's reason
- an `entry` that does not exist, or that points outside the plugin folder
- a script that throws while loading, with the error, file and line
- an error thrown by an action callback, reported when the action is used
- `addAction` called with a non-function, or with an empty title

A folder with no `plugin.json` is not a plugin and is skipped silently, so
stray directories in `plugins/` cost nothing.

## Menu layout

With one plugin contributing actions, its entries sit directly under
**Plugins**. With more than one, each plugin gets its own submenu named after
it. The Plugins menu is hidden entirely when nothing registered anything, so a
stock install shows no empty menu.

This is not cosmetic: a flat menu that grows with every installed plugin runs
past the bottom of a 1280x720 screen, which is the size Pixee is designed to
work at. See the UI-density note in `CLAUDE.md`.

## Threading

Everything above runs on the GUI thread, and `QJSEngine` belongs to the thread
that created it. A plugin callback blocks the UI for as long as it runs, so
keep them short.

That constraint is exactly why async is not in this iteration: doing it
properly means handing work to a worker thread and calling back **into the GUI
thread**, never touching the engine from another one — the same rule the rest
of the application follows.

## Not yet

In rough order of usefulness:

- **Async HTTP**, with the callback marshalled back to the GUI thread. This is
  the one that matters: the plugins this system exists for talk to ComfyUI and
  to a blog's publishing API.
- **A form/dialog API**, so a plugin can ask for parameters.
- **`Task` integration**, so a long job gets the progress dock, the conflict
  prompts, and the folder refresh that every built-in file operation has.
- **Plugin-contributed settings**, and a way to translate plugin strings —
  both sit outside the build-time `tr()` registry the rest of the UI uses.
- **Context-menu entries**, under a single `Plugins` submenu.
