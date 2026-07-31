#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVector>

#include "AppSettings.h"

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
};
struct GroupDef {
    QString title;
    QString icon;
    QList<SettingDef> settings;
};

QList<GroupDef> makeRegistry() {
    QList<GroupDef> groups;

    GroupDef fileOps;
    fileOps.title = QObject::tr("File operations");
    fileOps.icon  = QStringLiteral(":/icons/settings/file-operations.svg");
    fileOps.settings.append({
        QString::fromLatin1(AppSettings::kSelectAddedFiles),
        QObject::tr("Select added files"),
        0 /*Bool*/, true, {} });
    groups.append(fileOps);

    GroupDef general;
    general.title = QObject::tr("General");
    general.icon  = QStringLiteral(":/icons/settings/general.svg");
    general.settings.append({
        QString::fromLatin1(AppSettings::kLanguage),
        QObject::tr("Language"),
        1 /*Choice*/, QString(),
        {
            { QString(),                QObject::tr("System default") },
            { QStringLiteral("en"),     QStringLiteral("English") },
            { QStringLiteral("hu"),     QStringLiteral("Magyar") },
            { QStringLiteral("de"),     QStringLiteral("Deutsch") },
            { QStringLiteral("fr"),     QStringLiteral("Français") },
            { QStringLiteral("es"),     QStringLiteral("Español") },
        } });
    groups.append(general);

    return groups;
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("settingsDialog");
    setWindowTitle(tr("Settings"));
    setModal(false);
    // "Always in front but not blocking": modeless + stays above the main
    // window. The caller shows it with show() and can keep using Pixee.
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    resize(580, 400);

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
    const QList<GroupDef> groups = makeRegistry();

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

        for (const SettingDef& s : grp.settings) {
            auto* rowW = new QWidget(page);
            auto* rowLay = new QHBoxLayout(rowW);
            rowLay->setContentsMargins(0, 0, 0, 0);
            rowLay->setSpacing(8);

            Row row;
            row.container = rowW;
            row.labelText = s.label.toLower();
            row.groupIndex = g;
            row.key = s.key;
            row.type = s.type;
            row.def = s.def;

            if (s.type == Bool) {
                auto* cb = new QCheckBox(s.label, rowW);
                row.check = cb;
                row.labelWidget = cb;
                rowLay->addWidget(cb);
                rowLay->addStretch(1);
            } else {  // Choice
                auto* lab = new QLabel(s.label, rowW);
                auto* combo = new QComboBox(rowW);
                for (const auto& c : s.choices) combo->addItem(c.second, c.first);
                row.combo = combo;
                row.labelWidget = lab;
                rowLay->addWidget(lab);
                rowLay->addWidget(combo, 0);
                rowLay->addStretch(1);
            }

            pageLay->addWidget(rowW);
            _rows.append(row);
        }

        pageLay->addStretch(1);
        _stack->addWidget(page);
    }
}

void SettingsDialog::load() {
    QSettings s;
    for (const Row& r : _rows) {
        if (r.type == Bool && r.check) {
            r.check->setChecked(s.value(r.key, r.def).toBool());
        } else if (r.type == Choice && r.combo) {
            const QString cur = s.value(r.key, r.def).toString();
            const int idx = r.combo->findData(cur);
            r.combo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
    }
    _initialLanguage = s.value(QString::fromLatin1(AppSettings::kLanguage)).toString();
}

void SettingsDialog::save() {
    QSettings s;
    for (const Row& r : _rows) {
        if (r.type == Bool && r.check) {
            s.setValue(r.key, r.check->isChecked());
        } else if (r.type == Choice && r.combo) {
            s.setValue(r.key, r.combo->currentData().toString());
        }
    }
    s.sync();

    const QString newLanguage =
        s.value(QString::fromLatin1(AppSettings::kLanguage)).toString();
    if (newLanguage != _initialLanguage) {
        QMessageBox::information(this, tr("Language changed"),
            tr("Restart Pixee to apply the new language."));
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
        r.container->setVisible(q.isEmpty() || titleHit || labelHit);
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
