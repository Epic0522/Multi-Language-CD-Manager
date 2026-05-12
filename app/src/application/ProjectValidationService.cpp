#include "cdmanager/application/ProjectValidationService.h"

namespace cdmanager::application {

ValidationReport ProjectValidationService::validateCdText(
    const cdmanager::domain::project::CdProject& project
) const {
    return m_preparationService.prepare(project).validation;
}

}  // namespace cdmanager::application
