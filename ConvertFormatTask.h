#ifndef CONVERTFORMATTASK_H
#define CONVERTFORMATTASK_H

#include <QByteArray>
#include <QString>

#include "Task.h"

// Decode src and re-encode in a different format. Output extension is taken
// from `targetFormat` (e.g. "png", "jpg", "webp").
class ConvertFormatTask : public Task
{
    Q_OBJECT
public:
    ConvertFormatTask(const QString& sourcePath, const QString& destPath,
                      const QByteArray& targetFormat, int jpegQuality,
                      TaskGroup* group, QObject* parent = nullptr);

    QString displayName() const override;

protected:
    void run() override;

private:
    QString _src;
    QString _dst;
    QByteArray _format;
    int _jpegQuality;
};

#endif // CONVERTFORMATTASK_H
