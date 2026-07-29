#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

class QLineEdit;
class QListWidget;
class QStackedWidget;
class QCheckBox;
class QComboBox;
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
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void applySearch(const QString& query);
    void save();

private:
    enum ControlType { Bool, Choice };

    struct Row {
        QWidget*   container = nullptr;    // row widget — shown/hidden by search
        QWidget*   labelWidget = nullptr;  // checkbox (Bool) or QLabel (Choice)
        QString    labelText;              // lowercased, for matching
        int        groupIndex = 0;
        QString    key;
        int        type = Bool;
        QVariant   def;
        QCheckBox* check = nullptr;
        QComboBox* combo = nullptr;
    };

    void build();   // constructs the group list, stacked pages, and row registry
    void load();    // control values <- QSettings

    QLineEdit*      _search = nullptr;
    QListWidget*    _groupList = nullptr;
    QStackedWidget* _stack = nullptr;
    QList<Row>      _rows;
    QStringList     _groupTitlesLower;  // per group, for title matching
    QString         _initialLanguage;   // to detect a change on save (restart note)
};

#endif // SETTINGSDIALOG_H
