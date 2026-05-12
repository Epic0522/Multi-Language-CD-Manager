#pragma once

#include <QObject>

namespace cdmanager::infrastructure::disc {

// 基于系统介质事件的轻量监听器。
// 当前只关心“有盘/无盘发生变化”，由上层决定如何重新导入。
class DiscMediaMonitor final : public QObject {
    Q_OBJECT

public:
    explicit DiscMediaMonitor(QObject* parent = nullptr);
    ~DiscMediaMonitor() override;

    void start();
    void stop();
    bool isActive() const;

signals:
    void mediaChanged();

private:
    void* m_implHandle {nullptr};
};

}  // namespace cdmanager::infrastructure::disc
