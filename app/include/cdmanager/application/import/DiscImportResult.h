#pragma once

#include <QString>

#include "cdmanager/domain/project/CdProject.h"

namespace cdmanager::application::import {

enum class DiscImportStatus {
    Success,
    FallbackSample,
    NoMediaLoaded,
    BlankWritableMedia,
    DriveVisibleButReadNotImplemented,
    NoDriveAvailable
};

struct DiscImportResult {
    DiscImportStatus status {DiscImportStatus::NoDriveAvailable};
    QString message;
    QString summary;
    cdmanager::domain::project::CdProject project;
    QString diagnostics;
};

}  // namespace cdmanager::application::import
