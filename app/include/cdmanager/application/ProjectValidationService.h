#pragma once

#include "cdmanager/application/CdTextPreparationService.h"
#include "cdmanager/application/ValidationTypes.h"
#include "cdmanager/domain/project/CdProject.h"

namespace cdmanager::application {

class ProjectValidationService {
public:
    ValidationReport validateCdText(const cdmanager::domain::project::CdProject& project) const;

private:
    CdTextPreparationService m_preparationService;
};

}  // namespace cdmanager::application
