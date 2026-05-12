#pragma once

#include <QVector>

#include "cdmanager/domain/disc/DiscSnapshot.h"
#include "cdmanager/domain/disc/DriveInfo.h"

namespace cdmanager::infrastructure::disc {

// 当前阶段先做“保守解析”：
// - 能稳定识别驱动器列表
// - 能从 TOC 里估计音轨数
// - 保留原始输出给后续真实盘调试
class DrutilOutputParser {
public:
    QVector<cdmanager::domain::disc::DriveInfo> parseDriveList(const QString& output) const;
    bool outputSuggestsMediaPresent(const QString& statusOutput) const;
    bool outputSuggestsAudioCd(const QString& statusOutput) const;
    bool outputSuggestsWritableBlankMedia(const QString& statusOutput) const;
    QVector<cdmanager::domain::project::Track> parseTracksFromToc(const QString& tocOutput) const;
    void applyDrutilCdTextPlist(
        const QString& cdTextOutput,
        cdmanager::domain::disc::DiscSnapshot& snapshot
    ) const;
    void applyCdTextHeuristics(
        const QString& cdTextOutput,
        cdmanager::domain::disc::DiscSnapshot& snapshot
    ) const;
};

}  // namespace cdmanager::infrastructure::disc
