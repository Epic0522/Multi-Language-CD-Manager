#pragma once

#include "cdmanager/infrastructure/disc/DiscDeviceGateway.h"

namespace cdmanager::infrastructure::disc {

// 真实系统网关的第一阶段实现：
// 先解决“能不能看到光驱”，后面再逐步补真实读盘与 CD-TEXT 提取。
class SystemDiscGateway final : public DiscDeviceGateway {
public:
    GatewayMode mode() const override;
    QVector<cdmanager::domain::disc::DriveInfo> listDrives() const override;
    cdmanager::domain::disc::DiscSnapshot readDisc(const QString& deviceId) const override;
    cdmanager::domain::disc::DiscSnapshot readSampleDisc() const override;

private:
    QVector<cdmanager::domain::disc::DriveInfo> listDrivesFromDrutil() const;
};

}  // namespace cdmanager::infrastructure::disc
