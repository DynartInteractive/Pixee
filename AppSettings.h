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

}  // namespace AppSettings

#endif // APPSETTINGS_H
