#pragma once

#include "cdmanager/application/CdTextTypes.h"
#include "cdmanager/domain/project/CdProject.h"

namespace cdmanager::application {

class CdTextPreparationService {
public:
    CdTextPreparationResult prepare(const cdmanager::domain::project::CdProject& project) const;
};

}  // namespace cdmanager::application
