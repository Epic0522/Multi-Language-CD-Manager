#pragma once

#include <QJsonDocument>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cdmanager/domain/project/Track.h"

namespace cdmanager::application::import {

enum class DiscAnalysisDepth {
    Safe,
    Deep,
};

struct DiscAnalysisProbePoint {
    QString label;
    int lsn {0};
    bool attempted {false};
    bool ok {false};
    bool allZero {false};
    bool inferred {false};
    QString error;
};

struct DiscAnalysisTrackProbe {
    int trackNumber {0};
    QVector<DiscAnalysisProbePoint> points;
};

struct DiscAnalysisTrackLayout {
    int trackNumber {0};
    QString mode;
    QString startMsf;
    int startLsn {0};
    QString lengthMsf;
    int lengthSectors {0};
};

struct DiscAnalysisRegionMarker {
    double startRatio {0.0};
    double endRatio {0.0};
    QString reason;
};

struct DiscAnalysisResult {
    bool liveMode {true};
    QString source;
    QString driveId;
    QString rawDevicePath;
    bool mediaPresent {false};
    bool looksLikeAudioCd {false};
    bool looksLikeBlankWritableDisc {false};
    bool drutilCdTextAvailable {false};
    bool cdrdaoAvailable {false};
    bool looksHealthy {false};
    bool performedDeepAnalysis {false};
    bool confirmedTrackBoundaryFailure {false};
    QString mediaType;
    QString mediumType;
    QString devicePath;
    int sessions {0};
    int tracksReported {0};
    QString spaceUsed;
    QString spaceFree;
    double usageRatio {0.0};
    QString cdTextTitle;
    QString cdTextPerformer;
    QVector<int> cdTextSizeInfoValues;
    QVector<cdmanager::domain::project::Track> tracks;
    QVector<DiscAnalysisTrackLayout> cdrdaoTracks;
    QVector<DiscAnalysisTrackProbe> audioProbeResults;
    QVector<DiscAnalysisRegionMarker> regionMarkers;
    int firstBadTrackNumber {0};
    QStringList findings;
    QStringList suspiciousReasons;
    QString textReport;
    QJsonDocument jsonReport;
};

class DiscAnalysisService {
public:
    DiscAnalysisResult analyzeLiveDisc(
        const QString& requestedDriveId = {},
        DiscAnalysisDepth depth = DiscAnalysisDepth::Deep
    ) const;
    DiscAnalysisResult analyzeSampleDir(const QString& sampleDirPath) const;
};

}  // namespace cdmanager::application::import
