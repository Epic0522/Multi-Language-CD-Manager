#pragma once

#include <functional>
#include <memory>

#include <QString>
#include <QStringList>
#include <QVector>

#include "cdmanager/application/burn/CdTextPackTypes.h"

namespace cdmanager::infrastructure::burn {

struct BurnProgress {
    int trackIndex = 0;
    int trackCount = 0;
    float overallPercent = 0.f;
    QString phase;  // "Preparing", "Burning", "Finishing", etc.
};

struct BurnResult {
    bool ok = false;
    QString error;
    QString diagnostics;
};

struct DiscRecordingCdTextAnalysis {
    bool ok = false;
    QString error;
    QString diagnostics;
    int blockCount = 0;
};

// macOS DiscRecording-based CD burner supporting audio tracks with
// Japanese CD-TEXT (Shift-JIS).  Uses DRCDTextBlockCreateArrayFromPackList
// to consume our 18-byte MMC CD-TEXT packs directly.
//
// On macOS ≤10.14, DiscRecording is deprecated but still functional.
// On macOS ≥10.15, it may emit a deprecation warning at build time.
class DiscRecordingBurner {
public:
    DiscRecordingBurner();
    ~DiscRecordingBurner();

    DiscRecordingBurner(const DiscRecordingBurner&) = delete;
    DiscRecordingBurner& operator=(const DiscRecordingBurner&) = delete;

    void setSimulationMode(bool on);
    void setBurnSpeed(int speedX);  // 0 = max

    using ProgressCb = std::function<void(const BurnProgress&)>;
    void setProgressCallback(ProgressCb cb);

    static DiscRecordingCdTextAnalysis analyzeCdTextPacks(
        const QVector<cdmanager::application::burn::CdTextPack>& cdTextPacks);

    // Burn audio tracks to CD. audioFiles must be 44.1 kHz / 16-bit / stereo PCM
    // in a DiscRecording-readable container such as AIFF or WAV.
    // cdTextPacks are the 18-byte MMC packs from CdTextPackAssembler.
    // devicePath should be something like /dev/rdisk8.
    BurnResult burn(const QString& devicePath,
                    const QVector<cdmanager::application::burn::CdTextPack>& cdTextPacks,
                    const QStringList& audioFiles);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace cdmanager::infrastructure::burn
