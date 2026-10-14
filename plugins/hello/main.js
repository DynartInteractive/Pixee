// Example plugin. Everything a plugin can currently do is in this file.
//
// The script is evaluated once at startup, in its own JavaScript engine.
// Register menu entries at that point with pixee.addAction(); the callback
// runs later, each time the user picks the entry.

pixee.addAction("Count selected files", function () {
    var files = pixee.selectedFiles();
    pixee.log("selected: " + files.length);
    for (var i = 0; i < files.length; i++) {
        pixee.log("  " + files[i]);
    }
});

pixee.addAction("Where am I?", function () {
    pixee.log("current folder: " + pixee.currentFolder());
});

pixee.log("loaded");
