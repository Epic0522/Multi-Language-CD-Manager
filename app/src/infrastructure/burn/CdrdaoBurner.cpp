#include "cdmanager/infrastructure/burn/CdrdaoBurner.h"

#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace cdmanager::infrastructure::burn {

namespace {

QString cdrdaoExecutable() {
    return QStandardPaths::findExecutable(QStringLiteral("cdrdao"));
}

QString cdrdaoDriverSpec() {
    const QString override = qEnvironmentVariable("CDMANAGER_CDRDAO_DRIVER").trimmed();
    if (!override.isEmpty()) {
        return override;
    }

    // 0x10 这条线在第四次测试里反而把盘结构写得更怪，先退回 raw。
    // 至少 raw 路线已经验证过：macOS 能完整读出日文 CD-TEXT。
    return QStringLiteral("generic-mmc-raw");
}

int cdrdaoBufferCount() {
    bool ok = false;
    const int configured = qEnvironmentVariable("CDMANAGER_CDRDAO_BUFFERS").trimmed().toInt(&ok);
    if (ok && configured >= 10) {
        return configured;
    }
    return 64;
}

int effectiveCdrdaoSpeed(int requestedSpeedX, bool simulationMode) {
    if (simulationMode) {
        return requestedSpeedX;
    }

    if (requestedSpeedX <= 0) {
        return 8;
    }

    return qMin(requestedSpeedX, 8);
}

}

bool CdrdaoBurner::isAvailable() {
    return !cdrdaoExecutable().isEmpty();
}

QString CdrdaoBurner::currentDriverSpec() {
    return cdrdaoDriverSpec();
}

QString CdrdaoBurner::deviceSpecFor(const QString& driveId, const QString& devicePath) {
    const QString override = qEnvironmentVariable("CDMANAGER_CDRDAO_DEVICE");
    if (!override.trimmed().isEmpty()) {
        return override.trimmed();
    }

    const QString prefix = QStringLiteral("drutil-index://");
    if (driveId.startsWith(prefix)) {
        bool ok = false;
        const int oneBasedIndex = driveId.mid(prefix.size()).toInt(&ok);
        if (ok) {
            const int compatibilityBus = qMax(oneBasedIndex - 1, 0);
            return QStringLiteral("%1,0,0").arg(compatibilityBus);
        }
    }

    if (!devicePath.trimmed().isEmpty()) {
        return devicePath.trimmed();
    }

    return {};
}

void CdrdaoBurner::setSimulationMode(bool on) { m_simulationMode = on; }
void CdrdaoBurner::setBurnSpeed(int speedX) { m_burnSpeed = speedX; }
void CdrdaoBurner::setAllowOverburn(bool on) { m_allowOverburn = on; }
void CdrdaoBurner::setProgressCallback(ProgressCb cb) { m_progressCb = std::move(cb); }

BurnResult CdrdaoBurner::burn(const QString& deviceSpec, const QString& tocFilePath) {
    BurnResult result;

    const QString exe = cdrdaoExecutable();
    if (exe.isEmpty()) {
        result.error = QStringLiteral("cdrdao is not installed.");
        return result;
    }
    if (deviceSpec.trimmed().isEmpty()) {
        result.error = QStringLiteral("No cdrdao device spec resolved.");
        return result;
    }
    if (!QFileInfo::exists(tocFilePath)) {
        result.error = QStringLiteral("Temporary TOC file does not exist.");
        return result;
    }

    QStringList args;
    const QString driverSpec = cdrdaoDriverSpec();
    const int effectiveSpeed = effectiveCdrdaoSpeed(m_burnSpeed, m_simulationMode);
    const int bufferCount = cdrdaoBufferCount();
    args << QStringLiteral("write")
         << QStringLiteral("--device") << deviceSpec
         << QStringLiteral("--driver") << driverSpec
         << QStringLiteral("--buffers") << QString::number(bufferCount)
         << QStringLiteral("--eject")
         << QStringLiteral("-n");

    if (m_simulationMode) {
        args << QStringLiteral("--simulate");
    }
    if (effectiveSpeed > 0) {
        args << QStringLiteral("--speed") << QString::number(effectiveSpeed);
    }
    if (m_allowOverburn) {
        args << QStringLiteral("--overburn");
    }
    args << tocFilePath;

    result.diagnostics = QStringLiteral("Backend: cdrdao\nExecutable: %1\nDevice: %2\nDriver: %3\nSimulation: %4\nRequested speed: %5\nEffective speed: %6\nBuffers: %7\nTOC path: %8")
        .arg(exe)
        .arg(deviceSpec)
        .arg(driverSpec)
        .arg(m_simulationMode ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(m_burnSpeed > 0 ? QStringLiteral("%1x").arg(m_burnSpeed)
                             : QStringLiteral("max"))
        .arg(effectiveSpeed > 0 ? QStringLiteral("%1x").arg(effectiveSpeed)
                                : QStringLiteral("max"))
        .arg(bufferCount)
        .arg(tocFilePath);

    qDebug() << "CdrdaoBurner: running" << exe << args;

    QProcess proc;
    proc.start(exe, args);
    if (!proc.waitForStarted(5000)) {
        result.error = QStringLiteral("Failed to start cdrdao.");
        return result;
    }

    QString stdOut;
    QString stdErr;
    while (proc.state() != QProcess::NotRunning) {
        proc.waitForReadyRead(200);
        handleOutputChunk(proc.readAllStandardOutput(), false, &stdOut);
        handleOutputChunk(proc.readAllStandardError(), true, &stdErr);
        proc.waitForFinished(50);
    }
    handleOutputChunk(proc.readAllStandardOutput(), false, &stdOut);
    handleOutputChunk(proc.readAllStandardError(), true, &stdErr);

    result.ok = (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
    if (!stdOut.trimmed().isEmpty()) {
        result.diagnostics += QStringLiteral("\n\nstdout:\n%1").arg(stdOut.trimmed());
    }
    if (!stdErr.trimmed().isEmpty()) {
        result.diagnostics += QStringLiteral("\n\nstderr:\n%1").arg(stdErr.trimmed());
    }

    if (!result.ok) {
        result.error = !stdErr.trimmed().isEmpty() ? stdErr.trimmed()
                                                   : stdOut.trimmed();
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("cdrdao write failed.");
        }
    }

    return result;
}

void CdrdaoBurner::handleOutputChunk(const QByteArray& chunk, bool stderrStream, QString* aggregate) {
    if (chunk.isEmpty() || aggregate == nullptr) {
        return;
    }

    const QString text = QString::fromLocal8Bit(chunk);
    aggregate->append(text);

    QString normalized = text;
    normalized.replace(QStringLiteral("\r"), QStringLiteral("\n"));
    const QStringList lines = normalized.split(u'\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        processOutputLine(line.trimmed(), stderrStream);
    }
}

void CdrdaoBurner::processOutputLine(const QString& line, bool stderrStream) {
    if (line.isEmpty() || m_progressCb == nullptr) {
        return;
    }

    BurnProgress progress;

    const QRegularExpression trackRe(QStringLiteral(R"(Writing track\s+(\d+))"),
                                     QRegularExpression::CaseInsensitiveOption);
    const auto trackMatch = trackRe.match(line);
    if (trackMatch.hasMatch()) {
        progress.trackIndex = trackMatch.captured(1).toInt();
        progress.phase = QStringLiteral("Writing track %1").arg(progress.trackIndex);
        progress.overallPercent = 35.f + (progress.trackIndex * 10.f);
        m_progressCb(progress);
        return;
    }

    const QRegularExpression closingRe(QStringLiteral(R"(Closing.*?(\d+)%?)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto closingMatch = closingRe.match(line);
    if (closingMatch.hasMatch()) {
        const int closingPercent = closingMatch.captured(1).toInt();
        progress.phase = QStringLiteral("Closing");
        progress.overallPercent = 90.f + qBound(0, closingPercent, 100) / 10.f;
        m_progressCb(progress);
        return;
    }

    if (line.contains(QStringLiteral("Wrote"), Qt::CaseInsensitive)
        || line.contains(QStringLiteral("Writing"), Qt::CaseInsensitive)) {
        progress.phase = line;
        progress.overallPercent = 50.f;
        m_progressCb(progress);
        return;
    }

    if (!stderrStream && line.contains(QStringLiteral("done"), Qt::CaseInsensitive)) {
        progress.phase = QStringLiteral("Done");
        progress.overallPercent = 100.f;
        m_progressCb(progress);
    }
}

}  // namespace cdmanager::infrastructure::burn
