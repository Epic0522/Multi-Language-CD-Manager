#pragma once

#include <QString>
#include <QVector>

#include "cdmanager/domain/project/Track.h"

namespace cdmanager::domain::disc {

// 读取光盘后的中间表示。
// 这里先不直接暴露底层库结构，避免后面换 libcdio/libburn 时把上层一起拖乱。
struct DiscSnapshot {
    QString sourceName;
    QString albumTitle;
    QString albumArtist;
    QVector<cdmanager::domain::project::Track> tracks;
    bool hasMediaPresent {false};
    bool looksLikeAudioCd {false};
    bool looksLikeBlankWritableDisc {false};
    bool containsCdText {false};
    bool containsJapaneseCdText {false};
    QString rawStatusOutput;
    QString rawStatusError;
    QString rawTocOutput;
    QString rawTocError;
    QString rawCdTextOutput;
    QString rawCdTextError;
};

}  // namespace cdmanager::domain::disc
