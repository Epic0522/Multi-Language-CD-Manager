#include "cdmanager/infrastructure/disc/DrutilCommandRunner.h"

#include <QProcess>
#include <QThread>

namespace cdmanager::infrastructure::disc {

DrutilCommandResult DrutilCommandRunner::run(const QStringList& arguments) const {
    QProcess process;
    process.start(QStringLiteral("/usr/bin/drutil"), arguments);

    DrutilCommandResult result;
    if (!process.waitForFinished(4000)) {
        result.stdErr = QStringLiteral("drutil execution timed out.");
        return result;
    }

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(process.readAllStandardError());
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return result;
}

DrutilCommandResult DrutilCommandRunner::runWithRetries(
    const QStringList& arguments,
    int attempts,
    int delayMs
) const {
    DrutilCommandResult lastResult;

    for (int attempt = 0; attempt < attempts; ++attempt) {
        lastResult = run(arguments);

        const QString combinedOutput =
            (lastResult.stdOut + QStringLiteral("\n") + lastResult.stdErr).toLower();
        const bool looksBusy =
            combinedOutput.contains(QStringLiteral("media is busy")) ||
            combinedOutput.contains(QStringLiteral("no audio cd present"));

        if (lastResult.ok && !lastResult.stdOut.trimmed().isEmpty()) {
            return lastResult;
        }

        if (!looksBusy || attempt == attempts - 1) {
            return lastResult;
        }

        QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    return lastResult;
}

}  // namespace cdmanager::infrastructure::disc
