#pragma once

#include <QString>
#include <QVector>

#include "cdmanager/domain/disc/DiscSnapshot.h"
#include "cdmanager/domain/disc/DriveInfo.h"

namespace cdmanager::infrastructure::disc {

enum class GatewayMode {
    Sample,
    System
};

// 底层光驱访问抽象。
// 先用样本实现跑通流程，后面再接 libcdio / 系统设备枚举。
class DiscDeviceGateway {
public:
    virtual ~DiscDeviceGateway() = default;

    virtual GatewayMode mode() const = 0;
    virtual QVector<cdmanager::domain::disc::DriveInfo> listDrives() const = 0;
    virtual cdmanager::domain::disc::DiscSnapshot readDisc(const QString& deviceId) const = 0;
    virtual cdmanager::domain::disc::DiscSnapshot readSampleDisc() const = 0;
};

}  // namespace cdmanager::infrastructure::disc
