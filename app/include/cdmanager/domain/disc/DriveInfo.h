#pragma once

#include <QString>

namespace cdmanager::domain::disc {

// 表示一个可供读取或刻录的光驱。后面接入 libcdio 时，deviceId 会映射到底层设备路径。
struct DriveInfo {
    QString deviceId;
    QString displayName;
    bool canRead {false};
    bool canWrite {false};
    bool hasMediaLoaded {false};
};

}  // namespace cdmanager::domain::disc
