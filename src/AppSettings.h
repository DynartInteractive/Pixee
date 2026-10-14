#ifndef APPSETTINGS_H
#define APPSETTINGS_H

// Central QSettings key names (organisation "Dynart", application "Pixee").
// Kept in one place so the SettingsDialog and the code that reads a setting
// can never drift apart on the spelling of a key. Defaults live next to the
// keys for the same reason: the dialog's registry and the reading code both
// take them from here.
namespace AppSettings {

// ---- General ----------------------------------------------------------------

// string — UI language code ("en", "hu", "de", "fr", "es"); empty means
// follow the operating system locale. Applied at startup only (changing it
// needs a restart), so the value is read once when the translator is
// installed, before any UI is built.
constexpr const char* kLanguage = "general/language";

// string — theme folder name under themes/ ("dark" by default), or "default"
// for Qt's own look with no stylesheet at all. Read once by Config when the
// app starts (Theme loads its assets before any widget exists), so changing
// it needs a restart.
constexpr const char* kTheme = "general/theme";
constexpr const char* kThemeDefault = "dark";

// string — where the browser starts: "last" (the folder from the previous
// session, the default), "home" (the user's home folder), "root" (the drive
// list on Windows, "/" elsewhere) or "fixed" (the folder in kStartupPath).
// A command-line image always wins over this.
constexpr const char* kStartupMode = "general/startupMode";
constexpr const char* kStartupModeDefault = "last";
// string — the folder opened at startup when kStartupMode is "fixed".
constexpr const char* kStartupPath = "general/startupPath";

// ---- Thumbnails -------------------------------------------------------------

// int — how many thumbnail decoders run in parallel (1..8). Read once by
// Config at startup (the pool is built then), so it needs a restart. Two is
// the shipped default: on a network share more decoders compete with the
// viewer's own full-resolution load for the same pipe.
constexpr const char* kDecoderThreads = "thumbnails/decoderThreads";
constexpr int kDecoderThreadsDefault = 2;
constexpr int kDecoderThreadsMin = 1;
constexpr int kDecoderThreadsMax = 8;

// int — the edge of a file-list cell in pixels: 128, 192 or 256. Display
// only; thumbnails are always cached at Config::thumbnailSize() (256) and
// scaled down to the cell, so a smaller grid costs no re-decode. Applied
// live.
constexpr const char* kCellSize = "thumbnails/cellSize";
constexpr int kCellSizeDefault = 256;

// ---- Viewer -----------------------------------------------------------------

// string — what the plain mouse wheel does in the viewer: "navigate" (previous
// / next image, the default) or "zoom". Ctrl + wheel does the other one.
// Applied live.
constexpr const char* kViewerWheel = "viewer/wheel";
constexpr const char* kViewerWheelDefault = "navigate";

// string — how a freshly opened image is sized: "fitLarge" (shrink to fit
// only when it is bigger than the window, the default), "fit" (always scale
// to the window, up or down) or "actual" (1:1). Ignored while Lock zoom is
// on. Applied live.
constexpr const char* kViewerOpenZoom = "viewer/openZoom";
constexpr const char* kViewerOpenZoomDefault = "fitLarge";

// ---- File operations --------------------------------------------------------

// bool — after a task group adds files to the folder currently being viewed,
// select those files and scroll them into view. Default true.
constexpr const char* kSelectAddedFiles = "fileOps/selectAddedFiles";

// bool — ask before moving the selection to the recycle bin / trash. Default
// true. Permanent delete (Shift+Del) always asks, whatever this says.
constexpr const char* kConfirmTrash = "fileOps/confirmTrash";

// int — encoder quality (1..100) used by File → Save when it writes an edited
// image back over a lossy original (JPEG, WebP, …). Save As has its own
// slider. Default 92.
constexpr const char* kSaveQuality = "fileOps/saveQuality";
constexpr int kSaveQualityDefault = 92;

// ---- View menu (not in the Settings window) ---------------------------------

// string — list-view sort key: "name" (default), "created", or "modified".
// The folder tree and the folder index-image overlay are always name-sorted;
// this affects only the central file list.
constexpr const char* kSortBy = "view/sortBy";

// string — list-view sort direction: "asc" (default) or "desc". Applies to
// the leaf comparison only ("..", and folders-before-files, never invert).
constexpr const char* kSortOrder = "view/sortOrder";

}  // namespace AppSettings

#endif // APPSETTINGS_H
