#include "cdmanager/infrastructure/audio/AudioRingBuffer.h"

#include <algorithm>

#include <QMutexLocker>

namespace cdmanager::infrastructure::audio {

AudioRingBuffer::AudioRingBuffer(int capacity)
    : m_buffer(capacity, '\0') {
}

void AudioRingBuffer::write(const QByteArray& data) {
    QMutexLocker locker(&m_mutex);
    const int writeSize = static_cast<int>(std::min(data.size(), m_buffer.size() - static_cast<qsizetype>(m_available)));
    if (writeSize <= 0) {
        return;
    }

    const int firstChunk = std::min(writeSize, static_cast<int>(m_buffer.size()) - m_writePos);
    std::memcpy(m_buffer.data() + m_writePos, data.constData(),
                static_cast<std::size_t>(firstChunk));

    const int secondChunk = writeSize - firstChunk;
    if (secondChunk > 0) {
        std::memcpy(m_buffer.data(), data.constData() + firstChunk,
                    static_cast<std::size_t>(secondChunk));
    }

    m_writePos = (m_writePos + writeSize) % m_buffer.size();
    m_available += writeSize;
}

QByteArray AudioRingBuffer::read(int maxSize) {
    QMutexLocker locker(&m_mutex);
    const int readSize = std::min(maxSize, m_available);
    if (readSize <= 0) {
        return {};
    }

    QByteArray result(readSize, '\0');
    const int firstChunk = std::min(readSize, static_cast<int>(m_buffer.size()) - m_readPos);
    std::memcpy(result.data(), m_buffer.constData() + m_readPos,
                static_cast<std::size_t>(firstChunk));

    const int secondChunk = readSize - firstChunk;
    if (secondChunk > 0) {
        std::memcpy(result.data() + firstChunk, m_buffer.constData(),
                    static_cast<std::size_t>(secondChunk));
    }

    m_readPos = (m_readPos + readSize) % m_buffer.size();
    m_available -= readSize;

    return result;
}

int AudioRingBuffer::available() const {
    QMutexLocker locker(&m_mutex);
    return m_available;
}

int AudioRingBuffer::writeSpace() const {
    QMutexLocker locker(&m_mutex);
    return m_buffer.size() - m_available;
}

void AudioRingBuffer::clear() {
    QMutexLocker locker(&m_mutex);
    m_readPos = 0;
    m_writePos = 0;
    m_available = 0;
    m_finished = false;
}

void AudioRingBuffer::setFinished() {
    QMutexLocker locker(&m_mutex);
    m_finished = true;
}

bool AudioRingBuffer::isFinished() const {
    QMutexLocker locker(&m_mutex);
    return m_finished;
}

}  // namespace cdmanager::infrastructure::audio
