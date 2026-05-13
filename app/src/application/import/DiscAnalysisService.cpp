#include "cdmanager/application/import/DiscAnalysisService.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "cdmanager/infrastructure/audio/AudioCdReader.h"
#include "cdmanager/infrastructure/burn/CdrdaoBurner.h"
#include "cdmanager/infrastructure/disc/DrutilCommandRunner.h"
#include "cdmanager/infrastructure/disc/DrutilOutputParser.h"

#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cdmanager::application::import {

namespace {

using cdmanager::domain::project::Track;
using cdmanager::infrastructure::burn::CdrdaoBurner;
using cdmanager::infrastructure::disc::DrutilCommandResult;
using cdmanager::infrastructure::disc::DrutilCommandRunner;
using cdmanager::infrastructure::disc::DrutilOutputParser;

struct CdrdaoDiskInfoSummary {
    bool parsed {false};
    bool appendable {false};
    int sessions {0};
    int lastTrack {0};
    QString tocType;
    QString mediumType;
};

struct CdTextSummary {
    bool found {false};
    QString title;
    QString performer;
    QVector<int> sizeInfoValues;
    QString rawOutput;
    QString rawError;
};

struct StatusSummary {
    QString mediaType;
    QString devicePath;
    int sessions {0};
    int tracks {0};
    QString spaceUsed;
    QString spaceFree;
    QString rawOutput;
    QString rawError;
};

struct DiskutilListSummary {
    bool parsed {false};
    bool hasFreeSpaceEntry {false};
    QString freeSpaceText;
};

int parseMsfFrames(const QString& text) {
    const QStringList parts = text.split(u':');
    if (parts.size() != 3) {
        return -1;
    }
    bool ok0 = false;
    bool ok1 = false;
    bool ok2 = false;
    const int mm = parts.at(0).toInt(&ok0);
    const int ss = parts.at(1).toInt(&ok1);
    const int ff = parts.at(2).toInt(&ok2);
    if (!ok0 || !ok1 || !ok2) {
        return -1;
    }
    return (mm * 60 + ss) * 75 + ff;
}

QString msfFromSeconds(int totalSeconds) {
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString rawDevicePathFor(const StatusSummary& status) {
    if (status.devicePath.startsWith(QStringLiteral("/dev/disk"))) {
        QString raw = status.devicePath;
        raw.replace(QStringLiteral("/dev/disk"), QStringLiteral("/dev/rdisk"));
        return raw;
    }
    return status.devicePath;
}

QStringList selectorForDeviceId(const QString& deviceId) {
    const QString prefix = QStringLiteral("drutil-index://");
    if (deviceId.startsWith(prefix)) {
        const QString index = deviceId.mid(prefix.size());
        if (!index.isEmpty()) {
            return {QStringLiteral("-drive"), index};
        }
    }
    return {QStringLiteral("-drive"), QStringLiteral("external")};
}

QVector<QStringList> selectorFallbacksForDeviceId(const QString& deviceId) {
    return {
        selectorForDeviceId(deviceId),
        {QStringLiteral("-drive"), QStringLiteral("external")},
        {}
    };
}

DrutilCommandResult runDrutilWithSelectorFallback(
    const DrutilCommandRunner& runner,
    const QString& deviceId,
    const QStringList& commandArguments,
    int attempts = 1,
    int delayMs = 0
) {
    DrutilCommandResult bestResult;
    for (const auto& selector : selectorFallbacksForDeviceId(deviceId)) {
        const auto result =
            attempts > 1
                ? runner.runWithRetries(selector + commandArguments, attempts, delayMs)
                : runner.run(selector + commandArguments);
        if (result.ok && !result.stdOut.trimmed().isEmpty()) {
            return result;
        }
        bestResult = result;
    }
    return bestResult;
}

QString readOptionalTextFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

DiskutilListSummary parseDiskutilListSummary(const QString& output) {
    DiskutilListSummary summary;
    if (output.trimmed().isEmpty()) {
        return summary;
    }

    summary.parsed = true;
    const QRegularExpression freeSpaceRe(
        QStringLiteral(R"(\(free space\)\s+([0-9.]+\s+[A-Z]+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto match = freeSpaceRe.match(output);
    if (match.hasMatch()) {
        summary.hasFreeSpaceEntry = true;
        summary.freeSpaceText = match.captured(1).trimmed();
    }
    return summary;
}

StatusSummary parseStatusSummary(const QString& statusOutput, const QString& statusError) {
    StatusSummary summary;
    summary.rawOutput = statusOutput.trimmed();
    summary.rawError = statusError.trimmed();

    const auto capture = [&](const QString& pattern) -> QString {
        const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        const auto match = re.match(statusOutput);
        return match.hasMatch() ? match.captured(1).trimmed() : QString();
    };

    summary.mediaType = capture(QStringLiteral(R"(Type:\s*([^\n\r]+))"));
    summary.devicePath = capture(QStringLiteral(R"(Name:\s*(/dev/[^\s]+))"));
    summary.sessions = capture(QStringLiteral(R"(Sessions:\s*(\d+))")).toInt();
    summary.tracks = capture(QStringLiteral(R"(Tracks:\s*(\d+))")).toInt();
    summary.spaceUsed = capture(QStringLiteral(R"(Space Used:\s*([0-9:]+))"));
    summary.spaceFree = capture(QStringLiteral(R"(Space Free:\s*([0-9:]+))"));
    return summary;
}

CdTextSummary parseCdTextSummary(const QString& output, const QString& error = {}) {
    CdTextSummary summary;
    summary.rawOutput = output.trimmed();
    summary.rawError = error.trimmed();
    if (summary.rawOutput.isEmpty()) {
        return summary;
    }

    const auto captureQuoted = [&](const QString& label) -> QString {
        const QRegularExpression re(
            QStringLiteral("%1\\s+\"(.*)\"").arg(QRegularExpression::escape(label)),
            QRegularExpression::MultilineOption
        );
        const auto match = re.match(output);
        return match.hasMatch() ? match.captured(1) : QString();
    };

    summary.title = captureQuoted(QStringLiteral("TITLE"));
    summary.performer = captureQuoted(QStringLiteral("PERFORMER"));

    const QRegularExpression sizeInfoRe(
        QStringLiteral(R"(SIZE_INFO\s*\{([^}]*)\})"),
        QRegularExpression::DotMatchesEverythingOption
    );
    const auto sizeInfoMatch = sizeInfoRe.match(output);
    if (sizeInfoMatch.hasMatch()) {
        const QString body = sizeInfoMatch.captured(1);
        const QRegularExpression numRe(QStringLiteral(R"((-?\d+))"));
        auto numIt = numRe.globalMatch(body);
        while (numIt.hasNext()) {
            summary.sizeInfoValues.append(numIt.next().captured(1).toInt());
        }
        summary.found = true;
    }

    if (!summary.title.isEmpty() || !summary.performer.isEmpty()
        || output.contains(QStringLiteral("Found CD-TEXT data."), Qt::CaseInsensitive)
        || output.contains(QStringLiteral("DRCDText"), Qt::CaseInsensitive)
        || output.contains(QStringLiteral("<plist"))) {
        summary.found = true;
    }

    return summary;
}

QVector<DiscAnalysisTrackLayout> parseCdrdaoReadToc(const QString& output) {
    QVector<DiscAnalysisTrackLayout> tracks;
    const QRegularExpression re(
        QStringLiteral(R"(^\s*(\d+)\s+([A-Z0-9_-]+)\s+\d+\s+([0-9:]+\([ \d]+\))\s+([0-9:]+\([ \d]+\)))"),
        QRegularExpression::MultilineOption
    );
    auto it = re.globalMatch(output);
    while (it.hasNext()) {
        const auto match = it.next();
        DiscAnalysisTrackLayout info;
        info.trackNumber = match.captured(1).toInt();
        info.mode = match.captured(2);

        const QRegularExpression fieldRe(QStringLiteral(R"(([0-9:]+)\(\s*(\d+)\))"));
        const auto startMatch = fieldRe.match(match.captured(3));
        const auto lengthMatch = fieldRe.match(match.captured(4));
        if (startMatch.hasMatch()) {
            info.startMsf = startMatch.captured(1);
            info.startLsn = startMatch.captured(2).toInt();
        }
        if (lengthMatch.hasMatch()) {
            info.lengthMsf = lengthMatch.captured(1);
            info.lengthSectors = lengthMatch.captured(2).toInt();
        }
        tracks.append(info);
    }
    return tracks;
}

CdrdaoDiskInfoSummary parseCdrdaoDiskInfo(const QString& output) {
    CdrdaoDiskInfoSummary summary;
    if (output.trimmed().isEmpty()) {
        return summary;
    }

    summary.parsed = true;
    const auto capture = [&](const QString& pattern) -> QString {
        const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        const auto match = re.match(output);
        return match.hasMatch() ? match.captured(1).trimmed() : QString();
    };

    summary.mediumType = capture(QStringLiteral(R"(CD-R medium\s*:\s*([^\n\r]+))"));
    summary.tocType = capture(QStringLiteral(R"(Toc Type\s*:\s*([^\n\r]+))"));
    summary.sessions = capture(QStringLiteral(R"(Sessions\s*:\s*(\d+))")).toInt();
    summary.lastTrack = capture(QStringLiteral(R"(Last Track\s*:\s*(\d+))")).toInt();
    summary.appendable = capture(QStringLiteral(R"(Appendable\s*:\s*(yes|no))"))
                             .compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
    return summary;
}

#ifdef __APPLE__
DiscAnalysisProbePoint probeSectorWindow(int fd, const QString& label, int lsn, int sectorsToRead) {
    DiscAnalysisProbePoint result;
    result.label = label;
    result.lsn = lsn;
    result.attempted = true;

    if (lsn < 0) {
        result.error = QStringLiteral("negative-lsn");
        return result;
    }

    constexpr int kBytesPerSector = 2352;
    const qsizetype totalBytes = sectorsToRead * kBytesPerSector;
    QByteArray buffer(totalBytes, '\0');
    const off_t offset = static_cast<off_t>(lsn) * kBytesPerSector;

    size_t bytesRead = 0;
    while (bytesRead < static_cast<size_t>(totalBytes)) {
        const ssize_t n = pread(fd,
                                buffer.data() + bytesRead,
                                static_cast<size_t>(totalBytes) - bytesRead,
                                offset + static_cast<off_t>(bytesRead));
        if (n <= 0) {
            result.error = QStringLiteral("pread-failed");
            return result;
        }
        bytesRead += static_cast<size_t>(n);
    }

    result.ok = true;
    result.allZero = true;
    for (const char byte : buffer) {
        if (byte != '\0') {
            result.allZero = false;
            break;
        }
    }
    return result;
}
#endif

QVector<DiscAnalysisTrackProbe> probeAudioTracks(
    const QString& rawDevicePath,
    const QVector<DiscAnalysisTrackLayout>& tracks
) {
    QVector<DiscAnalysisTrackProbe> results;
#ifdef __APPLE__
    if (rawDevicePath.isEmpty() || tracks.isEmpty()) {
        return results;
    }

    const int fd = open(rawDevicePath.toUtf8().constData(), O_RDONLY);
    if (fd < 0) {
        return results;
    }

    for (const auto& track : tracks) {
        if (track.startLsn < 0 || track.lengthSectors <= 0) {
            continue;
        }

        DiscAnalysisTrackProbe probe;
        probe.trackNumber = track.trackNumber;

        const int startProbe = track.startLsn + qMin(150, qMax(track.lengthSectors / 10, 1));
        const int middleProbe = track.startLsn + (track.lengthSectors / 2);
        const int endProbe = track.startLsn + qMax(track.lengthSectors - 151, 0);

        probe.points.append(probeSectorWindow(fd, QStringLiteral("start"), startProbe, 8));
        probe.points.append(probeSectorWindow(fd, QStringLiteral("middle"), middleProbe, 8));
        probe.points.append(probeSectorWindow(fd, QStringLiteral("end"), endProbe, 8));
        results.append(probe);
    }

    close(fd);
#else
    Q_UNUSED(rawDevicePath)
    Q_UNUSED(tracks)
#endif
    return results;
}

QString chooseDriveId(const QVector<cdmanager::domain::disc::DriveInfo>& drives,
                      const QString& requestedId) {
    if (!requestedId.trimmed().isEmpty()) {
        return requestedId.trimmed();
    }
    for (const auto& drive : drives) {
        if (drive.hasMediaLoaded) {
            return drive.deviceId;
        }
    }
    return drives.isEmpty() ? QString() : drives.first().deviceId;
}

DrutilCommandResult runCdrdaoSimple(const QStringList& args) {
    DrutilCommandResult result;
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("cdrdao"));
    if (exe.isEmpty()) {
        result.stdErr = QStringLiteral("cdrdao not installed.");
        return result;
    }

    QProcess process;
    process.start(exe, args);
    if (!process.waitForFinished(10000)) {
        result.stdErr = QStringLiteral("cdrdao execution timed out.");
        return result;
    }

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.stdErr = QString::fromLocal8Bit(process.readAllStandardError());
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return result;
}

DrutilCommandResult runDiskutilListForDevice(const QString& devicePath) {
    DrutilCommandResult result;
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("diskutil"));
    if (exe.isEmpty() || devicePath.trimmed().isEmpty()) {
        result.stdErr = QStringLiteral("diskutil unavailable or device path missing.");
        return result;
    }

    QProcess process;
    process.start(exe, {QStringLiteral("list"), devicePath.trimmed()});
    if (!process.waitForFinished(10000)) {
        result.stdErr = QStringLiteral("diskutil list timed out.");
        return result;
    }

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.stdErr = QString::fromLocal8Bit(process.readAllStandardError());
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return result;
}

DrutilCommandResult runLiveCdrdaoReadToc(const QString& deviceSpec, const QString& driverSpec) {
    QTemporaryDir tempDir;
    const QString tocPath = tempDir.path() + QStringLiteral("/disc-read.toc");
    return runCdrdaoSimple({
        QStringLiteral("read-toc"),
        QStringLiteral("--device"), deviceSpec,
        QStringLiteral("--driver"), driverSpec,
        tocPath,
    });
}

DrutilCommandResult runLiveCdrdaoDiskInfo(const QString& deviceSpec, const QString& driverSpec) {
    return runCdrdaoSimple({
        QStringLiteral("disk-info"),
        QStringLiteral("--device"), deviceSpec,
        QStringLiteral("--driver"), driverSpec,
    });
}

void finalizeResult(
    DiscAnalysisResult& result,
    const StatusSummary& status,
    const CdTextSummary& drutilCdText,
    const CdTextSummary& cdrdaoCdText,
    const CdrdaoDiskInfoSummary& cdrdaoDiskInfo
) {
    if (!result.mediaPresent) {
        result.suspiciousReasons.append(QStringLiteral("No media detected."));
    } else {
        if (!result.looksLikeAudioCd && result.tracks.isEmpty()) {
            result.suspiciousReasons.append(QStringLiteral("The inserted disc does not look like an audio CD."));
        }

        if (!result.tracks.isEmpty()) {
            int totalSeconds = 0;
            for (const auto& track : result.tracks) {
                totalSeconds += track.durationSeconds;
            }
            result.findings.append(
                QStringLiteral("Audio TOC reports %1 track(s), total runtime %2.")
                    .arg(result.tracks.size())
                    .arg(msfFromSeconds(totalSeconds))
            );
        }

        if (cdrdaoDiskInfo.parsed) {
            if (cdrdaoDiskInfo.appendable) {
                result.suspiciousReasons.append(
                    QStringLiteral("cdrdao reports the disc as appendable, which is suspicious for a finalized audio CD.")
                );
            }
            if (cdrdaoDiskInfo.sessions > 1) {
                result.findings.append(QStringLiteral("cdrdao reports %1 sessions.").arg(cdrdaoDiskInfo.sessions));
            }
        }

        if (drutilCdText.found) {
            result.findings.append(QStringLiteral("drutil high-level CD-TEXT is readable."));
        } else if (cdrdaoCdText.found) {
            result.suspiciousReasons.append(
                QStringLiteral("Lower-level CD-TEXT exists, but drutil could not read high-level CD-TEXT.")
            );
        }

        bool anyProbeFailure = false;
        bool anyAllZeroProbe = false;
        for (const auto& trackProbe : result.audioProbeResults) {
            int failedPoints = 0;
            int zeroPoints = 0;
            bool middleZero = false;
            bool endZero = false;
            for (const auto& point : trackProbe.points) {
                if (point.attempted && !point.ok) {
                    ++failedPoints;
                    anyProbeFailure = true;
                }
                if (point.ok && point.allZero) {
                    ++zeroPoints;
                    anyAllZeroProbe = true;
                    if (point.label.compare(QStringLiteral("middle"), Qt::CaseInsensitive) == 0) {
                        middleZero = true;
                    }
                    if (point.label.compare(QStringLiteral("end"), Qt::CaseInsensitive) == 0) {
                        endZero = true;
                    }
                }
            }

            if (failedPoints > 0) {
                result.suspiciousReasons.append(
                    QStringLiteral("Track %1 has %2 unreadable audio probe point(s).")
                        .arg(trackProbe.trackNumber)
                        .arg(failedPoints)
                );
            } else if (endZero || middleZero || zeroPoints >= 2) {
                result.suspiciousReasons.append(
                    endZero
                        ? QStringLiteral("Track %1 has an all-zero end probe window, which strongly suggests unwritten or truncated later sectors.")
                              .arg(trackProbe.trackNumber)
                        : QStringLiteral("Track %1 has all-zero audio probe windows, which may indicate unwritten or damaged sectors.")
                              .arg(trackProbe.trackNumber)
                );
            }
        }

        if (!anyProbeFailure && !result.audioProbeResults.isEmpty()) {
            result.findings.append(QStringLiteral("Raw audio probe succeeded on all sampled track positions."));
        }
        if (anyAllZeroProbe && !anyProbeFailure) {
            result.findings.append(QStringLiteral("Some sampled audio windows were all-zero; inspect per-track probe details."));
        }

        if (!result.audioProbeResults.isEmpty() && !result.drutilCdTextAvailable && cdrdaoCdText.found) {
            result.suspiciousReasons.append(
                QStringLiteral("CD-TEXT is only recoverable through lower-level probes while high-level queries fail, which is atypical for a cleanly readable finalized disc.")
            );
        }

        if (result.usageRatio > 0.98 && !result.audioProbeResults.isEmpty() && anyAllZeroProbe) {
            result.suspiciousReasons.append(
                QStringLiteral("The disc reports near-full occupancy but sampled audio data contains zero-filled windows, suggesting TOC completion without fully written program data.")
            );
        }
    }

    result.looksHealthy = result.suspiciousReasons.isEmpty();
    result.mediumType = cdrdaoDiskInfo.mediumType;
    const int usedFrames = parseMsfFrames(status.spaceUsed);
    const int freeFrames = parseMsfFrames(status.spaceFree);
    if (usedFrames >= 0 && freeFrames >= 0 && (usedFrames + freeFrames) > 0) {
        result.usageRatio = static_cast<double>(usedFrames) / static_cast<double>(usedFrames + freeFrames);
    } else if (!result.tracks.isEmpty()) {
        int totalSeconds = 0;
        for (const auto& track : result.tracks) {
            totalSeconds += track.durationSeconds;
        }
        result.usageRatio = qBound(0.0, static_cast<double>(totalSeconds) / (74.0 * 60.0), 1.0);
    }

    QStringList lines;
    lines << QStringLiteral("Disc analysis: %1")
                 .arg(result.liveMode ? QStringLiteral("live drive") : QStringLiteral("sample directory"))
          << QStringLiteral("Source: %1").arg(result.source);
    if (!result.driveId.isEmpty()) {
        lines << QStringLiteral("Drive ID: %1").arg(result.driveId);
    }
    if (!result.rawDevicePath.isEmpty()) {
        lines << QStringLiteral("Raw device: %1").arg(result.rawDevicePath);
    }
    lines << QStringLiteral("Media present: %1").arg(result.mediaPresent ? QStringLiteral("yes") : QStringLiteral("no"))
          << QStringLiteral("Looks like audio CD: %1").arg(result.looksLikeAudioCd ? QStringLiteral("yes") : QStringLiteral("no"))
          << QStringLiteral("Looks like blank writable disc: %1").arg(result.looksLikeBlankWritableDisc ? QStringLiteral("yes") : QStringLiteral("no"));

    if (!status.mediaType.isEmpty()) {
        lines << QStringLiteral("Status media type: %1").arg(status.mediaType);
    }
    if (status.sessions > 0 || status.tracks > 0) {
        lines << QStringLiteral("Status sessions/tracks: %1 / %2").arg(status.sessions).arg(status.tracks);
    }
    if (!status.spaceUsed.isEmpty() || !status.spaceFree.isEmpty()) {
        lines << QStringLiteral("Status used/free: %1 / %2")
                     .arg(status.spaceUsed.isEmpty() ? QStringLiteral("n/a") : status.spaceUsed)
                     .arg(status.spaceFree.isEmpty() ? QStringLiteral("n/a") : status.spaceFree);
    }

    if (!result.cdTextTitle.isEmpty() || !result.cdTextPerformer.isEmpty()) {
        lines << QStringLiteral("CD-TEXT title: %1")
                     .arg(result.cdTextTitle.isEmpty() ? QStringLiteral("(empty)") : result.cdTextTitle)
              << QStringLiteral("CD-TEXT performer: %1")
                     .arg(result.cdTextPerformer.isEmpty() ? QStringLiteral("(empty)") : result.cdTextPerformer);
        if (!result.cdTextSizeInfoValues.isEmpty()) {
            QStringList values;
            for (const int value : result.cdTextSizeInfoValues) {
                values.append(QString::number(value));
            }
            lines << QStringLiteral("CD-TEXT SIZE_INFO: %1").arg(values.join(QStringLiteral(", ")));
        }
    } else {
        lines << QStringLiteral("CD-TEXT: not readable through current probes");
    }

    lines << QStringLiteral("Tracks:");
    if (result.tracks.isEmpty()) {
        lines << QStringLiteral("  (none)");
    } else {
        for (const auto& track : result.tracks) {
            lines << QStringLiteral("  %1. %2  [%3]  artist=%4")
                         .arg(track.number, 2)
                         .arg(track.title.isEmpty() ? QStringLiteral("Track %1").arg(track.number) : track.title)
                         .arg(msfFromSeconds(track.durationSeconds))
                         .arg(track.artist.isEmpty() ? QStringLiteral("(empty)") : track.artist);
        }
    }

    if (!result.audioProbeResults.isEmpty()) {
        lines << QStringLiteral("Audio probe:");
        for (const auto& trackProbe : result.audioProbeResults) {
            QStringList pointTexts;
            for (const auto& point : trackProbe.points) {
                QString marker;
                if (!point.attempted) {
                    marker = QStringLiteral("skip");
                } else if (!point.ok) {
                    marker = QStringLiteral("fail");
                } else if (point.allZero) {
                    marker = QStringLiteral("zero");
                } else {
                    marker = QStringLiteral("ok");
                }
                pointTexts.append(QStringLiteral("%1=%2@%3").arg(point.label, marker).arg(point.lsn));
            }
            lines << QStringLiteral("  Track %1: %2")
                         .arg(trackProbe.trackNumber, 2)
                         .arg(pointTexts.join(QStringLiteral(", ")));
        }
    }

    lines << QStringLiteral("Findings:");
    if (result.findings.isEmpty()) {
        lines << QStringLiteral("  (none)");
    } else {
        for (const auto& finding : result.findings) {
            lines << QStringLiteral("  - %1").arg(finding);
        }
    }

    lines << QStringLiteral("Suspicious reasons:");
    if (result.suspiciousReasons.isEmpty()) {
        lines << QStringLiteral("  (none)");
    } else {
        for (const auto& reason : result.suspiciousReasons) {
            lines << QStringLiteral("  - %1").arg(reason);
        }
    }

    lines << QStringLiteral("Overall verdict: %1")
                 .arg(result.looksHealthy ? QStringLiteral("looks healthy")
                                          : QStringLiteral("suspicious / needs manual review"));

    if (!status.rawOutput.isEmpty()) {
        lines << QStringLiteral("\n[drutil status]\n%1").arg(status.rawOutput);
    }
    if (!status.rawError.isEmpty()) {
        lines << QStringLiteral("\n[drutil status stderr]\n%1").arg(status.rawError);
    }
    if (!drutilCdText.rawOutput.isEmpty()) {
        lines << QStringLiteral("\n[drutil cdtext]\n%1").arg(drutilCdText.rawOutput);
    }
    if (!drutilCdText.rawError.isEmpty()) {
        lines << QStringLiteral("\n[drutil cdtext stderr]\n%1").arg(drutilCdText.rawError);
    }
    if (!cdrdaoCdText.rawOutput.isEmpty()) {
        lines << QStringLiteral("\n[cdrdao cdtext]\n%1").arg(cdrdaoCdText.rawOutput);
    }

    result.textReport = lines.join(u'\n');

    QJsonArray tracksArray;
    for (const auto& track : result.tracks) {
        tracksArray.append(QJsonObject{
            {QStringLiteral("number"), track.number},
            {QStringLiteral("title"), track.title},
            {QStringLiteral("artist"), track.artist},
            {QStringLiteral("durationSeconds"), track.durationSeconds},
            {QStringLiteral("duration"), msfFromSeconds(track.durationSeconds)},
        });
    }

    QJsonArray cdrdaoTracksArray;
    for (const auto& track : result.cdrdaoTracks) {
        cdrdaoTracksArray.append(QJsonObject{
            {QStringLiteral("trackNumber"), track.trackNumber},
            {QStringLiteral("mode"), track.mode},
            {QStringLiteral("startMsf"), track.startMsf},
            {QStringLiteral("startLsn"), track.startLsn},
            {QStringLiteral("lengthMsf"), track.lengthMsf},
            {QStringLiteral("lengthSectors"), track.lengthSectors},
        });
    }

    QJsonArray probeArray;
    for (const auto& probe : result.audioProbeResults) {
        QJsonArray pointsArray;
        for (const auto& point : probe.points) {
            pointsArray.append(QJsonObject{
                {QStringLiteral("label"), point.label},
                {QStringLiteral("lsn"), point.lsn},
                {QStringLiteral("attempted"), point.attempted},
                {QStringLiteral("ok"), point.ok},
                {QStringLiteral("allZero"), point.allZero},
                {QStringLiteral("error"), point.error},
            });
        }
        probeArray.append(QJsonObject{
            {QStringLiteral("trackNumber"), probe.trackNumber},
            {QStringLiteral("points"), pointsArray},
        });
    }

    QJsonArray sizeInfoArray;
    for (const int value : result.cdTextSizeInfoValues) {
        sizeInfoArray.append(value);
    }

    result.jsonReport = QJsonDocument(QJsonObject{
        {QStringLiteral("liveMode"), result.liveMode},
        {QStringLiteral("source"), result.source},
        {QStringLiteral("driveId"), result.driveId},
        {QStringLiteral("rawDevicePath"), result.rawDevicePath},
        {QStringLiteral("mediaPresent"), result.mediaPresent},
        {QStringLiteral("looksLikeAudioCd"), result.looksLikeAudioCd},
        {QStringLiteral("looksLikeBlankWritableDisc"), result.looksLikeBlankWritableDisc},
        {QStringLiteral("drutilCdTextAvailable"), result.drutilCdTextAvailable},
        {QStringLiteral("cdrdaoAvailable"), result.cdrdaoAvailable},
        {QStringLiteral("looksHealthy"), result.looksHealthy},
        {QStringLiteral("status"), QJsonObject{
             {QStringLiteral("mediaType"), result.mediaType},
             {QStringLiteral("mediumType"), result.mediumType},
             {QStringLiteral("devicePath"), result.devicePath},
             {QStringLiteral("sessions"), result.sessions},
             {QStringLiteral("tracks"), result.tracksReported},
             {QStringLiteral("spaceUsed"), result.spaceUsed},
             {QStringLiteral("spaceFree"), result.spaceFree},
             {QStringLiteral("usageRatio"), result.usageRatio},
         }},
        {QStringLiteral("cdText"), QJsonObject{
             {QStringLiteral("title"), result.cdTextTitle},
             {QStringLiteral("performer"), result.cdTextPerformer},
             {QStringLiteral("sizeInfoValues"), sizeInfoArray},
         }},
        {QStringLiteral("tracks"), tracksArray},
        {QStringLiteral("cdrdaoTracks"), cdrdaoTracksArray},
        {QStringLiteral("audioProbe"), probeArray},
        {QStringLiteral("findings"), QJsonArray::fromStringList(result.findings)},
        {QStringLiteral("suspiciousReasons"), QJsonArray::fromStringList(result.suspiciousReasons)},
    });
}

DiscAnalysisResult analyzeSampleDirImpl(const QString& sampleDirPath) {
    DiscAnalysisResult result;
    result.liveMode = false;
    result.source = sampleDirPath;
    result.mediaPresent = true;
    result.looksLikeAudioCd = true;

    const DrutilOutputParser parser;
    const StatusSummary status = parseStatusSummary(
        readOptionalTextFile(sampleDirPath + QStringLiteral("/drutil-status.txt")),
        {}
    );
    result.rawDevicePath = rawDevicePathFor(status);
    result.mediaType = status.mediaType;
    result.devicePath = status.devicePath;
    result.sessions = status.sessions;
    result.tracksReported = status.tracks;
    result.spaceUsed = status.spaceUsed;
    result.spaceFree = status.spaceFree;

    const auto drutilCdText = parseCdTextSummary(
        readOptionalTextFile(sampleDirPath + QStringLiteral("/drutil-cdtext.plist"))
    );
    result.drutilCdTextAvailable = drutilCdText.found;
    result.tracks = parser.parseTracksFromToc(
        readOptionalTextFile(sampleDirPath + QStringLiteral("/drutil-toc.txt"))
    );

    const auto cdrdaoCdText = parseCdTextSummary(
        readOptionalTextFile(sampleDirPath + QStringLiteral("/cdrdao-cdtext.txt"))
    );
    const auto cdrdaoDiskInfo = parseCdrdaoDiskInfo(
        readOptionalTextFile(sampleDirPath + QStringLiteral("/cdrdao-disk-info.txt"))
    );
    result.cdrdaoTracks = parseCdrdaoReadToc(
        readOptionalTextFile(sampleDirPath + QStringLiteral("/cdrdao-read-toc.txt"))
    );
    result.cdrdaoAvailable = !result.cdrdaoTracks.isEmpty() || cdrdaoDiskInfo.parsed;

    const CdTextSummary effectiveCdText = cdrdaoCdText.found ? cdrdaoCdText : drutilCdText;
    result.cdTextTitle = effectiveCdText.title;
    result.cdTextPerformer = effectiveCdText.performer;
    result.cdTextSizeInfoValues = effectiveCdText.sizeInfoValues;

    finalizeResult(result, status, drutilCdText, cdrdaoCdText, cdrdaoDiskInfo);
    return result;
}

DiscAnalysisResult analyzeLiveDiscImpl(const QString& requestedDriveId) {
    DiscAnalysisResult result;
    result.liveMode = true;
    result.source = QStringLiteral("live-drive");

    const DrutilCommandRunner runner;
    const DrutilOutputParser parser;
    const auto driveList = parser.parseDriveList(runner.run({QStringLiteral("list")}).stdOut);
    result.driveId = chooseDriveId(driveList, requestedDriveId);
    result.source = result.driveId;

    const auto statusResult = runDrutilWithSelectorFallback(
        runner, result.driveId, {QStringLiteral("status")}, 2, 200
    );
    const StatusSummary status = parseStatusSummary(statusResult.stdOut, statusResult.stdErr);
    result.rawDevicePath = rawDevicePathFor(status);
    result.mediaPresent = parser.outputSuggestsMediaPresent(statusResult.stdOut);
    result.looksLikeAudioCd = parser.outputSuggestsAudioCd(statusResult.stdOut);
    result.looksLikeBlankWritableDisc = parser.outputSuggestsWritableBlankMedia(statusResult.stdOut);
    result.mediaType = status.mediaType;
    result.devicePath = status.devicePath;
    result.sessions = status.sessions;
    result.tracksReported = status.tracks;
    result.spaceUsed = status.spaceUsed;
    result.spaceFree = status.spaceFree;
    const auto diskutilListResult = runDiskutilListForDevice(status.devicePath);
    const auto diskutilSummary = parseDiskutilListSummary(diskutilListResult.stdOut);
    if (diskutilSummary.hasFreeSpaceEntry && status.spaceFree == QStringLiteral("00:00:00")) {
        result.suspiciousReasons.append(
            QStringLiteral("diskutil still reports trailing free space (%1) while drutil reports zero remaining space; this strongly suggests the TOC was written but program data was not fully recorded.")
                .arg(diskutilSummary.freeSpaceText)
        );
    }
    if (diskutilSummary.hasFreeSpaceEntry) {
        result.findings.append(
            QStringLiteral("diskutil lists trailing free space on the audio disc: %1.").arg(diskutilSummary.freeSpaceText)
        );
    }

    const auto tocResult = runDrutilWithSelectorFallback(
        runner, result.driveId, {QStringLiteral("toc")}, 2, 200
    );
    result.tracks = parser.parseTracksFromToc(tocResult.stdOut);
    if (!result.tracks.isEmpty()) {
        result.looksLikeAudioCd = true;
    }

    const auto cdTextResult = runDrutilWithSelectorFallback(
        runner, result.driveId, {QStringLiteral("cdtext")}, 4, 350
    );
    const auto drutilCdText = parseCdTextSummary(cdTextResult.stdOut, cdTextResult.stdErr);
    result.drutilCdTextAvailable = drutilCdText.found;

    CdTextSummary cdrdaoCdText;
    CdrdaoDiskInfoSummary cdrdaoDiskInfo;
    if (CdrdaoBurner::isAvailable()) {
        result.cdrdaoAvailable = true;
        const QString deviceSpec = CdrdaoBurner::deviceSpecFor(
            result.driveId,
            result.rawDevicePath.isEmpty()
                ? cdmanager::infrastructure::audio::AudioCdReader::defaultDevicePath()
                : result.rawDevicePath
        );
        const QString driverSpec = CdrdaoBurner::currentDriverSpec();

        const auto diskInfoResult = runLiveCdrdaoDiskInfo(deviceSpec, driverSpec);
        cdrdaoDiskInfo = parseCdrdaoDiskInfo(
            diskInfoResult.stdOut + QStringLiteral("\n") + diskInfoResult.stdErr
        );

        const auto readTocResult = runLiveCdrdaoReadToc(deviceSpec, driverSpec);
        const QString cdrdaoReadText =
            readTocResult.stdOut + QStringLiteral("\n") + readTocResult.stdErr;
        result.cdrdaoTracks = parseCdrdaoReadToc(cdrdaoReadText);
        cdrdaoCdText = parseCdTextSummary(cdrdaoReadText);
        result.audioProbeResults = probeAudioTracks(result.rawDevicePath, result.cdrdaoTracks);
    }

    const CdTextSummary effectiveCdText = cdrdaoCdText.found ? cdrdaoCdText : drutilCdText;
    result.cdTextTitle = effectiveCdText.title;
    result.cdTextPerformer = effectiveCdText.performer;
    result.cdTextSizeInfoValues = effectiveCdText.sizeInfoValues;

    finalizeResult(result, status, drutilCdText, cdrdaoCdText, cdrdaoDiskInfo);
    return result;
}

}  // namespace

DiscAnalysisResult DiscAnalysisService::analyzeLiveDisc(const QString& requestedDriveId) const {
    return analyzeLiveDiscImpl(requestedDriveId);
}

DiscAnalysisResult DiscAnalysisService::analyzeSampleDir(const QString& sampleDirPath) const {
    return analyzeSampleDirImpl(sampleDirPath);
}

}  // namespace cdmanager::application::import
