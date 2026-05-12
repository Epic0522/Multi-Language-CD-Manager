#include "cdmanager/infrastructure/disc/DiscKeepAlive.h"

#include "cdmanager/infrastructure/disc/DrutilCommandRunner.h"

namespace cdmanager::infrastructure::disc {

DiscKeepAlive::DiscKeepAlive(QObject* parent)
    : QObject(parent) {
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &DiscKeepAlive::ping);
}

void DiscKeepAlive::start(const QString& deviceId) {
    if (m_timer.isActive()) {
        stop();
    }

    m_deviceId = deviceId;

    // 首次立即执行，后续由 timer 驱动。
    ping();
    m_timer.start(kIntervalMs);
}

void DiscKeepAlive::stop() {
    m_timer.stop();
}

bool DiscKeepAlive::isActive() const {
    return m_timer.isActive();
}

void DiscKeepAlive::ping() {
    QStringList args;
    const QString prefix = QStringLiteral("drutil-index://");
    if (m_deviceId.startsWith(prefix)) {
        const QString index = m_deviceId.mid(prefix.size());
        if (!index.isEmpty()) {
            args = {QStringLiteral("-drive"), index, QStringLiteral("status")};
        }
    }
    if (args.isEmpty()) {
        args = {QStringLiteral("-drive"), QStringLiteral("external"), QStringLiteral("status")};
    }

    DrutilCommandRunner runner;
    runner.run(args);
}

}  // namespace cdmanager::infrastructure::disc
