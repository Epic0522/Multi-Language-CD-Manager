#pragma once

#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>

namespace cdmanager::infrastructure::audio {

struct AudioBurnPreparationResult {
    bool ok {false};
    QString error;
    QString diagnostics;
    QStringList preparedAudioFiles;
    QVector<int> preparedDurationsSeconds;
};

// 将用户拖进来的常见音频源统一转成 Audio CD 可直接刻录的
// 44.1kHz / 16-bit / stereo PCM WAV，降低上手门槛。
class AudioBurnSourcePreparer {
public:
    enum class OutputContainer {
        Wave,
        Aiff
    };

    using ProgressCallback = std::function<void(int currentIndex,
                                                int totalCount,
                                                const QString& sourceFile)>;

    AudioBurnPreparationResult prepare(const QStringList& sourceFiles,
                                       const QString& outputDirectoryPath,
                                       OutputContainer outputContainer = OutputContainer::Aiff,
                                       ProgressCallback progressCallback = {}) const;

    static bool isSupportedSourceFile(const QString& filePath);
    static QString supportedSourceSummary();
};

}  // namespace cdmanager::infrastructure::audio
