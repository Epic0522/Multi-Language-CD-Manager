#include <algorithm>

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "cdmanager/infrastructure/burn/DiscRecordingBurner.h"
#include "cdmanager/tools/cdtextdiff/CdTextDocumentParser.h"
#include "cdmanager/tools/cdtextdiff/CurrentProjectPackExporter.h"

namespace {

using namespace cdmanager::tools::cdtextdiff;

void writeJsonIfRequested(const QJsonObject& object, const QString& path, QString& errorMessage) {
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMessage = QStringLiteral("Failed to open JSON output: %1").arg(path);
        return;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QStringList parserArgs(QCoreApplication& app, const QStringList& positional) {
    QStringList args;
    args.append(app.arguments().constFirst());
    args.append(positional);
    return args;
}

int runCheck(QCoreApplication& app, const QStringList& positional) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Validate that DiscRecording accepts the current raw CD-TEXT pack blob.")
    );
    parser.addHelpOption();

    QCommandLineOption fixtureOption(QStringLiteral("fixture"),
                                     QStringLiteral("Built-in fixture name."),
                                     QStringLiteral("name"),
                                     QStringLiteral("two-track-japanese"));
    QCommandLineOption sampleDirOption(QStringLiteral("sample-dir"),
                                       QStringLiteral("Path to a captured reference sample directory."),
                                       QStringLiteral("path"));
    QCommandLineOption cdtInputOption(QStringLiteral("cdt-in"),
                                      QStringLiteral("Path to a raw CD-TEXT .cdt/blob artifact to analyze directly."),
                                      QStringLiteral("path"));
    QCommandLineOption expectBlockCountOption(QStringLiteral("expect-block-count"),
                                              QStringLiteral("Fail unless DiscRecording parses exactly this many CD-TEXT block(s)."),
                                              QStringLiteral("count"));
    QCommandLineOption jsonOption(QStringLiteral("json"),
                                  QStringLiteral("Write analysis JSON report to path."),
                                  QStringLiteral("path"));
    parser.addOption(fixtureOption);
    parser.addOption(sampleDirOption);
    parser.addOption(cdtInputOption);
    parser.addOption(expectBlockCountOption);
    parser.addOption(jsonOption);
    parser.process(parserArgs(app, positional));

    const bool hasSampleDir = !parser.value(sampleDirOption).isEmpty();
    const bool hasCdtInput = !parser.value(cdtInputOption).isEmpty();
    if ((hasSampleDir ? 1 : 0) + (hasCdtInput ? 1 : 0) > 1) {
        qCritical().noquote() << QStringLiteral("Use only one of --sample-dir or --cdt-in.");
        return 1;
    }

    QVector<cdmanager::application::burn::CdTextPack> packs;
    QString sourceLabel;
    if (hasCdtInput) {
        CdTextDocumentParser documentParser;
        const auto parseResult = documentParser.parseFile(parser.value(cdtInputOption), InputFormat::Cdt);
        if (!parseResult.ok) {
            qCritical().noquote() << parseResult.errorMessage;
            return 1;
        }
        packs.reserve(parseResult.document.packs.size());
        for (const auto& parsedPack : parseResult.document.packs) {
            if (parsedPack.bytes.size() < cdmanager::application::burn::kPackTotalSize) {
                qCritical().noquote()
                    << QStringLiteral("CDT input pack %1 is shorter than %2 bytes.")
                           .arg(parsedPack.sourceIndex)
                           .arg(cdmanager::application::burn::kPackTotalSize);
                return 1;
            }
            cdmanager::application::burn::CdTextPack pack;
            std::copy_n(parsedPack.bytes.constData(),
                        cdmanager::application::burn::kPackTotalSize,
                        reinterpret_cast<char*>(pack.data.data()));
            packs.append(pack);
        }
        sourceLabel = parser.value(cdtInputOption);
    } else {
        CurrentProjectPackExporter exporter;
        ExportCurrentResult exportResult;
        if (hasSampleDir) {
            exportResult = exporter.exportReferenceSampleDir(parser.value(sampleDirOption));
        } else {
            exportResult = exporter.exportFixture(parser.value(fixtureOption));
        }

        if (!exportResult.ok) {
            qCritical().noquote() << exportResult.errorMessage;
            return 1;
        }
        packs = exportResult.assembly.packs;
        sourceLabel = hasSampleDir
            ? parser.value(sampleDirOption)
            : QStringLiteral("fixture:%1").arg(parser.value(fixtureOption));
    }

    const auto analysis = cdmanager::infrastructure::burn::DiscRecordingBurner::analyzeCdTextPacks(
        packs
    );

    const bool hasExpectedBlockCount = parser.isSet(expectBlockCountOption);
    bool expectedBlockCountOk = true;
    int expectedBlockCount = 0;
    if (hasExpectedBlockCount) {
        bool ok = false;
        expectedBlockCount = parser.value(expectBlockCountOption).toInt(&ok);
        if (!ok) {
            qCritical().noquote() << QStringLiteral("Invalid --expect-block-count value.");
            return 1;
        }
        expectedBlockCountOk = (analysis.blockCount == expectedBlockCount);
    }

    QString jsonError;
    writeJsonIfRequested(
        QJsonObject{
            {QStringLiteral("source"), sourceLabel},
            {QStringLiteral("accepted"), analysis.ok},
            {QStringLiteral("blockCount"), analysis.blockCount},
            {QStringLiteral("expectedBlockCount"), hasExpectedBlockCount ? expectedBlockCount : -1},
            {QStringLiteral("expectedBlockCountOk"), expectedBlockCountOk},
            {QStringLiteral("diagnostics"), analysis.diagnostics},
            {QStringLiteral("error"), analysis.error},
        },
        parser.value(jsonOption),
        jsonError
    );
    if (!jsonError.isEmpty()) {
        qCritical().noquote() << jsonError;
        return 1;
    }

    qInfo().noquote() << analysis.diagnostics;
    if (!analysis.ok) {
        if (!analysis.error.isEmpty()) {
            qCritical().noquote() << analysis.error;
        }
        return 2;
    }
    if (hasExpectedBlockCount && !expectedBlockCountOk) {
        qCritical().noquote()
            << QStringLiteral("DiscRecording block count mismatch: expected %1, got %2.")
                   .arg(expectedBlockCount)
                   .arg(analysis.blockCount);
        return 3;
    }

    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    @autoreleasepool {
        QCoreApplication app(argc, argv);
        app.setApplicationName(QStringLiteral("cdtext-disc-recording-check"));
        app.setApplicationVersion(QStringLiteral("0.1"));
        return runCheck(app, app.arguments().mid(1));
    }
}
