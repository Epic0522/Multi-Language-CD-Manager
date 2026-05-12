#pragma once

#include "cdmanager/application/ProjectOverview.h"
#include "cdmanager/domain/project/CdProject.h"

namespace cdmanager::application {

class ProjectOverviewBuilder {
public:
    ProjectOverview build(const cdmanager::domain::project::CdProject& project) const;

private:
    QString formatDuration(int seconds) const;
};

}  // namespace cdmanager::application
