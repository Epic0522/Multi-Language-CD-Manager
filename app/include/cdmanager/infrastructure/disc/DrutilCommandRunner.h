#pragma once

#include <QString>
#include <QStringList>

namespace cdmanager::infrastructure::disc {

struct DrutilCommandResult {
    bool ok {false};
    int exitCode {-1};
    QString stdOut;
    QString stdErr;
};

// 统一封装 drutil 调用，避免 SystemDiscGateway 里到处散落 QProcess 细节。
class DrutilCommandRunner {
public:
    DrutilCommandResult run(const QStringList& arguments) const;
    DrutilCommandResult runWithRetries(
        const QStringList& arguments,
        int attempts,
        int delayMs
    ) const;
};

}  // namespace cdmanager::infrastructure::disc
