#pragma once

#include "cdmanager/application/import/DiscImportResult.h"
#include "cdmanager/domain/project/CdProject.h"
#include "cdmanager/infrastructure/disc/DiscDeviceGateway.h"

namespace cdmanager::application::import {

// 负责把“读盘结果”整理成项目模型。
// 真接光驱时，界面仍然只依赖这个服务，不直接依赖 libcdio 一类底层实现。
class DiscImportService {
public:
    explicit DiscImportService(const cdmanager::infrastructure::disc::DiscDeviceGateway& gateway);

    QVector<cdmanager::domain::disc::DriveInfo> availableDrives() const;
    cdmanager::domain::project::CdProject importFromDrive(const QString& deviceId) const;
    cdmanager::domain::project::CdProject importSampleProject() const;
    DiscImportResult initialImport() const;

private:
    cdmanager::domain::project::CdProject mapSnapshot(
        const cdmanager::domain::disc::DiscSnapshot& snapshot
    ) const;

    const cdmanager::infrastructure::disc::DiscDeviceGateway& m_gateway;
};

}  // namespace cdmanager::application::import
