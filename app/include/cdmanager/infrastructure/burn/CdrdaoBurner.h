#pragma once

#include <QString>

#include "cdmanager/infrastructure/burn/DiscRecordingBurner.h"

namespace cdmanager::infrastructure::burn {

// cdrdao 路线更贴近吴叶这类老派“DAO/RAW96 + CD-TEXT”工具的工作方式。
// 这里先把它收成一个独立后端，避免和 drutil/DiscRecording 的假设搅在一起。
class CdrdaoBurner {
public:
    using ProgressCb = DiscRecordingBurner::ProgressCb;

    static bool isAvailable();
    static QString currentDriverSpec();

    // cdrdao 在 macOS 上既支持 compatibility mode 的 x,y,z，也支持 IORegistry path。
    // 这里先做一个“drutil-index://N -> (N-1),0,0”的兼容映射，
    // 允许后面再用真实 scanbus 结果把它收得更精准。
    static QString deviceSpecFor(const QString& driveId, const QString& devicePath);

    void setSimulationMode(bool on);
    void setBurnSpeed(int speedX);  // 0 = max
    void setAllowOverburn(bool on);
    void setProgressCallback(ProgressCb cb);

    BurnResult burn(const QString& deviceSpec, const QString& tocFilePath);

private:
    void handleOutputChunk(const QByteArray& chunk, bool stderrStream, QString* aggregate);
    void processOutputLine(const QString& line, bool stderrStream);

    bool m_simulationMode = true;
    int m_burnSpeed = 0;
    bool m_allowOverburn = false;
    ProgressCb m_progressCb;
};

}  // namespace cdmanager::infrastructure::burn
