#include "TaskTestFixture.h"

#include <QDir>
#include <QList>
#include <QVariant>

TaskTestFixture::TaskTestFixture(int workerCount)
    : mgr(workerCount),
      taskStateSpy(&mgr, &TaskManager::taskStateChanged),
      taskProgressSpy(&mgr, &TaskManager::taskProgress),
      questionSpy(&mgr, &TaskManager::taskQuestionPosed),
      groupRemovedSpy(&mgr, &TaskManager::groupRemoved),
      pathTouchedSpy(&mgr, &TaskManager::pathTouched) {}

QString TaskTestFixture::path() const {
    return tmp.path();
}

QString TaskTestFixture::path(const QString& rel) const {
    return QDir(tmp.path()).filePath(rel);
}

bool TaskTestFixture::waitForGroupRemoved(int timeoutMs) {
    if (groupRemovedSpy.count() > 0) return true;
    return groupRemovedSpy.wait(timeoutMs);
}

bool TaskTestFixture::waitForQuestion(QUuid* outTaskId, int* outKind,
                                      QVariantMap* outContext, int timeoutMs) {
    if (questionSpy.isEmpty()) {
        if (!questionSpy.wait(timeoutMs)) return false;
    }
    const QList<QVariant> args = questionSpy.takeFirst();
    if (outTaskId) *outTaskId = args.value(0).toUuid();
    if (outKind) *outKind = args.value(1).toInt();
    if (outContext) *outContext = args.value(2).toMap();
    return true;
}

int TaskTestFixture::lastStateOf(const QUuid& taskId) const {
    for (int i = taskStateSpy.size() - 1; i >= 0; --i) {
        const QList<QVariant>& args = taskStateSpy.at(i);
        if (args.value(0).toUuid() == taskId) {
            return args.value(1).toInt();
        }
    }
    return -1;
}
