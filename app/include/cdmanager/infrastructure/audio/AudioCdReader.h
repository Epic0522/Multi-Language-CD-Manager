#pragma once

#include <cstdint>

#include <QObject>
#include <QString>

namespace cdmanager::infrastructure::audio {

class AudioRingBuffer;

// Logical Sector Number — stored as uint32_t so the header remains
// usable even when libcdio is not installed.
using lsn_t = std::uint32_t;

// CD audio frame raw size: 2352 bytes per sector (Red Book).
inline constexpr int kCdFramesizeRaw = 2352;

struct CdTrackLocation {
    lsn_t startLsn = 0;
    uint32_t sectorCount = 0;
    QString devicePath;
    bool valid = false;
};

// Reads CD audio sectors on a dedicated worker thread.
//
// On macOS, libcdio's IOKit driver cannot read audio sectors, so we
// read directly from the raw device node (/dev/rdisk*).  On platforms
// where libcdio audio works, we delegate to cdio_read_audio_sectors().
//
// Usage:
//   1. Create, setRingBuffer(), setDevicePath(), move to a QThread.
//   2. Connect the thread's started() signal to readLoop().
//   3. Call start() with track LSN/count, then start the thread.
//   4. setInterrupted() + wait on the thread to stop.
class AudioCdReader : public QObject {
    Q_OBJECT

public:
    static constexpr int kSectorsPerRead = 20;  // ~267 ms of audio
    static constexpr int kBytesPerSector = kCdFramesizeRaw;

    explicit AudioCdReader(QObject* parent = nullptr);
    ~AudioCdReader() override;

    // Query track position from the default CD device via libcdio.
    static CdTrackLocation locateTrack(int trackNumber);

    // Obtain the default CD device path (e.g. /dev/rdisk8).
    static QString defaultDevicePath();

    void start(lsn_t startLsn, uint32_t sectorCount);
    void setRingBuffer(AudioRingBuffer* buffer);
    void setDevicePath(const QString& path);

public slots:
    void readLoop();
    void setInterrupted();

signals:
    void finished();
    void error(QString message);

private:
#ifdef __APPLE__
    bool readRawSectors();
#endif
    bool readViaLibcdio();

    // Forward-declared; actual CdIo_t usage is only in the .cpp file.
    struct CdIoDeleter {
        void operator()(void* ptr) const;
    };
    std::unique_ptr<void, CdIoDeleter> m_cdio;

    AudioRingBuffer* m_ringBuffer = nullptr;
    QString m_devicePath;
    lsn_t m_startLsn = 0;
    uint32_t m_sectorsRemaining = 0;
    bool m_interrupted = false;
};

}  // namespace cdmanager::infrastructure::audio
