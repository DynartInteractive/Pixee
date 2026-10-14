#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

#include "AppSettings.h"
#include "Config.h"

namespace {

// ---- Declarative settings registry -----------------------------------------
// Add a setting by appending to the relevant group here; the dialog builds the
// UI and search index from this. Labels use tr() so they follow the active
// language (built fresh each time the dialog is constructed).

struct SettingDef {
    QString key;
    QString label;
    int type;                                  // SettingsDialog::ControlType
    QVariant def;
    QList<QPair<QString, QString>> choices;     // (stored value, display) — Choice only
    int min = 0;                               // Int only
    int max = 0;
    QString suffix;
    bool restart = false;                      // read once at startup
    QString enabledByKey;                      // enabled only while that Choice …
    QString enabledByValue;                    // … holds this stored value
};
struct GroupDef {
    QString title;
    QString icon;
    QList<SettingDef> settings;
};

// The numeric values mirror SettingsDialog::ControlType; the enum is private
// to the class and the registry lives outside it.
constexpr int kBool = 0;
constexpr int kChoice = 1;
constexpr int kInt = 2;
constexpr int kPath = 3;

SettingDef boolSetting(const char* key, const QString& label, bool def) {
    SettingDef s;
    s.key = QString::fromLatin1(key);
    s.label = label;
    s.type = kBool;
    s.def = def;
    return s;
}

SettingDef choiceSetting(const char* key, const QString& label, const QString& def,
                         const QList<QPair<QString, QString>>& choices,
                         bool restart = false) {
    SettingDef s;
    s.key = QString::fromLatin1(key);
    s.label = label;
    s.type = kChoice;
    s.def = def;
    s.choices = choices;
    s.restart = restart;
    return s;
}

SettingDef intSetting(const char* key, const QString& label, int def, int min, int max,
                      const QString& suffix = QString(), bool restart = false) {
    SettingDef s;
    s.key = QString::fromLatin1(key);
    s.label = label;
    s.type = kInt;
    s.def = def;
    s.min = min;
    s.max = max;
    s.suffix = suffix;
    s.restart = restart;
    return s;
}

SettingDef pathSetting(const char* key, const QString& label) {
    SettingDef s;
    s.key = QString::fromLatin1(key);
    s.label = label;
    s.type = kPath;
    s.def = QString();
    return s;
}

// Every theme folder Theme could load, in the order it would find them, plus
// "default" (Qt's own look). The bundled dark theme is always offered even
// when no themes/ tree is next to the binary, because its assets fall through
// to the :/ resources.
QList<QPair<QString, QString>> themeChoices(Config* config) {
    QStringList names;
    names << QString::fromLatin1(AppSettings::kThemeDefault);
    if (config) {
        for (const QString& root : config->themeSearchPaths()) {
            const QDir dir(root + QStringLiteral("/themes"));
            const QStringList subdirs =
                dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString& name : subdirs) {
                if (QFileInfo::exists(dir.filePath(name) + QStringLiteral("/style.qss")))
                    names << name;
            }
        }
    }
    names.removeDuplicates();
    names.removeAll(QStringLiteral("default"));
    std::sort(names.begin(), names.end(),
              [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });

    QList<QPair<QString, QString>> choices;
    for (const QString& name : names) {
        QString display = name;
        if (!display.isEmpty()) display[0] = display.at(0).toUpper();
        choices.append({ name, display });
    }
    choices.append({ QStringLiteral("default"), QObject::tr("System default (no theme)") });
    return choices;
}

QList<GroupDef> makeRegistry(Config* config) {
    QList<GroupDef> groups;

    GroupDef general;
    general.title = QObject::tr("General");
    general.icon  = QStringLiteral(":/icons/settings/general.svg");
    general.settings.append(choiceSetting(
        AppSettings::kLanguage, QObject::tr("Language"), QString(),
        {
            { QString(),                QObject::tr("System default") },
            { QStringLiteral("en"),     QStringLiteral("English") },
            { QStringLiteral("hu"),     QStringLiteral("Magyar") },
            { QStringLiteral("de"),     QStringLiteral("Deutsch") },
            { QStringLiteral("fr"),     QStringLiteral("Français") },
            { QStringLiteral("es"),     QStringLiteral("Español") },
        },
        /*restart=*/true));
    general.settings.append(choiceSetting(
        AppSettings::kTheme, QObject::tr("Theme"),
        QString::fromLatin1(AppSettings::kThemeDefault), themeChoices(config),
        /*restart=*/true));
    general.settings.append(choiceSetting(
        AppSettings::kStartupMode, QObject::tr("Start in"),
        QString::fromLatin1(AppSettings::kStartupModeDefault),
        {
            { QStringLiteral("last"),  QObject::tr("Last visited folder") },
            { QStringLiteral("home"),  QObject::tr("Home folder") },
#if defined(__linux__) || defined(__APPLE__)
            { QStringLiteral("root"),  QObject::tr("Root folder (/)") },
#else
            { QStringLiteral("root"),  QObject::tr("Drive list") },
#endif
            { QStringLiteral("fixed"), QObject::tr("This folder:") },
        }));
    {
        SettingDef path = pathSetting(AppSettings::kStartupPath, QObject::tr("Startup folder"));
        path.enabledByKey = QString::fromLatin1(AppSettings::kStartupMode);
        path.enabledByValue = QStringLiteral("fixed");
        general.settings.append(path);
    }
    groups.append(general);

    GroupDef thumbs;
    thumbs.title = QObject::tr("Thumbnails");
    thumbs.icon  = QStringLiteral(":/icons/settings/thumbnails.svg");
    thumbs.settings.append(intSetting(
        AppSettings::kDecoderThreads, QObject::tr("Parallel thumbnail decoders"),
        AppSettings::kDecoderThreadsDefault,
        AppSettings::kDecoderThreadsMin, AppSettings::kDecoderThreadsMax,
        QString(), /*restart=*/true));
    thumbs.settings.append(choiceSetting(
        AppSettings::kCellSize, QObject::tr("Thumbnail cell size"),
        QString::number(AppSettings::kCellSizeDefault),
        {
            { QStringLiteral("128"), QStringLiteral("128 px") },
            { QStringLiteral("192"), QStringLiteral("192 px") },
            { QStringLiteral("256"), QStringLiteral("256 px") },
        }));
    groups.append(thumbs);

    GroupDef viewer;
    viewer.title = QObject::tr("Viewer");
    viewer.icon  = QStringLiteral(":/icons/settings/viewer.svg");
    viewer.settings.append(choiceSetting(
        AppSettings::kViewerWheel, QObject::tr("Mouse wheel"),
        QString::fromLatin1(AppSettings::kViewerWheelDefault),
        {
            { QStringLiteral("navigate"), QObject::tr("Previous / next image (Ctrl + wheel zooms)") },
            { QStringLiteral("zoom"),     QObject::tr("Zoom (Ctrl + wheel changes image)") },
        }));
    viewer.settings.append(choiceSetting(
        AppSettings::kViewerOpenZoom, QObject::tr("Open images at"),
        QString::fromLatin1(AppSettings::kViewerOpenZoomDefault),
        {
            { QStringLiteral("fitLarge"), QObject::tr("Fit to window if larger") },
            { QStringLiteral("fit"),      QObject::tr("Fit to window") },
            { QStringLiteral("actual"),   QObject::tr("Actual size (1:1)") },
        }));
    groups.append(viewer);

    GroupDef fileOps;
    fileOps.title = QObject::tr("File operations");
    fileOps.icon  = QStringLiteral(":/icons/settings/file-operations.svg");
    fileOps.settings.append(boolSetting(
        AppSettings::kSelectAddedFiles, QObject::tr("Select added files"), true));
    fileOps.settings.append(boolSetting(
        AppSettings::kConfirmTrash, QObject::tr("Ask before deleting to the recycle bin"), true));
    fileOps.settings.append(intSetting(
        AppSettings::kSaveQuality, QObject::tr("Save quality (JPEG, WebP)"),
        AppSettings::kSaveQualityDefault, 1, 100));
    groups.append(fileOps);

    return groups;
}

}  // namespace

SettingsDialog::SettingsDialog(Config* config, QWidget* parent)
    : QDialog(parent), _config(config) {
    setObjectName("settingsDialog");
    setWindowTitle(tr("Settings"));
    setModal(false);
    // "Always in front but not blocking": modeless + stays above the main
    // window. The caller shows it with show() and can keep using Pixee.
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    resize(640, 420);

    // ---- top: search ----
    _search = new QLineEdit(this);
    _search->setObjectName("settingsSearch");
    _search->setPlaceholderText(tr("Search settings..."));
    _search->setClearButtonEnabled(true);
    auto* searchBtn = new QPushButton(tr("Search"), this);

    auto* searchRow = new QHBoxLayout();
    searchRow->addWidget(_search, 1);
    searchRow->addWidget(searchBtn, 0);

    // ---- middle: group list | pages ----
    _groupList = new QListWidget(this);
    _groupList->setObjectName("settingsGroupList");
    _groupList->setIconSize(QSize(24, 24));
    _groupList->setMaximumWidth(190);
    _groupList->setSpacing(2);

    _stack = new QStackedWidget(this);

    auto* mid = new QHBoxLayout();
    mid->setSpacing(10);
    mid->addWidget(_groupList, 0);
    mid->addWidget(_stack, 1);

    // ---- bottom: Save / Cancel ----
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);

    auto* outer = new QVBoxLayout(this);
    outer->setSpacing(10);
    outer->addLayout(searchRow);
    outer->addLayout(mid, 1);
    outer->addWidget(buttons);

    build();
    load();
    updateDependentRows();

    connect(_groupList, &QListWidget::currentRowChanged,
            _stack, &QStackedWidget::setCurrentIndex);
    if (_groupList->count() > 0) _groupList->setCurrentRow(0);

    connect(_search, &QLineEdit::textChanged, this, &SettingsDialog::applySearch);
    connect(searchBtn, &QPushButton::clicked, this,
            [this]() { applySearch(_search->text()); });

    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
}

void SettingsDialog::build() {
    const QList<GroupDef> groups = makeRegistry(_config);

    for (int g = 0; g < groups.size(); ++g) {
        const GroupDef& grp = groups.at(g);
        _groupTitlesLower.append(grp.title.toLower());

        new QListWidgetItem(QIcon(grp.icon), grp.title, _groupList);

        auto* page = new QWidget();
        auto* pageLay = new QVBoxLayout(page);
        pageLay->setContentsMargins(12, 12, 12, 12);
        pageLay->setSpacing(8);

        auto* header = new QLabel(grp.title, page);
        header->setObjectName("settingsGroupHeader");
        QFont hf = header->font();
        hf.setBold(true);
        hf.setPointSizeF(hf.pointSizeF() + 1.0);
        header->setFont(hf);
        pageLay->addWidget(header);

        // Column 0 labels, column 1 controls, column 2 takes the slack. One
        // grid per page, so the controls line up down the page whatever the
        // label lengths. A check box carries its own text and spans all
        // three columns, so its text starts where the labels do.
        auto* grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(8);
        grid->setColumnMinimumWidth(1, 180);
        grid->setColumnStretch(2, 1);
        pageLay->addLayout(grid);
        int gridRow = 0;

        for (const SettingDef& s : grp.settings) {
            Row row;
            row.label = s.label;
            row.labelText = s.label.toLower();
            row.groupIndex = g;
            row.key = s.key;
            row.type = s.type;
            row.def = s.def;
            row.restart = s.restart;
            row.enabledByKey = s.enabledByKey;
            row.enabledByValue = s.enabledByValue;

            if (s.type == Bool) {
                auto* cb = new QCheckBox(s.label, page);
                row.check = cb;
                row.labelWidget = cb;
                row.widgets = { cb };
                grid->addWidget(cb, gridRow, 0, 1, 3);
            } else if (s.type == Choice) {
                auto* lab = new QLabel(s.label, page);
                auto* combo = new QComboBox(page);
                for (const auto& c : s.choices) combo->addItem(c.second, c.first);
                row.combo = combo;
                row.labelWidget = lab;
                row.widgets = { lab, combo };
                grid->addWidget(lab, gridRow, 0);
                grid->addWidget(combo, gridRow, 1);
            } else if (s.type == Int) {
                auto* lab = new QLabel(s.label, page);
                auto* spin = new QSpinBox(page);
                spin->setRange(s.min, s.max);
                if (!s.suffix.isEmpty()) spin->setSuffix(s.suffix);
                row.spin = spin;
                row.labelWidget = lab;
                row.widgets = { lab, spin };
                grid->addWidget(lab, gridRow, 0);
                grid->addWidget(spin, gridRow, 1);
            } else {  // Path
                auto* lab = new QLabel(s.label, page);
                // Edit + Browse travel as one cell spanning the control and
                // slack columns, so the edit gets the width and the button
                // keeps its own.
                auto* pathW = new QWidget(page);
                auto* pathLay = new QHBoxLayout(pathW);
                pathLay->setContentsMargins(0, 0, 0, 0);
                pathLay->setSpacing(8);
                auto* edit = new QLineEdit(pathW);
                edit->setClearButtonEnabled(true);
                auto* browse = new QPushButton(tr("Browse..."), pathW);
                // Native folder picker — the same one Copy to / Move to and
                // Save As use. (The no-QFileDialog rule is about the central
                // browser, where the model's behaviour on shares matters.)
                connect(browse, &QPushButton::clicked, this, [this, edit]() {
                    const QString start = edit->text().isEmpty()
                        ? QDir::homePath() : edit->text();
                    const QString dir = QFileDialog::getExistingDirectory(
                        this, tr("Choose the startup folder"), start);
                    if (!dir.isEmpty()) edit->setText(QDir::toNativeSeparators(dir));
                });
                // The Browse button shares the row's enabled state through
                // pathW; the edit is what holds the value.
                row.edit = edit;
                row.labelWidget = lab;
                row.widgets = { lab, pathW };
                pathLay->addWidget(edit, 1);
                pathLay->addWidget(browse, 0);
                grid->addWidget(lab, gridRow, 0);
                grid->addWidget(pathW, gridRow, 1, 1, 2);
            }

            ++gridRow;
            _rows.append(row);
        }

        pageLay->addStretch(1);
        _stack->addWidget(page);
    }

    // Dependent rows follow their controlling Choice live, so the startup
    // path greys out the moment "This folder:" is deselected.
    for (const Row& r : _rows) {
        if (r.enabledByKey.isEmpty() || !r.combo) continue;
        for (const Row& dep : _rows) {
            if (dep.enabledByKey != r.key) continue;
            connect(r.combo, &QComboBox::currentIndexChanged, this,
                    &SettingsDialog::updateDependentRows);
            break;
        }
    }
}

void SettingsDialog::load() {
    QSettings s;
    for (Row& r : _rows) {
        if (r.type == Bool && r.check) {
            r.check->setChecked(s.value(r.key, r.def).toBool());
        } else if (r.type == Choice && r.combo) {
            const QString cur = s.value(r.key, r.def).toString();
            const int idx = r.combo->findData(cur);
            r.combo->setCurrentIndex(idx < 0 ? 0 : idx);
        } else if (r.type == Int && r.spin) {
            // The spin box clamps an out-of-range stored value for us.
            r.spin->setValue(s.value(r.key, r.def).toInt());
        } else if (r.type == Path && r.edit) {
            r.edit->setText(s.value(r.key, r.def).toString());
        }
        r.initial = currentValue(r);
    }
}

QVariant SettingsDialog::currentValue(const Row& r) const {
    if (r.type == Bool && r.check) return r.check->isChecked();
    if (r.type == Choice && r.combo) return r.combo->currentData().toString();
    if (r.type == Int && r.spin) return r.spin->value();
    if (r.type == Path && r.edit) return r.edit->text().trimmed();
    return r.def;
}

void SettingsDialog::updateDependentRows() {
    for (const Row& dep : _rows) {
        if (dep.enabledByKey.isEmpty()) continue;
        bool enabled = true;
        for (const Row& ctl : _rows) {
            if (ctl.key != dep.enabledByKey) continue;
            enabled = currentValue(ctl).toString() == dep.enabledByValue;
            break;
        }
        for (QWidget* w : dep.widgets) w->setEnabled(enabled);
    }
}

void SettingsDialog::save() {
    QSettings s;
    QStringList needRestart;
    for (const Row& r : _rows) {
        const QVariant v = currentValue(r);
        s.setValue(r.key, v);
        if (r.restart && v != r.initial) needRestart << r.label;
    }
    s.sync();
    emit saved();

    if (!needRestart.isEmpty()) {
        QMessageBox::information(this, tr("Restart needed"),
            tr("Restart Pixee to apply: %1.").arg(needRestart.join(QStringLiteral(", "))));
    }
    close();
}

void SettingsDialog::applySearch(const QString& query) {
    const QString q = query.trimmed().toLower();

    // A group matches if its title matches or any of its rows' labels match.
    QVector<bool> groupHasMatch(_groupList->count(), q.isEmpty());
    if (!q.isEmpty()) {
        for (int g = 0; g < _groupTitlesLower.size(); ++g) {
            if (_groupTitlesLower.at(g).contains(q)) groupHasMatch[g] = true;
        }
        for (const Row& r : _rows) {
            if (r.labelText.contains(q)) groupHasMatch[r.groupIndex] = true;
        }
    }

    // Rows: visible when the query is empty, the group title matches, or the
    // row's own label matches. Bold the label on a direct hit.
    for (const Row& r : _rows) {
        const bool titleHit = !q.isEmpty() && _groupTitlesLower.at(r.groupIndex).contains(q);
        const bool labelHit = !q.isEmpty() && r.labelText.contains(q);
        const bool show = q.isEmpty() || titleHit || labelHit;
        for (QWidget* w : r.widgets) w->setVisible(show);
        if (r.labelWidget) {
            QFont f = r.labelWidget->font();
            f.setBold(labelHit);
            r.labelWidget->setFont(f);
        }
    }

    // Group list: hide groups with no match at all.
    for (int g = 0; g < _groupList->count(); ++g) {
        _groupList->item(g)->setHidden(!groupHasMatch.at(g));
    }
    // If the current group was hidden, jump to the first still-visible one.
    QListWidgetItem* cur = _groupList->currentItem();
    if (!cur || cur->isHidden()) {
        for (int g = 0; g < _groupList->count(); ++g) {
            if (!_groupList->item(g)->isHidden()) {
                _groupList->setCurrentRow(g);
                break;
            }
        }
    }
}
