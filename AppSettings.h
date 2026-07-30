#ifndef APPSETTINGS_H
#define APPSETTINGS_H

// Central QSettings key names (organisation "Dynart", application "Pixee").
// Kept in one place so the SettingsDialog and the code that reads a setting
// can never drift apart on the spelling of a key.
namespace AppSettings {

// bool — after a task group adds files to the folder currently being viewed,
// select those files and scroll them into view. Default true.
constexpr const char* kSelectAddedFiles = "fileOps/selectAddedFiles";

// string — UI language code ("en", "hu", "de", "fr", "es"); empty means
// follow the operating system locale. Applied at startup only (changing it
// needs a restart), so the value is read once when the translator is
// installed, before any UI is built.
constexpr const char* kLanguage = "general/language";

// string — list-view sort key: "name" (default), "created", or "modified".
// The folder tree and the folder index-image overlay are always name-sorted;
// this affects only the central file list.
constexpr const char* kSortBy = "view/sortBy";

// string — list-view sort direction: "asc" (default) or "desc". Applies to
// the leaf comparison only ("..", and folders-before-files, never invert).
constexpr const char* kSortOrder = "view/sortOrder";

}  // namespace AppSettings

#endif // APPSETTINGS_H
