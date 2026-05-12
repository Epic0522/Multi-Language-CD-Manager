#include "cdmanager/tools/cdtextdiff/CurrentProjectPackExporter.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace cdmanager::tools::cdtextdiff {

namespace {

cdmanager::domain::project::CdProject twoTrackJapaneseProject() {
    cdmanager::domain::project::CdProject project;
    project.albumTitle = QStringLiteral("Ａｔａｒａｙｏ　ＣＯＬＬＥＣＴＩＯＮ　０１");
    project.albumArtist = QStringLiteral("あたらよ");
    project.albumTitlePresent = true;
    project.albumArtistPresent = true;
    project.cdTextLanguage = cdmanager::domain::cdtext::CdTextLanguage::Japanese;
    project.trackGapSeconds = 2;
    project.allowOverburn = false;
    project.tracks = {
        {
            1,
            QStringLiteral("/music/01-akanechirru.wav"),
            QStringLiteral("アカネチル"),
            QStringLiteral(""),
            true,
            true,
            259,
        },
        {
            2,
            QStringLiteral("/music/02-88.wav"),
            QStringLiteral("８．８"),
            QStringLiteral(""),
            true,
            true,
            354,
        },
    };
    return project;
}

QJsonArray validationIssuesToJson(const QVector<cdmanager::application::ValidationIssue>& issues) {
    QJsonArray array;
    for (const auto& issue : issues) {
        array.append(QJsonObject{
            {QStringLiteral("fieldLabel"), issue.fieldLabel},
            {QStringLiteral("message"), issue.message},
        });
    }
    return array;
}

QJsonArray preparedFieldsToJson(const QVector<cdmanager::application::PreparedCdTextField>& fields) {
    QJsonArray array;
    for (const auto& field : fields) {
        array.append(QJsonObject{
            {QStringLiteral("label"), field.field.label},
            {QStringLiteral("effectiveValue"), field.effectiveValue},
            {QStringLiteral("sourceNote"), field.sourceNote},
            {QStringLiteral("rawHex"), QString::fromLatin1(field.encodedBytes.toHex()).toUpper()},
            {QStringLiteral("byteCount"), field.encodedBytes.size()},
            {QStringLiteral("reusedPreservedBytes"), field.reusedPreservedBytes},
            {QStringLiteral("normalizedToFullwidth"), field.normalizedToFullwidth},
            {QStringLiteral("trackNumber"), field.field.trackNumber.has_value() ? *field.field.trackNumber : 0},
        });
    }
    return array;
}

QJsonArray writePlanToJson(const cdmanager::application::CdTextWritePlan& plan) {
    QJsonArray array;
    for (const auto& entry : plan.entries) {
        array.append(QJsonObject{
            {QStringLiteral("label"), entry.preparedField.field.label},
            {QStringLiteral("reason"), entry.reason},
            {QStringLiteral("action"), static_cast<int>(entry.action)},
            {QStringLiteral("rawHex"), QString::fromLatin1(entry.preparedField.encodedBytes.toHex()).toUpper()},
        });
    }
    return array;
}

ParsedCdTextDocument documentFromAssembly(const cdmanager::application::burn::CdTextPackAssembly& assembly,
                                          const QString& sourceLabel) {
    ParsedCdTextDocument document;
    document.format = InputFormat::PacksJson;
    document.sourcePath = sourceLabel;
    for (int index = 0; index < assembly.packs.size(); ++index) {
        ParsedCdTextPack parsedPack;
        parsedPack.bytes = QByteArray(
            reinterpret_cast<const char*>(assembly.packs.at(index).data.data()),
            static_cast<qsizetype>(assembly.packs.at(index).data.size())
        );
        parsedPack.hasCrc = true;
        parsedPack.sourceIndex = index;
        parsedPack.sourceLabel = QStringLiteral("pack %1").arg(index);
        document.packs.append(parsedPack);
    }
    document.notes.append(QStringLiteral("Exported from current MultiLanguageCDManager pack assembly pipeline."));
    return document;
}

QJsonObject parseCdrdaoReferenceMetadata(const QString& text, QStringList& notes) {
    QJsonObject metadata;

    const QRegularExpression titlePattern(QStringLiteral("TITLE\\s+\"([^\"]*)\""));
    const QRegularExpression performerPattern(QStringLiteral("PERFORMER\\s+\"([^\"]*)\""));
    const QRegularExpression sizeInfoPattern(
        QStringLiteral(R"(SIZE_INFO\s*\{([^}]*)\})"),
        QRegularExpression::DotMatchesEverythingOption
    );

    QJsonArray titles;
    auto titleIterator = titlePattern.globalMatch(text);
    while (titleIterator.hasNext()) {
        titles.append(titleIterator.next().captured(1));
    }

    QJsonArray performers;
    auto performerIterator = performerPattern.globalMatch(text);
    while (performerIterator.hasNext()) {
        performers.append(performerIterator.next().captured(1));
    }

    QJsonArray sizeInfoGroups;
    QStringList sizeInfoNotes;
    auto sizeInfoIterator = sizeInfoPattern.globalMatch(text);
    int sizeInfoIndex = 0;
    while (sizeInfoIterator.hasNext()) {
        const auto match = sizeInfoIterator.next();
        const auto parts = match.captured(1).split(u',', Qt::SkipEmptyParts);
        QJsonArray values;
        QStringList preview;
        for (const auto& part : parts) {
            const int value = part.trimmed().toInt();
            values.append(value);
            if (preview.size() < 8) {
                preview.append(QString::number(value));
            }
        }
        sizeInfoGroups.append(values);
        sizeInfoNotes.append(
            QStringLiteral("cdrdao block%1 SIZE_INFO count=%2 preview=[%3]")
                .arg(sizeInfoIndex)
                .arg(values.size())
                .arg(preview.join(QStringLiteral(", ")))
        );
        ++sizeInfoIndex;
    }

    if (!titles.isEmpty()) {
        metadata.insert(QStringLiteral("cdrdaoTitle"), titles.first());
        metadata.insert(QStringLiteral("cdrdaoTitles"), titles);
    }
    if (!performers.isEmpty()) {
        metadata.insert(QStringLiteral("cdrdaoPerformer"), performers.first());
        metadata.insert(QStringLiteral("cdrdaoPerformers"), performers);
    }
    if (!sizeInfoGroups.isEmpty()) {
        metadata.insert(QStringLiteral("cdrdaoSizeInfo"), sizeInfoGroups.first());
        metadata.insert(QStringLiteral("cdrdaoSizeInfoGroups"), sizeInfoGroups);
    }

    notes.append(QStringLiteral("cdrdao group count=%1").arg(sizeInfoGroups.size()));
    for (int index = 0; index < sizeInfoGroups.size(); ++index) {
        const QString title = index < titles.size() ? titles.at(index).toString() : QStringLiteral("(missing)");
        const QString performer = index < performers.size() ? performers.at(index).toString() : QStringLiteral("(missing)");
        notes.append(
            QStringLiteral("cdrdao block%1 title=%2 performer=%3")
                .arg(index)
                .arg(title.isEmpty() ? QStringLiteral("(empty)") : title)
                .arg(performer.isEmpty() ? QStringLiteral("(empty)") : performer)
        );
    }
    notes.append(sizeInfoNotes);
    if (sizeInfoGroups.size() > 1) {
        notes.append(QStringLiteral("cdrdao dump contains multiple CD-TEXT groups; later groups may carry the real Japanese payload even if the first group is an empty Latin placeholder."));
    }

    return metadata;
}

QJsonObject parseDrutilReferenceMetadata(const QByteArray& bytes, QStringList& notes) {
    QJsonObject metadata;
    const QString text = QString::fromUtf8(bytes);

    const QRegularExpression blockPattern(
        QStringLiteral(R"(<dict>\s*<key>Properties</key>\s*<dict>(.*?)</dict>\s*<key>Tracks</key>\s*<array>(.*?)</array>\s*</dict>)"),
        QRegularExpression::DotMatchesEverythingOption
    );
    const QRegularExpression languagePattern(QStringLiteral(R"(<key>DRCDTextLanguageKey</key>\s*<string>([^<]*)</string>)"));
    const QRegularExpression charCodePattern(QStringLiteral(R"(<key>DRCDTextCharacterCodeKey</key>\s*<integer>(\d+)</integer>)"));
    const QRegularExpression firstTrackPattern(
        QStringLiteral(R"(<dict>(.*?)</dict>)"),
        QRegularExpression::DotMatchesEverythingOption
    );
    const QRegularExpression titlePattern(QStringLiteral(R"(<key>DRCDTextTitleKey</key>\s*<string>([^<]*)</string>)"));
    const QRegularExpression performerPattern(QStringLiteral(R"(<key>DRCDTextPerformerKey</key>\s*<string>([^<]*)</string>)"));

    QJsonArray blocks;
    QStringList blockNotes;
    auto blockIterator = blockPattern.globalMatch(text);
    int blockIndex = 0;
    while (blockIterator.hasNext()) {
        const auto blockMatch = blockIterator.next();
        const QString propertiesText = blockMatch.captured(1);
        const QString tracksText = blockMatch.captured(2);

        const QString language = languagePattern.match(propertiesText).captured(1);
        const int characterCode = charCodePattern.match(propertiesText).captured(1).toInt();

        QString albumTitle;
        QString albumPerformer;
        int nonBlankTitles = 0;
        int nonBlankPerformers = 0;

        auto trackIterator = firstTrackPattern.globalMatch(tracksText);
        bool firstTrackSeen = false;
        while (trackIterator.hasNext()) {
            const auto trackMatch = trackIterator.next();
            const QString trackText = trackMatch.captured(1);
            const QString title = titlePattern.match(trackText).captured(1);
            const QString performer = performerPattern.match(trackText).captured(1);

            if (!firstTrackSeen) {
                albumTitle = title;
                albumPerformer = performer;
                firstTrackSeen = true;
            }
            if (!title.trimmed().isEmpty()) {
                ++nonBlankTitles;
            }
            if (!performer.trimmed().isEmpty()) {
                ++nonBlankPerformers;
            }
        }

        blocks.append(QJsonObject{
            {QStringLiteral("blockNumber"), blockIndex},
            {QStringLiteral("language"), language},
            {QStringLiteral("characterCode"), characterCode},
            {QStringLiteral("albumTitle"), albumTitle},
            {QStringLiteral("albumPerformer"), albumPerformer},
            {QStringLiteral("nonBlankTitles"), nonBlankTitles},
            {QStringLiteral("nonBlankPerformers"), nonBlankPerformers},
        });

        blockNotes.append(
            QStringLiteral("block%1 language=%2 char=%3 albumTitle=%4 albumPerformer=%5 nonBlankTitles=%6 nonBlankPerformers=%7")
                .arg(blockIndex)
                .arg(language.isEmpty() ? QStringLiteral("(empty)") : language)
                .arg(characterCode)
                .arg(albumTitle.isEmpty() ? QStringLiteral("(empty)") : albumTitle)
                .arg(albumPerformer.isEmpty() ? QStringLiteral("(empty)") : albumPerformer)
                .arg(nonBlankTitles)
                .arg(nonBlankPerformers)
        );
        ++blockIndex;
    }

    if (blocks.isEmpty()) {
        return metadata;
    }

    metadata.insert(QStringLiteral("drutilBlockCount"), blocks.size());
    metadata.insert(QStringLiteral("drutilBlocks"), blocks);

    notes.append(QStringLiteral("drutil block count=%1").arg(blocks.size()));
    for (const auto& blockNote : blockNotes) {
        notes.append(QStringLiteral("drutil %1").arg(blockNote));
    }
    if (blocks.size() > 1) {
        notes.append(QStringLiteral("drutil plist contains multiple language blocks; cdtext-summary.json may only describe the first block and can underrepresent the real disc structure."));
    }

    return metadata;
}

QByteArray collectSizeInfoPayloadBytes(const ParsedCdTextDocument& document) {
    QByteArray bytes;
    for (const auto& pack : document.packs) {
        if (pack.packType() != 0x8F || pack.bytes.size() < 16) {
            continue;
        }
        bytes.append(pack.bytes.mid(4, 12));
    }
    return bytes;
}

QJsonArray byteArrayToJson(const QByteArray& bytes) {
    QJsonArray array;
    for (unsigned char byte : bytes) {
        array.append(static_cast<int>(byte));
    }
    return array;
}

QString previewByteList(const QByteArray& bytes, int maxCount = 8) {
    QStringList preview;
    const int limit = std::min(maxCount, static_cast<int>(bytes.size()));
    for (int index = 0; index < limit; ++index) {
        preview.append(QString::number(static_cast<unsigned char>(bytes.at(index))));
    }
    return preview.join(QStringLiteral(", "));
}

QJsonObject buildGeneratedMetadata(const ParsedCdTextDocument& document) {
    const QByteArray sizeInfoBytes = collectSizeInfoPayloadBytes(document);

    int sizeInfoPackCount = 0;
    for (const auto& pack : document.packs) {
        if (pack.packType() == 0x8F) {
            ++sizeInfoPackCount;
        }
    }

    return QJsonObject{
        {QStringLiteral("blockCount"), document.blockCount()},
        {QStringLiteral("sizeInfoPackCount"), sizeInfoPackCount},
        {QStringLiteral("sizeInfoValueCount"), sizeInfoBytes.size()},
        {QStringLiteral("sizeInfoValues"), byteArrayToJson(sizeInfoBytes)},
    };
}

void appendSizeInfoComparisonNotes(ExportCurrentResult& result) {
    const QJsonArray referenceValues = result.referenceMetadata.value(QStringLiteral("cdrdaoSizeInfo")).toArray();
    if (referenceValues.isEmpty()) {
        return;
    }

    const int generatedPackCount = result.generatedMetadata.value(QStringLiteral("sizeInfoPackCount")).toInt();
    const QJsonArray generatedValues = result.generatedMetadata.value(QStringLiteral("sizeInfoValues")).toArray();

    result.analysisNotes.append(
        QStringLiteral("Generated blockCount=%1")
            .arg(result.generatedMetadata.value(QStringLiteral("blockCount")).toInt())
    );
    result.analysisNotes.append(
        QStringLiteral("Generated SIZE_INFO pack(s)=%1 valueCount=%2 preview=[%3]")
            .arg(generatedPackCount)
            .arg(generatedValues.size())
            .arg(previewByteList(collectSizeInfoPayloadBytes(result.document)))
    );
    result.analysisNotes.append(
        QStringLiteral("Reference blockCount=%1")
            .arg(result.document.blockCount())
    );
    result.analysisNotes.append(
        QStringLiteral("Reference SIZE_INFO valueCount=%1 preview=[%2]")
            .arg(referenceValues.size())
            .arg([&]() {
                QStringList preview;
                const int limit = std::min(8, static_cast<int>(referenceValues.size()));
                for (int index = 0; index < limit; ++index) {
                    preview.append(QString::number(referenceValues.at(index).toInt()));
                }
                return preview.join(QStringLiteral(", "));
            }())
    );

    if (generatedValues.size() != referenceValues.size()) {
        result.analysisNotes.append(
            QStringLiteral("SIZE_INFO mismatch: generated value count %1 differs from reference %2. "
                           "This means the current compare path is text-synthetic and not yet matching the "
                           "captured disc-style summary structure.")
                .arg(generatedValues.size())
                .arg(referenceValues.size())
        );
        return;
    }

    QStringList differingEntries;
    for (int index = 0; index < generatedValues.size(); ++index) {
        const int generated = generatedValues.at(index).toInt();
        const int reference = referenceValues.at(index).toInt();
        if (generated != reference) {
            differingEntries.append(
                QStringLiteral("[%1]%2->%3")
                    .arg(index)
                    .arg(generated)
                    .arg(reference)
            );
        }
    }

    if (!differingEntries.isEmpty()) {
        result.analysisNotes.append(
            QStringLiteral("SIZE_INFO mismatch: same value count but differing entries %1")
                .arg(differingEntries.join(QStringLiteral(", ")))
        );
    }
}

}  // namespace

ExportCurrentResult CurrentProjectPackExporter::exportFixture(const QString& fixtureName) const {
    const auto project = fixtureProject(fixtureName);
    if (!project.has_value()) {
        ExportCurrentResult result;
        result.ok = false;
        result.errorMessage = QStringLiteral("Unknown fixture: %1").arg(fixtureName);
        return result;
    }

    return exportProject(*project, QStringLiteral("fixture:%1").arg(fixtureName));
}

ExportCurrentResult CurrentProjectPackExporter::exportProjectSpec(const QJsonObject& projectSpec) const {
    QString error;
    const auto project = projectFromJson(projectSpec, error);
    if (!project.has_value()) {
        ExportCurrentResult result;
        result.ok = false;
        result.errorMessage = error;
        return result;
    }

    return exportProject(*project, QStringLiteral("project-spec"));
}

ExportCurrentResult CurrentProjectPackExporter::exportReferenceSampleDir(const QString& sampleDirPath) const {
    QFile summaryFile(sampleDirPath + QStringLiteral("/cdtext-summary.json"));
    if (!summaryFile.open(QIODevice::ReadOnly)) {
        ExportCurrentResult result;
        result.ok = false;
        result.errorMessage = QStringLiteral("Failed to open reference sample summary: %1")
                                  .arg(summaryFile.fileName());
        return result;
    }

    QJsonParseError jsonError;
    const auto summaryDocument = QJsonDocument::fromJson(summaryFile.readAll(), &jsonError);
    if (summaryDocument.isNull()) {
        ExportCurrentResult result;
        result.ok = false;
        result.errorMessage = QStringLiteral("Invalid reference sample summary JSON: %1")
                                  .arg(jsonError.errorString());
        return result;
    }

    auto result = exportProjectSpec(summaryDocument.object());
    if (!result.ok) {
        return result;
    }

    result.document.sourcePath = QStringLiteral("reference-sample:%1").arg(sampleDirPath);
    result.referenceNotes.append(QStringLiteral("Reference sample directory: %1").arg(sampleDirPath));

    QFile drutilPlistFile(sampleDirPath + QStringLiteral("/drutil-cdtext.plist"));
    if (drutilPlistFile.open(QIODevice::ReadOnly)) {
        const QJsonObject drutilMetadata = parseDrutilReferenceMetadata(drutilPlistFile.readAll(), result.referenceNotes);
        for (auto it = drutilMetadata.begin(); it != drutilMetadata.end(); ++it) {
            result.referenceMetadata.insert(it.key(), it.value());
        }
    } else {
        result.referenceNotes.append(QStringLiteral("drutil-cdtext.plist not found in reference sample directory."));
    }

    QFile cdrdaoFile(sampleDirPath + QStringLiteral("/cdrdao-cdtext.txt"));
    if (cdrdaoFile.open(QIODevice::ReadOnly)) {
        result.referenceMetadata = parseCdrdaoReferenceMetadata(
            QString::fromUtf8(cdrdaoFile.readAll()),
            result.referenceNotes
        );
        appendSizeInfoComparisonNotes(result);
    } else {
        result.referenceNotes.append(QStringLiteral("cdrdao-cdtext.txt not found in reference sample directory."));
    }

    return result;
}

ExportCurrentResult CurrentProjectPackExporter::exportProject(
    const cdmanager::domain::project::CdProject& project,
    const QString& sourceLabel
) const {
    ExportCurrentResult result;
    result.project = project;

    cdmanager::application::CdTextPreparationService preparationService;
    cdmanager::application::CdTextWritePlanBuilder planBuilder;
    cdmanager::application::CdTextWritePayloadBuilder payloadBuilder;
    cdmanager::application::burn::CdTextPackAssembler assembler;

    result.preparation = preparationService.prepare(project);
    if (!result.preparation.ok()) {
        result.ok = false;
        result.errorMessage = result.preparation.validation.summary();
        return result;
    }

    result.plan = planBuilder.build(result.preparation);
    result.payload = payloadBuilder.build(result.plan);
    result.assembly = assembler.assemble(result.payload);
    result.document = documentFromAssembly(result.assembly, sourceLabel);
    result.generatedMetadata = buildGeneratedMetadata(result.document);
    result.ok = true;
    return result;
}

std::optional<cdmanager::domain::project::CdProject> CurrentProjectPackExporter::fixtureProject(
    const QString& fixtureName
) const {
    if (fixtureName == QStringLiteral("sample-project")) {
        return cdmanager::domain::project::CdProject::sampleProject();
    }
    if (fixtureName == QStringLiteral("two-track-japanese")) {
        return twoTrackJapaneseProject();
    }
    return std::nullopt;
}

std::optional<cdmanager::domain::project::CdProject> CurrentProjectPackExporter::projectFromJson(
    const QJsonObject& spec,
    QString& error
) const {
    cdmanager::domain::project::CdProject project;

    project.albumTitle = spec.value(QStringLiteral("albumTitle")).toString();
    if (project.albumTitle.isEmpty()) {
        project.albumTitle = spec.value(QStringLiteral("title")).toString();
    }

    project.albumArtist = spec.value(QStringLiteral("albumArtist")).toString();
    if (project.albumArtist.isEmpty()) {
        project.albumArtist = spec.value(QStringLiteral("albumPerformer")).toString();
    }
    project.albumTitlePresent = spec.value(QStringLiteral("albumTitlePresent")).toBool(true);
    project.albumArtistPresent = spec.value(QStringLiteral("albumArtistPresent")).toBool(true);
    project.trackGapSeconds = spec.value(QStringLiteral("trackGapSeconds")).toInt(2);
    project.allowOverburn = spec.value(QStringLiteral("allowOverburn")).toBool(false);

    const QString language = spec.value(QStringLiteral("cdTextLanguage")).toString(QStringLiteral("japanese")).toLower();
    if (language == QStringLiteral("latin")) {
        project.cdTextLanguage = cdmanager::domain::cdtext::CdTextLanguage::Latin;
    } else {
        project.cdTextLanguage = cdmanager::domain::cdtext::CdTextLanguage::Japanese;
    }

    const auto trackArray = spec.value(QStringLiteral("tracks")).toArray();
    if (trackArray.isEmpty()) {
        error = QStringLiteral("Project JSON must contain at least one track.");
        return std::nullopt;
    }

    for (int index = 0; index < trackArray.size(); ++index) {
        const auto trackObject = trackArray.at(index).toObject();
        cdmanager::domain::project::Track track;
        track.number = trackObject.value(QStringLiteral("number")).toInt(index + 1);
        track.filePath = trackObject.value(QStringLiteral("filePath")).toString(
            QStringLiteral("/music/track-%1.wav").arg(track.number, 2, 10, QLatin1Char('0'))
        );
        track.title = trackObject.value(QStringLiteral("title")).toString();
        track.artist = trackObject.value(QStringLiteral("artist")).toString();
        if (track.artist.isEmpty()) {
            track.artist = trackObject.value(QStringLiteral("performer")).toString();
        }
        track.titlePresent = trackObject.value(QStringLiteral("titlePresent")).toBool(true);
        track.artistPresent = trackObject.value(QStringLiteral("artistPresent")).toBool(true);
        track.durationSeconds = trackObject.value(QStringLiteral("durationSeconds")).toInt(180);
        project.tracks.append(track);
    }

    return project;
}

QJsonObject ExportCurrentResult::toJson() const {
    QJsonArray analysisNoteArray;
    for (const auto& note : analysisNotes) {
        analysisNoteArray.append(note);
    }

    return QJsonObject{
        {QStringLiteral("ok"), ok},
        {QStringLiteral("errorMessage"), errorMessage},
        {QStringLiteral("analysisNotes"), analysisNoteArray},
        {QStringLiteral("generatedMetadata"), generatedMetadata},
        {QStringLiteral("referenceNotes"), QJsonArray::fromStringList(referenceNotes)},
        {QStringLiteral("referenceMetadata"), referenceMetadata},
        {QStringLiteral("project"), QJsonObject{
             {QStringLiteral("albumTitle"), project.albumTitle},
             {QStringLiteral("albumArtist"), project.albumArtist},
             {QStringLiteral("trackGapSeconds"), project.trackGapSeconds},
             {QStringLiteral("allowOverburn"), project.allowOverburn},
             {QStringLiteral("trackCount"), static_cast<int>(project.tracks.size())},
         }},
        {QStringLiteral("preparation"), QJsonObject{
             {QStringLiteral("ok"), preparation.ok()},
             {QStringLiteral("summary"), preparation.validation.summary()},
             {QStringLiteral("issues"), validationIssuesToJson(preparation.validation.issues)},
             {QStringLiteral("preparedFields"), preparedFieldsToJson(preparation.preparedFields)},
         }},
        {QStringLiteral("writePlan"), QJsonObject{
             {QStringLiteral("writableFieldCount"), plan.writableFieldCount()},
             {QStringLiteral("skippedFieldCount"), plan.skippedFieldCount()},
             {QStringLiteral("entries"), writePlanToJson(plan)},
         }},
        {QStringLiteral("writePayload"), QJsonObject{
             {QStringLiteral("writableFieldCount"), payload.writableFieldCount()},
             {QStringLiteral("skippedFieldCount"), payload.skippedFieldCount()},
             {QStringLiteral("writableByteCount"), payload.writableByteCount()},
         }},
        {QStringLiteral("packAssembly"), QJsonObject{
             {QStringLiteral("packCount"), assembly.packCount()},
             {QStringLiteral("totalByteCount"), assembly.totalByteCount()},
             {QStringLiteral("diagnosticSummary"), assembly.diagnosticSummary()},
             {QStringLiteral("packs"), packsToJson(document.packs)},
         }},
    };
}

QString ExportCurrentResult::toText() const {
    QStringList lines;
    lines.append(QStringLiteral("Current export: %1").arg(ok ? QStringLiteral("ok") : QStringLiteral("failed")));
    if (!errorMessage.isEmpty()) {
        lines.append(QStringLiteral("Error: %1").arg(errorMessage));
        return lines.join(u'\n');
    }

    lines.append(QStringLiteral("Album : %1 / %2").arg(project.albumTitle, project.albumArtist));
    lines.append(QStringLiteral("Tracks: %1").arg(project.tracks.size()));
    lines.append(QStringLiteral("Prepared fields: %1").arg(preparation.preparedFields.size()));
    lines.append(QStringLiteral("Write plan: writable=%1 skipped=%2")
                     .arg(plan.writableFieldCount())
                     .arg(plan.skippedFieldCount()));
    lines.append(QStringLiteral("Pack assembly: %1 packs, %2 bytes")
                     .arg(assembly.packCount())
                     .arg(assembly.totalByteCount()));
    lines.append(QStringLiteral("Diagnostic: %1").arg(assembly.diagnosticSummary()));
    for (const auto& note : analysisNotes) {
        lines.append(QStringLiteral("Analysis : %1").arg(note));
    }
    for (const auto& note : referenceNotes) {
        lines.append(QStringLiteral("Reference: %1").arg(note));
    }
    return lines.join(u'\n');
}

}  // namespace cdmanager::tools::cdtextdiff
