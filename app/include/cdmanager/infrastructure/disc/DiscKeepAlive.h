#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace cdmanager::infrastructure::disc {

class DrutilCommandRunner;

// Periodically sends a lightweight status query to the optical drive
// to prevent macOS from spinning it down after idle time.
//
// The keep-alive fires every ~25 seconds (well within the typical
// 1–2 minute auto-sleep window).  Uses drutil status, which is a
// read-only command that doesn't interfere with playback or reading.
class DiscKeepAlive : public QObject {
    Q_OBJECT

public:
    static constexpr int kIntervalMs = 25 * 1000;

    explicit DiscKeepAlive(QObject* parent = nullptr);

    void start(const QString& deviceId);
    void stop();
    bool isActive() const;

private slots:
    void ping();

private:
    QTimer m_timer;
    QString m_deviceId;

    Q_DISABLE_COPY(DiscKeepAlive)
};

}  // namespace cdmanager::infrastructure::disc
