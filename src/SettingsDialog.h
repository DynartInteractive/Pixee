#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

class Config;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QWidget;

// Non-modal, always-on-top settings window.
//   - Top:    a search box + button that filters/highlights settings by label.
//   - Left:   an icon list of setting groups (single selection).
//   - Right:  the selected group's controls (one QStackedWidget page/group).
//   - Bottom: Save / Cancel. Values are staged in the controls and only
//             written to QSettings on Save; Cancel (or the window's X)
//             discards.
//
// The whole thing is generated from a small declarative registry in the .cpp,
// so adding a setting is a one-liner and search stays generic. Labels are
// produced with tr() when the dialog is built, so search matches whatever
// language is currently active.
//
// Four control types: Bool (check box), Choice (combo of stored-value /
// display pairs), Int (spin box with a range and an optional suffix) and
// Path (line edit + Browse… folder picker). A setting flagged `restart` is
// one the app reads once at startup; Save lists the changed ones in a single
// notice. A setting can also be enabled only while another Choice holds a
// given value (the fixed startup folder's path, for example).
//
// Save emits saved() after the values hit QSettings, so MainWindow can
// re-read the live ones (viewer wheel, cell size, …) without a restart.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(Config* config, QWidget* parent = nullptr);

signals:
    // Emitted once per Save, after QSettings::sync().
    void saved();

private slots:
    void applySearch(const QString& query);
    void save();

private:
    enum ControlType { Bool, Choice, Int, Path };

    struct Row {
        QWidget*   container = nullptr;    // row widget — shown/hidden by search
        QWidget*   labelWidget = nullptr;  // checkbox (Bool) or QLabel (others)
        QString    label;                  // display text, for the restart notice
        QString    labelText;              // lowercased, for matching
        int        groupIndex = 0;
        QString    key;
        int        type = Bool;
        QVariant   def;
        QVariant   initial;                // value loaded from QSettings
        bool       restart = false;        // read once at startup
        QString    enabledByKey;           // enabled only while this Choice …
        QString    enabledByValue;         // … holds this stored value
        QCheckBox* check = nullptr;
        QComboBox* combo = nullptr;
        QSpinBox*  spin = nullptr;
        QLineEdit* edit = nullptr;
    };

    void build();   // constructs the group list, stacked pages, and row registry
    void load();    // control values <- QSettings
    QVariant currentValue(const Row& row) const;  // staged value as stored
    void updateDependentRows();

    Config*         _config = nullptr;
    QLineEdit*      _search = nullptr;
    QListWidget*    _groupList = nullptr;
    QStackedWidget* _stack = nullptr;
    QList<Row>      _rows;
    QStringList     _groupTitlesLower;  // per group, for title matching
};

#endif // SETTINGSDIALOG_H
