#ifndef TASK_TEST_FIXTURE_H
#define TASK_TEST_FIXTURE_H

#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariantMap>

#include "TaskManager.h"

// Per-test scratch dir + a real TaskManager, bundled together with
// pre-wired QSignalSpies on every signal a file-op test typically cares
// about. Construct fresh per test method (no init/cleanup boilerplate)
// so each test's lifetime is explicit.
class TaskTestFixture {
public:
    explicit TaskTestFixture(int workerCount = 2);

    // tmp.path() shorthand. The overload joins a relative path under
    // the scratch dir using QDir::filePath (handles separator concerns).
    QString path() const;
    QString path(const QString& rel) const;

    // Members (declaration order matters — mgr must be constructed
    // before the spies that reference &mgr).
    QTemporaryDir tmp;
    TaskManager mgr;

    QSignalSpy taskStateSpy;       // (QUuid, int)        — every transition
    QSignalSpy taskProgressSpy;    // (QUuid, int)
    QSignalSpy questionSpy;        // (QUuid, int, QVariantMap)
    QSignalSpy groupRemovedSpy;    // (QUuid)              — fires on all-terminal
    QSignalSpy pathTouchedSpy;     // (QString)

    // Block until the manager's groupRemoved signal fires (or timeout).
    // The manager auto-removes a group once every task in it reaches a
    // terminal state, so this is the cleanest "task reached terminal"
    // wait.
    bool waitForGroupRemoved(int timeoutMs = 5000);

    // Block until a question is posed; returns the question kind and
    // context via the out parameters. Pops the entry off the spy so
    // subsequent calls wait for the next question.
    bool waitForQuestion(QUuid* outTaskId, int* outKind,
                         QVariantMap* outContext, int timeoutMs = 2000);

    // Most-recent state recorded by taskStateSpy for the given task.
    // Returns -1 if no state has been observed for that task (e.g., it
    // never made it past Queued and the manager hasn't published a
    // transition yet). Useful for asserting the terminal state after
    // waitForGroupRemoved.
    int lastStateOf(const QUuid& taskId) const;
};

#endif
