#include "cdmanager/infrastructure/burn/CdrecordBurner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace cdmanager::infrastructure::burn {

namespace {

using cdmanager::application::burn::CdTextPack;
using cdmanager::application::burn::kPackTotalSize;

QString quoteCueString(const QString& value) {
    QString escaped = QDir::toNativeSeparators(value);
    escaped.replace(u'"', QStringLiteral("\"\""));
    return escaped;
}

QString detectCueFileType(const QString& audioPath) {
    const QString suffix = QFileInfo(audioPath).suffix().toLower();
    if (suffix == QStringLiteral("aiff") || suffix == QStringLiteral("aif")) {
        return QStringLiteral("AIFF");
    }
    if (suffix == QStringLiteral("wav") || suffix == QStringLiteral("wave")) {
        return QStringLiteral("WAVE");
    }
    return QStringLiteral("BINARY");
}

QString framesToCueTime(int frames) {
    const int minutes = frames / (75 * 60);
    const int seconds = (frames / 75) % 60;
    const int remainingFrames = frames % 75;
    return QStringLiteral("%1:%2:%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(remainingFrames, 2, 10, QLatin1Char('0'));
}

QString cdrecordExecutable() {
    return QStandardPaths::findExecutable(QStringLiteral("cdrecord"));
}

QString scanbusDeviceSpec() {
    const QString exe = cdrecordExecutable();
    if (exe.isEmpty()) {
        return {};
    }

    QProcess proc;
    proc.start(exe, {QStringLiteral("-scanbus")});
    if (!proc.waitForStarted(5000)) {
        return {};
    }
    proc.waitForFinished(15000);

    const QString output = QString::fromLocal8Bit(proc.readAllStandardOutput())
        + QString::fromLocal8Bit(proc.readAllStandardError());

    const QRegularExpression entryRe(
        QStringLiteral(R"((\d+),(\d+),(\d+)\s+\d+\)\s+'.*?'\s+'.*?'\s+'.*?'\s+Removable CD-ROM)"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto match = entryRe.match(output);
    if (!match.hasMatch()) {
        return {};
    }

    return QStringLiteral("%1,%2,%3")
        .arg(match.captured(1), match.captured(2), match.captured(3));
}

std::uint16_t computeCdrecordCrc(const std::uint8_t* data, std::size_t length) {
    std::uint16_t crc = 0x0000;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
        }
    }
    return static_cast<std::uint16_t>(crc ^ 0xFFFF);
}

}

bool CdrecordBurner::isAvailable() {
    return !cdrecordExecutable().isEmpty();
}

QString CdrecordBurner::executablePath() {
    return cdrecordExecutable();
}

QString CdrecordBurner::deviceSpecFor(const QString& driveId, const QString& devicePath) {
    Q_UNUSED(driveId)
    Q_UNUSED(devicePath)

    const QString override = qEnvironmentVariable("CDMANAGER_CDRECORD_DEVICE").trimmed();
    if (!override.isEmpty()) {
        return override;
    }

    const QString scanned = scanbusDeviceSpec();
    if (!scanned.isEmpty()) {
        return scanned;
    }

    return QStringLiteral("IODVDServices/0");
}

QByteArray CdrecordBurner::buildCdTextBinary(const QVector<CdTextPack>& packs) {
    QByteArray body;
    body.reserve(packs.size() * kPackTotalSize);

    for (const auto& pack : packs) {
        std::array<std::uint8_t, kPackTotalSize> copy = pack.data;
        const std::uint16_t crc = computeCdrecordCrc(copy.data(), 16);
        copy[16] = static_cast<std::uint8_t>(crc >> 8);
        copy[17] = static_cast<std::uint8_t>(crc & 0xFF);
        body.append(reinterpret_cast<const char*>(copy.data()), kPackTotalSize);
    }

    const int totalLength = body.size() + 4;
    QByteArray output;
    output.reserve(totalLength);
    output.append(static_cast<char>((totalLength - 2) >> 8));
    output.append(static_cast<char>((totalLength - 2) & 0xFF));
    output.append('\0');
    output.append('\0');
    output.append(body);
    return output;
}

bool CdrecordBurner::writeCdTextFile(const QString& path,
                                     const QVector<CdTextPack>& packs,
                                     QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not create temporary cdrecord CD-TEXT file.");
        }
        return false;
    }

    const QByteArray payload = buildCdTextBinary(packs);
    if (file.write(payload) != payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not fully write temporary cdrecord CD-TEXT file.");
        }
        return false;
    }

    return true;
}

QString CdrecordBurner::buildCueText(const QStringList& audioFiles, int trackGapSeconds) {
    QString cueText;
    QTextStream out(&cueText);

    const int pregapFrames = qMax(trackGapSeconds, 0) * 75;
    for (int index = 0; index < audioFiles.size(); ++index) {
        const QString& audioFile = audioFiles.at(index);
        out << "FILE \"" << quoteCueString(audioFile) << "\" " << detectCueFileType(audioFile) << "\n";
        out << "  TRACK " << QString::number(index + 1).rightJustified(2, u'0') << " AUDIO\n";
        if (index > 0 && pregapFrames > 0) {
            out << "    PREGAP " << framesToCueTime(pregapFrames) << "\n";
        }
        out << "    INDEX 01 00:00:00\n";
    }

    return cueText;
}

bool CdrecordBurner::writeCueFile(const QString& cuePath,
                                  const QStringList& audioFiles,
                                  int trackGapSeconds,
                                  QString* errorMessage) {
    if (audioFiles.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("No prepared audio files for cue sheet.");
        }
        return false;
    }

    QFile cueFile(cuePath);
    if (!cueFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not create temporary cue file.");
        }
        return false;
    }

    cueFile.write(buildCueText(audioFiles, trackGapSeconds).toUtf8());
    return true;
}

void CdrecordBurner::setSimulationMode(bool on) { m_simulationMode = on; }
void CdrecordBurner::setBurnSpeed(int speedX) { m_burnSpeed = speedX; }
void CdrecordBurner::setAllowOverburn(bool on) { m_allowOverburn = on; }
void CdrecordBurner::setProgressCallback(ProgressCb cb) { m_progressCb = std::move(cb); }

BurnResult CdrecordBurner::burn(const QString& deviceSpec,
                                const QString& cueFilePath,
                                const QString& cdTextFilePath) {
    BurnResult result;

    const QString exe = cdrecordExecutable();
    if (exe.isEmpty()) {
        result.error = QStringLiteral("cdrecord is not installed.");
        return result;
    }
    if (deviceSpec.trimmed().isEmpty()) {
        result.error = QStringLiteral("No cdrecord device spec resolved.");
        return result;
    }
    if (!QFileInfo::exists(cueFilePath)) {
        result.error = QStringLiteral("Temporary CUE file does not exist.");
        return result;
    }
    if (!QFileInfo::exists(cdTextFilePath)) {
        result.error = QStringLiteral("Temporary CD-TEXT blob does not exist.");
        return result;
    }

    QStringList args;
    args << QStringLiteral("-v")
         << QStringLiteral("dev=%1").arg(deviceSpec)
         << QStringLiteral("-eject")
         << QStringLiteral("driveropts=burnfree")
         << QStringLiteral("-raw96r")
         << QStringLiteral("-text")
         << QStringLiteral("textfile=%1").arg(cdTextFilePath)
         << QStringLiteral("cuefile=%1").arg(cueFilePath);

    if (m_simulationMode) {
        args << QStringLiteral("-dummy");
    }
    if (m_burnSpeed > 0) {
        args << QStringLiteral("speed=%1").arg(m_burnSpeed);
    }
    if (m_allowOverburn) {
        args << QStringLiteral("-overburn");
    }

    result.diagnostics = QStringLiteral(
        "Backend: cdrecord\nExecutable: %1\nDevice: %2\nSimulation: %3\nSpeed: %4\nMode: RAW96R\nCue path: %5\nCD-TEXT path: %6")
        .arg(exe,
             deviceSpec,
             m_simulationMode ? QStringLiteral("yes") : QStringLiteral("no"),
             m_burnSpeed > 0 ? QStringLiteral("%1x").arg(m_burnSpeed) : QStringLiteral("max"),
             cueFilePath,
             cdTextFilePath);

    QProcess proc;
    proc.start(exe, args);
    if (!proc.waitForStarted(5000)) {
        result.error = QStringLiteral("Failed to start cdrecord.");
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
        result.error = !stdErr.trimmed().isEmpty() ? stdErr.trimmed() : stdOut.trimmed();
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("cdrecord write failed.");
        }
    }

    return result;
}

void CdrecordBurner::handleOutputChunk(const QByteArray& chunk,
                                       bool stderrStream,
                                       QString* aggregate) {
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

void CdrecordBurner::processOutputLine(const QString& line, bool stderrStream) {
    Q_UNUSED(stderrStream)
    if (line.isEmpty() || m_progressCb == nullptr) {
        return;
    }

    BurnProgress progress;

    const QRegularExpression trackRe(
        QStringLiteral(R"((Track|Writing track)\s+(\d+))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto trackMatch = trackRe.match(line);
    if (trackMatch.hasMatch()) {
        progress.trackIndex = trackMatch.captured(2).toInt();
        progress.phase = QStringLiteral("Writing track %1").arg(progress.trackIndex);
        progress.overallPercent = 35.f + (progress.trackIndex * 10.f);
        m_progressCb(progress);
        return;
    }

    if (line.contains(QStringLiteral("Fixating"), Qt::CaseInsensitive)
        || line.contains(QStringLiteral("Writing lead-out"), Qt::CaseInsensitive)) {
        progress.phase = QStringLiteral("Closing");
        progress.overallPercent = 92.f;
        m_progressCb(progress);
        return;
    }

    if (line.contains(QStringLiteral("Operation starts"), Qt::CaseInsensitive)
        || line.contains(QStringLiteral("Sending CUE sheet"), Qt::CaseInsensitive)) {
        progress.phase = line;
        progress.overallPercent = 32.f;
        m_progressCb(progress);
        return;
    }

    if (line.contains(QStringLiteral("done"), Qt::CaseInsensitive)
        || line.contains(QStringLiteral("synchronized cache"), Qt::CaseInsensitive)) {
        progress.phase = QStringLiteral("Done");
        progress.overallPercent = 100.f;
        m_progressCb(progress);
    }
}

}  // namespace cdmanager::infrastructure::burn
