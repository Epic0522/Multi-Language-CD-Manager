#include "cdmanager/infrastructure/audio/AudioCdReader.h"

#include <algorithm>
#include <cstring>

#include <QDebug>

#include "cdmanager/infrastructure/audio/AudioRingBuffer.h"

#if __has_include(<cdio/cdio.h>)
#include <cdio/cdio.h>
#define CDMANAGER_HAS_LIBCDIO_C 1
#else
#define CDMANAGER_HAS_LIBCDIO_C 0
#endif

#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#include <QThread>
#endif

#if CDMANAGER_HAS_LIBCDIO_C
static_assert(cdmanager::infrastructure::audio::kCdFramesizeRaw == CDIO_CD_FRAMESIZE_RAW,
              "CD frame size mismatch between local constant and libcdio");
#endif

namespace cdmanager::infrastructure::audio {

// ---------- CdIoDeleter ----------

void AudioCdReader::CdIoDeleter::operator()(void* ptr) const {
#if CDMANAGER_HAS_LIBCDIO_C
    if (ptr != nullptr) {
        cdio_destroy(static_cast<CdIo_t*>(ptr));
    }
#else
    Q_UNUSED(ptr)
#endif
}

// ---------- AudioCdReader ----------

AudioCdReader::AudioCdReader(QObject* parent)
    : QObject(parent) {
}

AudioCdReader::~AudioCdReader() {
    setInterrupted();
}

QString AudioCdReader::defaultDevicePath() {
    // Open the default device to get the correct path, same as locateTrack.
    CdTrackLocation loc = locateTrack(1);
    if (!loc.devicePath.isEmpty()) return loc.devicePath;

#if CDMANAGER_HAS_LIBCDIO_C
#ifdef __APPLE__
    char* path = cdio_get_default_device_osx();
    if (path != nullptr) {
        QString result = QString::fromUtf8(path);
        std::free(path);
        if (!result.isEmpty()) return result;
    }
#else
    char* path = cdio_get_default_device(nullptr);
    if (path != nullptr) {
        QString result = QString::fromUtf8(path);
        std::free(path);
        return result;
    }
#endif
#endif
    return {};
}

CdTrackLocation AudioCdReader::locateTrack(int trackNumber) {
    CdTrackLocation location;
    qDebug() << "locateTrack:" << trackNumber;
#if CDMANAGER_HAS_LIBCDIO_C
#ifdef __APPLE__
    CdIo_t* cdio = cdio_open_osx(nullptr);
#else
    CdIo_t* cdio = cdio_open(nullptr, DRIVER_UNKNOWN);
#endif
    qDebug() << "  cdio_open =" << (cdio != nullptr ? "ok" : "NULL");
    if (cdio == nullptr) {
        return location;
    }

#ifdef __APPLE__
    char* devPath = cdio_get_default_device_osx();
    if (devPath != nullptr) {
        location.devicePath = QString::fromUtf8(devPath);
        std::free(devPath);
    }
#else
    char* devPath = cdio_get_default_device(cdio);
    if (devPath != nullptr) {
        location.devicePath = QString::fromUtf8(devPath);
        std::free(devPath);
    }
#endif
    if (location.devicePath.isEmpty()) {
        location.devicePath = defaultDevicePath();
    }

    const track_t firstTrack = cdio_get_first_track_num(cdio);
    const track_t lastTrack = cdio_get_last_track_num(cdio);
    const track_t tn = static_cast<track_t>(trackNumber);

    if (tn < firstTrack || tn > lastTrack) {
        cdio_destroy(cdio);
        return location;
    }

    const ::lsn_t libcdioLsn = cdio_get_track_lsn(cdio, tn);
    location.startLsn = static_cast<lsn_t>(libcdioLsn);
    location.sectorCount = static_cast<uint32_t>(cdio_get_track_sec_count(cdio, tn));
    location.valid = (libcdioLsn != CDIO_INVALID_LSN && location.sectorCount > 0);
    qDebug() << "  LSN:" << location.startLsn << "sectors:" << location.sectorCount
             << "valid:" << location.valid << "devicePath:" << location.devicePath;
    cdio_destroy(cdio);
#else
    Q_UNUSED(trackNumber)
#endif
    return location;
}

void AudioCdReader::start(lsn_t startLsn, uint32_t sectorCount) {
    m_startLsn = startLsn;
    m_sectorsRemaining = sectorCount;
    m_interrupted = false;
}

void AudioCdReader::setRingBuffer(AudioRingBuffer* buffer) {
    m_ringBuffer = buffer;
}

void AudioCdReader::setDevicePath(const QString& path) {
    m_devicePath = path;
}

void AudioCdReader::readLoop() {
    if (m_ringBuffer == nullptr) {
        Q_EMIT error(QStringLiteral("No ring buffer set on AudioCdReader."));
        return;
    }

    bool ok = false;

#ifdef __APPLE__
    if (!m_devicePath.isEmpty()) {
        ok = readRawSectors();
    }
#endif

    if (!ok) {
        ok = readViaLibcdio();
    }

    if (ok) {
        Q_EMIT finished();
    }
}

#ifdef __APPLE__
bool AudioCdReader::readRawSectors() {
    const int fd = open(m_devicePath.toUtf8().constData(), O_RDONLY);
    if (fd < 0) {
        Q_EMIT error(
            QStringLiteral("Cannot open %1 for raw audio read.").arg(m_devicePath)
        );
        return false;
    }

    lsn_t currentLsn = m_startLsn;

    while (m_sectorsRemaining > 0 && !m_interrupted) {
        const uint32_t batch = std::min(
            static_cast<uint32_t>(kSectorsPerRead), m_sectorsRemaining
        );

        QByteArray buffer(static_cast<int>(batch) * kBytesPerSector, '\0');
        const off_t offset = static_cast<off_t>(currentLsn) * kBytesPerSector;
        const size_t totalBytes = static_cast<size_t>(batch) * kBytesPerSector;

        size_t bytesRead = 0;
        char* dest = buffer.data();
        bool readError = false;

        while (bytesRead < totalBytes && !m_interrupted) {
            const ssize_t n = pread(
                fd,
                dest + bytesRead,
                totalBytes - bytesRead,
                offset + static_cast<off_t>(bytesRead)
            );
            if (n <= 0) {
                readError = true;
                break;
            }
            bytesRead += static_cast<size_t>(n);
        }

        if (readError) {
            close(fd);
            Q_EMIT error(
                QStringLiteral("Raw read failed at LSN %1.").arg(currentLsn)
            );
            return false;
        }

        m_ringBuffer->write(buffer);
        currentLsn += static_cast<lsn_t>(batch);
        m_sectorsRemaining -= batch;

        // Pause if the ring buffer has enough data queued.
        // This prevents flooding the audio pipeline with data
        // faster than QAudioSink can consume it at 1x speed.
        while (m_ringBuffer->available() > AudioRingBuffer::kDefaultCapacity / 2
               && !m_interrupted) {
            QThread::msleep(20);
        }
    }

    close(fd);
    m_ringBuffer->setFinished();
    return true;
}
#endif  // __APPLE__

bool AudioCdReader::readViaLibcdio() {
#if CDMANAGER_HAS_LIBCDIO_C
    m_cdio.reset();
#ifdef __APPLE__
    CdIo_t* cdio = cdio_open_osx(nullptr);
#else
    CdIo_t* cdio = cdio_open(nullptr, DRIVER_UNKNOWN);
#endif
    if (cdio == nullptr) {
        Q_EMIT error(QStringLiteral("cdio_open failed — no CD device available."));
        return false;
    }
    m_cdio.reset(cdio);

    lsn_t currentLsn = m_startLsn;

    while (m_sectorsRemaining > 0 && !m_interrupted) {
        const uint32_t batch = std::min(
            static_cast<uint32_t>(kSectorsPerRead), m_sectorsRemaining
        );

        QByteArray buffer(static_cast<int>(batch) * kBytesPerSector, '\0');
        const driver_return_code_t rc = cdio_read_audio_sectors(
            cdio,
            buffer.data(),
            static_cast<::lsn_t>(currentLsn),
            batch
        );

        if (rc != DRIVER_OP_SUCCESS) {
            m_cdio.reset();
            Q_EMIT error(
                QStringLiteral("cdio_read_audio_sectors failed at LSN %1.").arg(currentLsn)
            );
            return false;
        }

        m_ringBuffer->write(buffer);
        currentLsn += static_cast<lsn_t>(batch);
        m_sectorsRemaining -= batch;
    }

    m_cdio.reset();
    m_ringBuffer->setFinished();
    return true;
#else
    Q_UNUSED(m_startLsn)
    Q_UNUSED(m_sectorsRemaining)
    Q_EMIT error(QStringLiteral("Neither raw device I/O nor libcdio is available."));
    return false;
#endif
}

void AudioCdReader::setInterrupted() {
    m_interrupted = true;
}

}  // namespace cdmanager::infrastructure::audio
