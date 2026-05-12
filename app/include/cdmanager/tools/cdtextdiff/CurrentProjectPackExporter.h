#pragma once

#include <optional>

#include <QJsonObject>

#include "cdmanager/application/CdTextPreparationService.h"
#include "cdmanager/application/CdTextWritePayloadBuilder.h"
#include "cdmanager/application/CdTextWritePlanBuilder.h"
#include "cdmanager/application/burn/CdTextPackAssembler.h"
#include "cdmanager/domain/project/CdProject.h"
#include "cdmanager/tools/cdtextdiff/CdTextDiffTypes.h"

namespace cdmanager::tools::cdtextdiff {

struct ExportCurrentResult {
    bool ok {false};
    QString errorMessage;
    cdmanager::domain::project::CdProject project;
    cdmanager::application::CdTextPreparationResult preparation;
    cdmanager::application::CdTextWritePlan plan;
    cdmanager::application::CdTextWritePayload payload;
    cdmanager::application::burn::CdTextPackAssembly assembly;
    ParsedCdTextDocument document;
    QStringList analysisNotes;
    QJsonObject generatedMetadata;
    QStringList referenceNotes;
    QJsonObject referenceMetadata;

    QJsonObject toJson() const;
    QString toText() const;
};

class CurrentProjectPackExporter {
public:
    ExportCurrentResult exportFixture(const QString& fixtureName) const;
    ExportCurrentResult exportProjectSpec(const QJsonObject& projectSpec) const;
    ExportCurrentResult exportReferenceSampleDir(const QString& sampleDirPath) const;

private:
    ExportCurrentResult exportProject(const cdmanager::domain::project::CdProject& project,
                                      const QString& sourceLabel) const;
    std::optional<cdmanager::domain::project::CdProject> fixtureProject(const QString& fixtureName) const;
    std::optional<cdmanager::domain::project::CdProject> projectFromJson(const QJsonObject& spec,
                                                                         QString& error) const;
};

}  // namespace cdmanager::tools::cdtextdiff
