#pragma once

#include "cdmanager/infrastructure/disc/DiscDeviceGateway.h"

namespace cdmanager::infrastructure::disc {

// 当前阶段的假网关：
// 1. 让导入链路先跑起来
// 2. 给 UI 和项目模型一个真实的落点
// 3. 后面只替换这个类，不动上层流程
class LegacySampleDiscGateway final : public DiscDeviceGateway {
public:
    GatewayMode mode() const override;
    QVector<cdmanager::domain::disc::DriveInfo> listDrives() const override;
    cdmanager::domain::disc::DiscSnapshot readDisc(const QString& deviceId) const override;
    cdmanager::domain::disc::DiscSnapshot readSampleDisc() const override;
};

}  // namespace cdmanager::infrastructure::disc
