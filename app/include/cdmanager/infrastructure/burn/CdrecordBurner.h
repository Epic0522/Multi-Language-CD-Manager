#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "cdmanager/application/burn/CdTextPackTypes.h"
#include "cdmanager/infrastructure/burn/DiscRecordingBurner.h"

namespace cdmanager::infrastructure::burn {

class CdrecordBurner {
public:
    using ProgressCb = DiscRecordingBurner::ProgressCb;

    static bool isAvailable();
    static QString executablePath();
    static QString deviceSpecFor(const QString& driveId, const QString& devicePath);
    static QByteArray buildCdTextBinary(const QVector<cdmanager::application::burn::CdTextPack>& packs);
    static bool writeCdTextFile(const QString& path,
                                const QVector<cdmanager::application::burn::CdTextPack>& packs,
                                QString* errorMessage = nullptr);
    static QString buildCueText(const QStringList& audioFiles, int trackGapSeconds);
    static bool writeCueFile(const QString& cuePath,
                             const QStringList& audioFiles,
                             int trackGapSeconds,
                             QString* errorMessage = nullptr);

    void setSimulationMode(bool on);
    void setBurnSpeed(int speedX);
    void setAllowOverburn(bool on);
    void setProgressCallback(ProgressCb cb);

    BurnResult burn(const QString& deviceSpec,
                    const QString& cueFilePath,
                    const QString& cdTextFilePath);

private:
    void handleOutputChunk(const QByteArray& chunk, bool stderrStream, QString* aggregate);
    void processOutputLine(const QString& line, bool stderrStream);

    bool m_simulationMode = true;
    int m_burnSpeed = 0;
    bool m_allowOverburn = false;
    ProgressCb m_progressCb;
};

}  // namespace cdmanager::infrastructure::burn
