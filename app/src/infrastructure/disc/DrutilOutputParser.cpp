#include "cdmanager/infrastructure/disc/DrutilOutputParser.h"

#include <QRegularExpression>
#include <QStringList>

namespace cdmanager::infrastructure::disc {

namespace {

QString normalizedWhitespace(const QString& line) {
    return line.simplified();
}

QString valueAfterColon(const QString& line) {
    const int colonIndex = line.indexOf(u':');
    if (colonIndex < 0) {
        return QString();
    }
    return line.mid(colonIndex + 1).trimmed();
}

QString extractPlistStringValue(const QString& dictText, const QString& keyName) {
    const QString escapedKey = QRegularExpression::escape(keyName);
    const QRegularExpression pattern(
        QStringLiteral(R"(<key>%1</key>\s*<string>(.*?)</string>)").arg(escapedKey),
        QRegularExpression::DotMatchesEverythingOption
    );
    const auto match = pattern.match(dictText);
    if (!match.hasMatch()) {
        return QString();
    }
    return match.captured(1).trimmed();
}

int extractPlistIntegerValue(const QString& dictText, const QString& keyName, int defaultValue = 0) {
    const QString escapedKey = QRegularExpression::escape(keyName);
    const QRegularExpression pattern(
        QStringLiteral(R"(<key>%1</key>\s*<integer>(-?\d+)</integer>)").arg(escapedKey),
        QRegularExpression::DotMatchesEverythingOption
    );
    const auto match = pattern.match(dictText);
    if (!match.hasMatch()) {
        return defaultValue;
    }
    bool ok = false;
    const int value = match.captured(1).toInt(&ok);
    return ok ? value : defaultValue;
}

struct ParsedCdTextBlock {
    QString language;
    int characterCode {0};
    QString albumTitle;
    QString albumArtist;
    QVector<QPair<QString, QString>> tracks;
};

int nonBlankScore(const QString& text) {
    return text.trimmed().isEmpty() ? 0 : 1;
}

int blockScore(const ParsedCdTextBlock& block) {
    int score = 0;
    score += nonBlankScore(block.albumTitle) * 100;
    score += nonBlankScore(block.albumArtist) * 50;
    if (block.language == QStringLiteral("ja")) {
        score += 25;
    }
    for (const auto& track : block.tracks) {
        score += nonBlankScore(track.first) * 10;
        score += nonBlankScore(track.second) * 3;
    }
    return score;
}

}  // namespace

QVector<cdmanager::domain::disc::DriveInfo> DrutilOutputParser::parseDriveList(const QString& output) const {
    QVector<cdmanager::domain::disc::DriveInfo> drives;
    const QStringList lines = output.split(u'\n', Qt::SkipEmptyParts);
    int driveIndex = 0;

    for (const QString& rawLine : lines) {
        const QString line = normalizedWhitespace(rawLine);
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QStringLiteral("Vendor")) || line.startsWith(QStringLiteral("-"))) {
            continue;
        }

        const QStringList parts = line.split(u' ', Qt::SkipEmptyParts);
        if (parts.size() < 5) {
            continue;
        }

        ++driveIndex;
        cdmanager::domain::disc::DriveInfo info;
        info.deviceId = QStringLiteral("drutil-index://%1").arg(driveIndex);
        info.displayName = parts.mid(0, 3).join(u' ');
        info.canRead = true;
        info.canWrite = true;
        info.hasMediaLoaded = false;
        drives.append(info);
    }

    return drives;
}

bool DrutilOutputParser::outputSuggestsMediaPresent(const QString& statusOutput) const {
    const QString normalized = statusOutput.toLower();
    if (normalized.contains(QStringLiteral("no media"))) {
        return false;
    }
    return normalized.contains(QStringLiteral("type:")) || normalized.contains(QStringLiteral("sessions:"));
}

bool DrutilOutputParser::outputSuggestsAudioCd(const QString& statusOutput) const {
    const QString normalized = statusOutput.toLower();
    return normalized.contains(QStringLiteral("audio")) || normalized.contains(QStringLiteral("cd-da"));
}

bool DrutilOutputParser::outputSuggestsWritableBlankMedia(const QString& statusOutput) const {
    const QString normalized = statusOutput.toLower();
    if (normalized.contains(QStringLiteral("type: cd-rw")) ||
        normalized.contains(QStringLiteral("type: dvd-rw")) ||
        normalized.contains(QStringLiteral("type: bd-re"))) {
        return true;
    }

    const QRegularExpression usedRe(
        QStringLiteral(R"(space used:\s+([0-9:]+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto usedMatch = usedRe.match(statusOutput);
    if (usedMatch.hasMatch() && usedMatch.captured(1) != QStringLiteral("00:00:00")) {
        return false;
    }

    const QRegularExpression tracksRe(
        QStringLiteral(R"(tracks:\s+(\d+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto tracksMatch = tracksRe.match(statusOutput);
    if (tracksMatch.hasMatch() && tracksMatch.captured(1).toInt() > 0) {
        return false;
    }

    const QRegularExpression freeRe(
        QStringLiteral(R"(space free:\s+([0-9:]+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto freeMatch = freeRe.match(statusOutput);
    if (freeMatch.hasMatch()) {
        return freeMatch.captured(1) != QStringLiteral("00:00:00");
    }

    return false;
}

namespace {

// Convert an MSF string like "00:02.00" or "76:04.42" to total seconds.
int msfToSeconds(const QString& msf) {
    const QStringList parts = msf.split(u':');
    if (parts.size() != 2) return 0;
    const QStringList secondsParts = parts[1].split(u'.');
    if (secondsParts.size() != 2) return 0;
    bool ok = false;
    const int minutes = parts[0].toInt(&ok);
    if (!ok) return 0;
    const int seconds = secondsParts[0].toInt(&ok);
    if (!ok) return 0;
    // Ignore frames (75 frames/second) for duration display.
    return minutes * 60 + seconds;
}

}  // namespace

QVector<cdmanager::domain::project::Track> DrutilOutputParser::parseTracksFromToc(const QString& tocOutput) const {
    // Collect (trackNumber, msfString) pairs, plus the lead-out MSF.
    struct TocEntry {
        int trackNumber = 0;
        QString msf;
    };
    QVector<TocEntry> entries;
    QString leadOutMsf;

    const QRegularExpression trackLinePattern(
        QStringLiteral(R"(Session\s+\d+,\s+Track\s+(\d+):\s+(\d+:\d+\.\d+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpression leadOutPattern(
        QStringLiteral(R"(Lead-out:\s+(\d+:\d+\.\d+))"),
        QRegularExpression::CaseInsensitiveOption
    );

    const QStringList lines = tocOutput.split(u'\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const auto leadMatch = leadOutPattern.match(line);
        if (leadMatch.hasMatch()) {
            leadOutMsf = leadMatch.captured(1);
            continue;
        }

        const auto match = trackLinePattern.match(line);
        if (!match.hasMatch()) continue;

        bool ok = false;
        const int trackNumber = match.captured(1).toInt(&ok);
        if (!ok || trackNumber <= 0) continue;

        entries.append({trackNumber, match.captured(2)});
    }

    QVector<cdmanager::domain::project::Track> tracks;
    for (int i = 0; i < entries.size(); ++i) {
        const int startSec = msfToSeconds(entries[i].msf);
        int endSec = startSec;
        if (i + 1 < entries.size()) {
            endSec = msfToSeconds(entries[i + 1].msf);
        } else if (!leadOutMsf.isEmpty()) {
            endSec = msfToSeconds(leadOutMsf);
        }

        cdmanager::domain::project::Track track;
        track.number = entries[i].trackNumber;
        track.title = QStringLiteral("Track %1").arg(entries[i].trackNumber);
        track.artist = QStringLiteral("未焼錄");
        track.durationSeconds = (endSec > startSec) ? (endSec - startSec) : 0;
        tracks.append(track);
    }

    return tracks;
}

void DrutilOutputParser::applyDrutilCdTextPlist(
    const QString& cdTextOutput,
    cdmanager::domain::disc::DiscSnapshot& snapshot
) const {
    if (!cdTextOutput.contains(QStringLiteral("<plist"))) {
        return;
    }

    const QRegularExpression blockPattern(
        QStringLiteral(
            R"(<dict>\s*<key>Properties</key>\s*<dict>(.*?)</dict>\s*<key>Tracks</key>\s*<array>(.*?)</array>\s*</dict>)"),
        QRegularExpression::DotMatchesEverythingOption
    );
    const QRegularExpression dictPattern(
        QStringLiteral(R"(<dict>(.*?)</dict>)"),
        QRegularExpression::DotMatchesEverythingOption
    );

    QVector<ParsedCdTextBlock> blocks;
    auto blockIterator = blockPattern.globalMatch(cdTextOutput);
    while (blockIterator.hasNext()) {
        const auto blockMatch = blockIterator.next();
        ParsedCdTextBlock block;
        const QString propertiesText = blockMatch.captured(1);
        const QString tracksArrayText = blockMatch.captured(2);

        block.language = extractPlistStringValue(propertiesText, QStringLiteral("DRCDTextLanguageKey"));
        block.characterCode = extractPlistIntegerValue(propertiesText, QStringLiteral("DRCDTextCharacterCodeKey"));

        auto dictIterator = dictPattern.globalMatch(tracksArrayText);
        bool albumAssigned = false;
        while (dictIterator.hasNext()) {
            const auto match = dictIterator.next();
            const QString dictText = match.captured(1);

            const QString title = extractPlistStringValue(dictText, QStringLiteral("DRCDTextTitleKey"));
            const QString performer = extractPlistStringValue(dictText, QStringLiteral("DRCDTextPerformerKey"));

            if (!albumAssigned) {
                block.albumTitle = title;
                block.albumArtist = performer;
                albumAssigned = true;
            } else {
                block.tracks.append({title, performer});
            }
        }

        if (albumAssigned || !block.tracks.isEmpty()) {
            blocks.append(block);
        }
    }

    if (blocks.isEmpty()) {
        return;
    }

    int bestIndex = 0;
    int bestScore = blockScore(blocks.first());
    for (int i = 1; i < blocks.size(); ++i) {
        const int score = blockScore(blocks.at(i));
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    const ParsedCdTextBlock& chosen = blocks.at(bestIndex);
    if (!chosen.albumTitle.trimmed().isEmpty()) {
        snapshot.albumTitle = chosen.albumTitle;
    }
    if (!chosen.albumArtist.trimmed().isEmpty()) {
        snapshot.albumArtist = chosen.albumArtist;
    }

    for (int i = 0; i < chosen.tracks.size(); ++i) {
        const QString& title = chosen.tracks.at(i).first;
        const QString& performer = chosen.tracks.at(i).second;
        const int trackNumber = i + 1;

        if (snapshot.tracks.size() < trackNumber) {
            cdmanager::domain::project::Track track;
            track.number = trackNumber;
            track.title = title.trimmed().isEmpty() ? QStringLiteral("Track %1").arg(track.number) : title;
            track.artist = performer.trimmed().isEmpty() ? QStringLiteral("未焼錄") : performer;
            snapshot.tracks.append(track);
        } else {
            auto& track = snapshot.tracks[trackNumber - 1];
            track.number = trackNumber;
            if (!title.trimmed().isEmpty()) {
                track.title = title;
                track.titlePresent = true;
            }
            if (!performer.trimmed().isEmpty()) {
                track.artist = performer;
                track.artistPresent = true;
            }
        }
    }
}

void DrutilOutputParser::applyCdTextHeuristics(
    const QString& cdTextOutput,
    cdmanager::domain::disc::DiscSnapshot& snapshot
) const {
    // 先尝试解析 drutil 已经给出的 plist 结构；失败时再退回到宽松文本匹配。
    applyDrutilCdTextPlist(cdTextOutput, snapshot);
    if (!snapshot.tracks.isEmpty() || !snapshot.albumTitle.isEmpty() || !snapshot.albumArtist.isEmpty()) {
        return;
    }

    const QStringList lines = cdTextOutput.split(u'\n', Qt::SkipEmptyParts);
    cdmanager::domain::project::Track* currentTrack = nullptr;

    for (const QString& rawLine : lines) {
        const QString line = normalizedWhitespace(rawLine);
        const QString lower = line.toLower();

        if (lower.startsWith(QStringLiteral("title:")) || lower.startsWith(QStringLiteral("album title:"))) {
        if (currentTrack != nullptr) {
            currentTrack->title = valueAfterColon(line);
        } else if (snapshot.albumTitle.isEmpty()) {
            snapshot.albumTitle = valueAfterColon(line);
        }
            continue;
        }

        if (lower.startsWith(QStringLiteral("artist:")) || lower.startsWith(QStringLiteral("album artist:")) || lower.startsWith(QStringLiteral("performer:"))) {
            if (currentTrack != nullptr) {
                currentTrack->artist = valueAfterColon(line);
            } else if (snapshot.albumArtist.isEmpty()) {
                snapshot.albumArtist = valueAfterColon(line);
            }
            continue;
        }

        const QRegularExpression trackHeaderPattern(QStringLiteral(R"(track\s+(\d+))"), QRegularExpression::CaseInsensitiveOption);
        const auto trackMatch = trackHeaderPattern.match(line);
        if (trackMatch.hasMatch()) {
            bool ok = false;
            const int trackNumber = trackMatch.captured(1).toInt(&ok);
            if (ok) {
                currentTrack = nullptr;
                for (auto& track : snapshot.tracks) {
                    if (track.number == trackNumber) {
                        currentTrack = &track;
                        break;
                    }
                }
            }
            continue;
        }
    }

    // 如果只拿到了 CD-TEXT 但 TOC 还没解析出轨道，至少从文本里补一个最小轨道数量线索。
    if (snapshot.tracks.isEmpty()) {
        const QRegularExpression genericTrackPattern(QStringLiteral(R"(track\s+(\d+))"), QRegularExpression::CaseInsensitiveOption);
        int maxTrackNumber = 0;
        for (const QString& line : lines) {
            const auto match = genericTrackPattern.match(line);
            if (!match.hasMatch()) {
                continue;
            }
            bool ok = false;
            const int trackNumber = match.captured(1).toInt(&ok);
            if (ok) {
                maxTrackNumber = qMax(maxTrackNumber, trackNumber);
            }
        }

        for (int i = 1; i <= maxTrackNumber; ++i) {
            cdmanager::domain::project::Track track;
            track.number = i;
            track.title = QStringLiteral("Track %1").arg(i);
            track.artist = QStringLiteral("未焼錄");
            snapshot.tracks.append(track);
        }
    }
}

}  // namespace cdmanager::infrastructure::disc
