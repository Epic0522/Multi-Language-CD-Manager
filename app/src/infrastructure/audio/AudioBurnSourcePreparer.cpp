#include "cdmanager/infrastructure/audio/AudioBurnSourcePreparer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <QtEndian>

namespace cdmanager::infrastructure::audio {

namespace {

struct WavFormatInfo {
    bool ok {false};
    QString fileType;
    QString dataFormatSummary;
    int channels {0};
    int sampleRate {0};
    int bitsPerSample {0};
    int durationSeconds {0};
    qint64 dataSize {0};
};

quint32 readBig32(const char* p) {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(p));
}

quint32 readLittle32(const char* p) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(p));
}

void writeBig32(char* p, quint32 value) {
    qToBigEndian<quint32>(value, reinterpret_cast<uchar*>(p));
}

void writeLittle32(char* p, quint32 value) {
    qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(p));
}

bool padAiffToSectorAlignment(const QString& filePath, qint64& paddedBytes, QString& detail) {
    paddedBytes = 0;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadWrite)) {
        detail = QStringLiteral("Could not open AIFF for padding.");
        return false;
    }

    QByteArray data = file.readAll();
    if (data.size() < 12) {
        detail = QStringLiteral("AIFF too small.");
        return false;
    }

    char* bytes = data.data();
    if (memcmp(bytes, "FORM", 4) != 0 ||
        (memcmp(bytes + 8, "AIFF", 4) != 0 && memcmp(bytes + 8, "AIFC", 4) != 0)) {
        detail = QStringLiteral("Not an AIFF/AIFC file.");
        return false;
    }

    int offset = 12;
    int ssndChunkOffset = -1;
    quint32 ssndChunkSize = 0;
    while (offset + 8 <= data.size()) {
        const quint32 chunkSize = readBig32(bytes + offset + 4);
        if (memcmp(bytes + offset, "SSND", 4) == 0) {
            ssndChunkOffset = offset;
            ssndChunkSize = chunkSize;
            break;
        }
        int advance = 8 + static_cast<int>(chunkSize);
        if (chunkSize & 1U) {
            ++advance;
        }
        if (advance <= 0) {
            break;
        }
        offset += advance;
    }

    if (ssndChunkOffset < 0 || ssndChunkSize < 8) {
        detail = QStringLiteral("AIFF SSND chunk not found.");
        return false;
    }

    const int soundDataStart = ssndChunkOffset + 16;
    if (soundDataStart > data.size()) {
        detail = QStringLiteral("AIFF SSND payload truncated.");
        return false;
    }

    const quint32 declaredAudioBytes = ssndChunkSize - 8;
    const qint64 actualAudioBytes = data.size() - soundDataStart;
    const qint64 audioBytes = std::min<qint64>(declaredAudioBytes, actualAudioBytes);
    const qint64 remainder = audioBytes % 2352;
    if (remainder == 0) {
        detail = QStringLiteral("already aligned");
        return true;
    }

    paddedBytes = 2352 - remainder;
    data.append(QByteArray(static_cast<int>(paddedBytes), '\0'));
    bytes = data.data();

    writeBig32(bytes + ssndChunkOffset + 4, static_cast<quint32>(ssndChunkSize + paddedBytes));
    writeBig32(bytes + 4, static_cast<quint32>(data.size() - 8));

    file.resize(0);
    file.seek(0);
    if (file.write(data) != data.size()) {
        detail = QStringLiteral("Failed to rewrite padded AIFF.");
        return false;
    }
    detail = QStringLiteral("padded %1 byte(s) to 2352-byte boundary").arg(paddedBytes);
    return true;
}

bool padWaveToSectorAlignment(const QString& filePath, qint64& paddedBytes, QString& detail) {
    paddedBytes = 0;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadWrite)) {
        detail = QStringLiteral("Could not open WAV for padding.");
        return false;
    }

    QByteArray data = file.readAll();
    if (data.size() < 12) {
        detail = QStringLiteral("WAV too small.");
        return false;
    }

    char* bytes = data.data();
    if (memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0) {
        detail = QStringLiteral("Not a WAVE file.");
        return false;
    }

    int offset = 12;
    int dataChunkOffset = -1;
    quint32 dataChunkSize = 0;
    while (offset + 8 <= data.size()) {
        const quint32 chunkSize = readLittle32(bytes + offset + 4);
        if (memcmp(bytes + offset, "data", 4) == 0) {
            dataChunkOffset = offset;
            dataChunkSize = chunkSize;
            break;
        }
        int advance = 8 + static_cast<int>(chunkSize);
        if (chunkSize & 1U) {
            ++advance;
        }
        if (advance <= 0) {
            break;
        }
        offset += advance;
    }

    if (dataChunkOffset < 0) {
        detail = QStringLiteral("WAV data chunk not found.");
        return false;
    }

    const int soundDataStart = dataChunkOffset + 8;
    const qint64 actualAudioBytes = data.size() - soundDataStart;
    const qint64 audioBytes = std::min<qint64>(dataChunkSize, actualAudioBytes);
    const qint64 remainder = audioBytes % 2352;
    if (remainder == 0) {
        detail = QStringLiteral("already aligned");
        return true;
    }

    paddedBytes = 2352 - remainder;
    data.append(QByteArray(static_cast<int>(paddedBytes), '\0'));
    bytes = data.data();

    writeLittle32(bytes + dataChunkOffset + 4, static_cast<quint32>(dataChunkSize + paddedBytes));
    writeLittle32(bytes + 4, static_cast<quint32>(data.size() - 8));

    file.resize(0);
    file.seek(0);
    if (file.write(data) != data.size()) {
        detail = QStringLiteral("Failed to rewrite padded WAV.");
        return false;
    }
    detail = QStringLiteral("padded %1 byte(s) to 2352-byte boundary").arg(paddedBytes);
    return true;
}

bool padPreparedAudioToSectorAlignment(const QString& filePath, QString& detail) {
    qint64 paddedBytes = 0;
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("aiff") || suffix == QStringLiteral("aif") || suffix == QStringLiteral("aifc")) {
        return padAiffToSectorAlignment(filePath, paddedBytes, detail);
    }
    if (suffix == QStringLiteral("wav") || suffix == QStringLiteral("wave")) {
        return padWaveToSectorAlignment(filePath, paddedBytes, detail);
    }
    detail = QStringLiteral("unsupported container for sector padding");
    return false;
}

WavFormatInfo probeWavFormat(const QString& filePath) {
    WavFormatInfo info;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return info;

    if (f.size() < 44) return info;
    f.seek(40);
    QByteArray d = f.read(4);
    if (d.size() < 4) return info;
    const uint32_t dataSize = *reinterpret_cast<const uint32_t*>(d.constData());
    info.dataSize = dataSize;

    f.seek(22);
    d = f.read(2);
    if (d.size() < 2) return info;
    const uint16_t ch = *reinterpret_cast<const uint16_t*>(d.constData());
    f.seek(24);
    d = f.read(4);
    if (d.size() < 4) return info;
    const uint32_t sr = *reinterpret_cast<const uint32_t*>(d.constData());
    f.seek(34);
    d = f.read(2);
    if (d.size() < 2) return info;
    const uint16_t bps = *reinterpret_cast<const uint16_t*>(d.constData());

    const int bytesPerSec = static_cast<int>(sr) * ch * (bps / 8);
    if (bytesPerSec <= 0) return info;

    info.ok = true;
    info.channels = ch;
    info.sampleRate = static_cast<int>(sr);
    info.bitsPerSample = bps;
    info.durationSeconds = static_cast<int>(dataSize) / bytesPerSec;
    return info;
}

QStringList supportedExtensions() {
    return {
        QStringLiteral("wav"),
        QStringLiteral("wave"),
        QStringLiteral("aif"),
        QStringLiteral("aiff"),
        QStringLiteral("m4a"),
        QStringLiteral("mp3"),
        QStringLiteral("flac"),
        QStringLiteral("aac"),
        QStringLiteral("ogg"),
        QStringLiteral("opus"),
        QStringLiteral("caf")
    };
}

QStringList ffmpegArgs(const QString& inputPath,
                       const QString& outputPath,
                       AudioBurnSourcePreparer::OutputContainer outputContainer) {
    return {
        QStringLiteral("-y"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-i"),
        inputPath,
        QStringLiteral("-vn"),
        QStringLiteral("-ar"),
        QStringLiteral("44100"),
        QStringLiteral("-ac"),
        QStringLiteral("2"),
        QStringLiteral("-c:a"),
        outputContainer == AudioBurnSourcePreparer::OutputContainer::Aiff
            ? QStringLiteral("pcm_s16be")
            : QStringLiteral("pcm_s16le"),
        QStringLiteral("-f"),
        outputContainer == AudioBurnSourcePreparer::OutputContainer::Aiff
            ? QStringLiteral("aiff")
            : QStringLiteral("wav"),
        outputPath
    };
}

QStringList afconvertArgs(const QString& inputPath,
                         const QString& outputPath,
                         AudioBurnSourcePreparer::OutputContainer outputContainer) {
    return {
        inputPath,
        QStringLiteral("-o"),
        outputPath,
        QStringLiteral("-f"),
        outputContainer == AudioBurnSourcePreparer::OutputContainer::Aiff
            ? QStringLiteral("AIFF")
            : QStringLiteral("WAVE"),
        QStringLiteral("-d"),
        outputContainer == AudioBurnSourcePreparer::OutputContainer::Aiff
            ? QStringLiteral("BEI16@44100")
            : QStringLiteral("LEI16@44100"),
        QStringLiteral("-c"),
        QStringLiteral("2")
    };
}

bool runCommand(const QString& program,
                const QStringList& args,
                QString& stdOut,
                QString& stdErr,
                int timeoutMs = -1) {
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(5000)) {
        stdErr = QStringLiteral("Failed to start %1.").arg(program);
        return false;
    }

    process.closeWriteChannel();
    process.waitForFinished(timeoutMs);
    stdOut = QString::fromUtf8(process.readAllStandardOutput());
    stdErr = QString::fromUtf8(process.readAllStandardError());
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

WavFormatInfo probeAudioFormatWithAfinfo(const QString& filePath, const QString& afinfoPath) {
    WavFormatInfo info;
    QString stdOut;
    QString stdErr;
    if (!runCommand(afinfoPath, {filePath}, stdOut, stdErr)) {
        return info;
    }

    const QRegularExpression fileTypeRe(QStringLiteral(R"(File type ID:\s+([^\r\n]+))"));
    const QRegularExpression dataFormatRe(QStringLiteral(R"(Data format:\s+(\d+)\s+ch,\s+([0-9.]+)\s+Hz,\s+([^\r\n]+))"));
    const QRegularExpression bitsRe(QStringLiteral(R"((\d+)-bit)"));
    const QRegularExpression durationRe(QStringLiteral(R"(estimated duration:\s+([0-9.]+)\s+sec)"));
    const QRegularExpression bytesRe(QStringLiteral(R"(audio bytes:\s+(\d+))"));

    const auto fileTypeMatch = fileTypeRe.match(stdOut);
    if (fileTypeMatch.hasMatch()) {
        info.fileType = fileTypeMatch.captured(1).trimmed();
    }

    const auto dataFormatMatch = dataFormatRe.match(stdOut);
    if (dataFormatMatch.hasMatch()) {
        info.channels = dataFormatMatch.captured(1).toInt();
        info.sampleRate = qRound(dataFormatMatch.captured(2).toDouble());
        info.dataFormatSummary = dataFormatMatch.captured(3).trimmed();
        const auto bitsMatch = bitsRe.match(info.dataFormatSummary);
        if (bitsMatch.hasMatch()) {
            info.bitsPerSample = bitsMatch.captured(1).toInt();
        }
    }

    const auto durationMatch = durationRe.match(stdOut);
    if (durationMatch.hasMatch()) {
        info.durationSeconds = qRound(durationMatch.captured(1).toDouble());
    }

    const auto bytesMatch = bytesRe.match(stdOut);
    if (bytesMatch.hasMatch()) {
        info.dataSize = bytesMatch.captured(1).toLongLong();
    }

    info.ok = info.channels > 0 && info.sampleRate > 0 && info.bitsPerSample > 0;
    return info;
}

}  // namespace

AudioBurnPreparationResult AudioBurnSourcePreparer::prepare(const QStringList& sourceFiles,
                                                            const QString& outputDirectoryPath,
                                                            OutputContainer outputContainer,
                                                            ProgressCallback progressCallback) const {
    AudioBurnPreparationResult result;
    if (sourceFiles.isEmpty()) {
        result.error = QStringLiteral("No audio source files provided.");
        return result;
    }

    const QString ffmpegPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
#ifdef Q_OS_MACOS
    const QString afconvertPath = QStandardPaths::findExecutable(QStringLiteral("afconvert"));
    const QString afinfoPath = QStandardPaths::findExecutable(QStringLiteral("afinfo"));
#else
    const QString afconvertPath;
    const QString afinfoPath;
#endif

    enum class Backend {
        Ffmpeg,
        Afconvert
    };

    std::optional<Backend> backend;
    QString backendName;
    if (!afconvertPath.isEmpty()) {
        backend = Backend::Afconvert;
        backendName = QStringLiteral("afconvert");
    } else if (!ffmpegPath.isEmpty()) {
        backend = Backend::Ffmpeg;
        backendName = QStringLiteral("ffmpeg");
    }

    if (!backend.has_value()) {
        result.error = QStringLiteral("No audio transcoder available. Install ffmpeg or provide afconvert.");
        return result;
    }

    QDir outputDir(outputDirectoryPath);
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("Could not create burn temp directory.");
        return result;
    }

    result.diagnostics += QStringLiteral("Audio preparation backend: %1\n").arg(backendName);
    result.diagnostics += QStringLiteral("Audio output container: %1\n")
        .arg(outputContainer == OutputContainer::Aiff ? QStringLiteral("AIFF")
                                                      : QStringLiteral("WAVE"));

    for (int i = 0; i < sourceFiles.size(); ++i) {
        const QString sourceFile = sourceFiles[i];
        if (progressCallback) {
            progressCallback(i + 1, sourceFiles.size(), sourceFile);
        }
        const QFileInfo sourceInfo(sourceFile);
        if (!sourceInfo.exists() || !sourceInfo.isFile()) {
            result.error = QStringLiteral("Audio source not found: %1").arg(sourceFile);
            return result;
        }

        const QString outputFile = outputDir.filePath(
            QStringLiteral("prepared-track-%1.%2")
                .arg(i + 1, 2, 10, QLatin1Char('0'))
                .arg(outputContainer == OutputContainer::Aiff
                         ? QStringLiteral("aiff")
                         : QStringLiteral("wav"))
        );

        QString stdOut;
        QString stdErr;
        bool ok = false;
        switch (*backend) {
            case Backend::Ffmpeg:
                ok = runCommand(ffmpegPath, ffmpegArgs(sourceFile, outputFile, outputContainer), stdOut, stdErr);
                break;
            case Backend::Afconvert:
                ok = runCommand(afconvertPath, afconvertArgs(sourceFile, outputFile, outputContainer), stdOut, stdErr);
                break;
        }

        result.diagnostics += QStringLiteral("\n[%1] %2 -> %3\n")
            .arg(backendName)
            .arg(sourceFile)
            .arg(outputFile);
        if (!stdOut.trimmed().isEmpty()) {
            result.diagnostics += QStringLiteral("stdout:\n%1\n").arg(stdOut.trimmed());
        }
        if (!stdErr.trimmed().isEmpty()) {
            result.diagnostics += QStringLiteral("stderr:\n%1\n").arg(stdErr.trimmed());
        }

        if (!ok) {
            result.error = QStringLiteral("Could not convert audio source: %1").arg(sourceInfo.fileName());
            return result;
        }

        QString alignmentDetail;
        if (!padPreparedAudioToSectorAlignment(outputFile, alignmentDetail)) {
            result.error = QStringLiteral("Could not sector-align prepared audio file: %1").arg(QFileInfo(outputFile).fileName());
            result.diagnostics += QStringLiteral("sector alignment: %1\n").arg(alignmentDetail);
            return result;
        }
        result.diagnostics += QStringLiteral("sector alignment: %1\n").arg(alignmentDetail);

        WavFormatInfo audioInfo;
        if (!afinfoPath.isEmpty()) {
            audioInfo = probeAudioFormatWithAfinfo(outputFile, afinfoPath);
        }
        if (!audioInfo.ok && outputFile.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
            audioInfo = probeWavFormat(outputFile);
            if (audioInfo.fileType.isEmpty()) {
                audioInfo.fileType = QStringLiteral("WAVE");
            }
        }

        result.diagnostics += QStringLiteral(
            "prepared audio: type=%1 sr=%2 ch=%3 bits=%4 dataBytes=%5 duration=%6s format=%7\n")
            .arg(audioInfo.fileType.isEmpty() ? QStringLiteral("(unknown)") : audioInfo.fileType)
            .arg(audioInfo.sampleRate)
            .arg(audioInfo.channels)
            .arg(audioInfo.bitsPerSample)
            .arg(audioInfo.dataSize)
            .arg(audioInfo.durationSeconds)
            .arg(audioInfo.dataFormatSummary.isEmpty() ? QStringLiteral("(unknown)") : audioInfo.dataFormatSummary);
        if (!audioInfo.ok || audioInfo.sampleRate != 44100 || audioInfo.channels != 2 || audioInfo.bitsPerSample != 16) {
            result.error = QStringLiteral("Prepared audio file is not audio-CD compatible: %1").arg(QFileInfo(outputFile).fileName());
            result.diagnostics += QStringLiteral("Expected 44.1kHz / 16-bit / 2ch PCM audio.\n");
            return result;
        }

        result.preparedAudioFiles.append(outputFile);
        result.preparedDurationsSeconds.append(audioInfo.durationSeconds);
    }

    result.ok = true;
    return result;
}

bool AudioBurnSourcePreparer::isSupportedSourceFile(const QString& filePath) {
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return supportedExtensions().contains(suffix);
}

QString AudioBurnSourcePreparer::supportedSourceSummary() {
    return QStringLiteral("wav, aiff, m4a, mp3, flac, aac, ogg, opus, caf");
}

}  // namespace cdmanager::infrastructure::audio
