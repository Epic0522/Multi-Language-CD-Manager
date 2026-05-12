#include "cdmanager/infrastructure/burn/DrutilBurner.h"

#include <QProcess>
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>

namespace cdmanager::infrastructure::burn {

namespace {

bool pathLooksLikeImageFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("toc") ||
           suffix == QStringLiteral("iso") ||
           suffix == QStringLiteral("dmg");
}

}

DrutilBurner::DrutilBurner(QObject* parent)
    : QObject(parent) {
}

DrutilBurnResult DrutilBurner::burn(int deviceIndex, const QString& tocFilePath,
                                    bool simulation,
                                    int speedX,
                                    bool standardPregap) {
    DrutilBurnResult result;

    QStringList args;
    args << QStringLiteral("-drive") << QString::number(deviceIndex)
         << QStringLiteral("burn");
    if (simulation) {
        args << QStringLiteral("-test");
    }
    if (speedX > 0) {
        args << QStringLiteral("-speed") << QString::number(speedX);
    }
    if (standardPregap) {
        args << QStringLiteral("-pregap");
    }

    // .toc / .iso / .dmg 这类镜像描述文件应直接交给 drutil 作为映像烧录。
    // 如果这里额外带上 -audio，drutil 会把参数当作“音频目录路径”，
    // 于是报 “Must specify a directory when burning an Audio CD.”
    if (!pathLooksLikeImageFile(tocFilePath)) {
        args << QStringLiteral("-audio");
    }
    args << tocFilePath;

    qDebug() << "DrutilBurner: running drutil" << args;

    QProcess proc;
    proc.start(QStringLiteral("/usr/bin/drutil"), args);
    if (!proc.waitForStarted(5000)) {
        result.error = QStringLiteral("Failed to start drutil.");
        return result;
    }

    proc.closeWriteChannel();

    proc.waitForFinished(-1);  // burn can take a while

    result.stdOut = QString::fromUtf8(proc.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(proc.readAllStandardError());
    result.ok = (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);

    if (!result.ok) {
        result.error = result.stdErr.isEmpty() ? result.stdOut : result.stdErr;
    }

    qDebug() << "DrutilBurner: exit" << proc.exitCode() << "ok =" << result.ok;
    if (!result.stdOut.trimmed().isEmpty()) {
        qDebug().noquote() << "DrutilBurner stdout:\n" << result.stdOut;
    }
    if (!result.stdErr.trimmed().isEmpty()) {
        qDebug().noquote() << "DrutilBurner stderr:\n" << result.stdErr;
    }
    return result;
}

QString DrutilBurner::findDevicePath() {
    QProcess proc;
    proc.start(QStringLiteral("/usr/bin/drutil"), {QStringLiteral("list")});
    proc.waitForFinished(3000);
    QString out = QString::fromUtf8(proc.readAllStandardOutput());
    // Parse the first drive line: " 1  _NEC DVD+-RW ND-6500A"
    const QStringList lines = out.split(u'\n');
    for (const auto& line : lines) {
        const QRegularExpression re(QStringLiteral(R"(^\s*(\d+)\s+)"));
        auto m = re.match(line);
        if (m.hasMatch()) {
            // Found a drive — build a drutil-index:// ID.
            return QStringLiteral("drutil-index://%1").arg(m.captured(1));
        }
    }
    return {};
}

int DrutilBurner::deviceIndexForPath(const QString& path) {
    // path is "drutil-index://N" — extract N.
    const QString prefix = QStringLiteral("drutil-index://");
    if (path.startsWith(prefix)) {
        bool ok = false;
        int idx = path.mid(prefix.size()).toInt(&ok);
        if (ok) return idx;
    }
    // Fallback: parse /dev/rdisk8 — but for drutil we need the index.
    // Try drutil list to find matching BSD name.
    return 1;  // default to first drive
}

}  // namespace cdmanager::infrastructure::burn
