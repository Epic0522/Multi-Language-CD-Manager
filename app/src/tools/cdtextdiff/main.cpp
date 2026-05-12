#include <algorithm>

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QStringList>

#include "cdmanager/tools/cdtextdiff/CdTextDiffEngine.h"
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

void writeRawBlobIfRequested(const ParsedCdTextDocument& document, const QString& path, QString& errorMessage) {
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMessage = QStringLiteral("Failed to open raw output: %1").arg(path);
        return;
    }

    QByteArray blob;
    for (const auto& pack : document.packs) {
        blob.append(pack.bytes);
    }
    file.write(blob);
}

void writeSonyBinIfRequested(const ParsedCdTextDocument& document, const QString& path, QString& errorMessage) {
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMessage = QStringLiteral("Failed to open Sony BIN output: %1").arg(path);
        return;
    }

    QByteArray blob;
    for (const auto& pack : document.packs) {
        blob.append(pack.bytes);
    }
    blob.append('\0');
    file.write(blob);
}

std::optional<InputFormat> requiredFormat(const QString& rawValue) {
    const QString value = rawValue.trimmed().toLower();
    if (value == QStringLiteral("cdt")) {
        return InputFormat::Cdt;
    }
    if (value == QStringLiteral("packs-json")) {
        return InputFormat::PacksJson;
    }
    if (value == QStringLiteral("sample-dump")) {
        return InputFormat::SampleDump;
    }
    if (value == QStringLiteral("reference-sample")) {
        return InputFormat::ReferenceSample;
    }
    return std::nullopt;
}

std::optional<CompareMode> requiredCompareMode(const QString& rawValue) {
    const QString value = rawValue.trimmed().toLower();
    if (value.isEmpty() || value == QStringLiteral("exact")) {
        return CompareMode::Exact;
    }
    if (value == QStringLiteral("structure")) {
        return CompareMode::Structure;
    }
    if (value == QStringLiteral("schema")) {
        return CompareMode::Schema;
    }
    return std::nullopt;
}

QStringList parserArgs(QCoreApplication& app, const QStringList& positional) {
    QStringList args;
    args.append(app.arguments().constFirst());
    args.append(positional);
    return args;
}

QString documentSummaryText(const ParsedCdTextDocument& document) {
    QStringList lines;
    lines.append(QStringLiteral("Parsed %1 packs from %2 (%3)")
                     .arg(document.packCount())
                     .arg(document.sourcePath)
                     .arg(inputFormatName(document.format)));

    for (const auto& note : document.notes) {
        lines.append(QStringLiteral("Note : %1").arg(note));
    }

    const int previewCount = std::min<int>(8, static_cast<int>(document.packs.size()));
    for (int index = 0; index < previewCount; ++index) {
        const auto& pack = document.packs.at(index);
        lines.append(QStringLiteral("Pack %1: %2 track=%3 seq=%4 block=%5 cpos=%6 %7")
                         .arg(index)
                         .arg(pack.packTypeLabel())
                         .arg(pack.trackNumber())
                         .arg(pack.sequenceNumber())
                         .arg(pack.blockNumber())
                         .arg(pack.characterPosition())
                         .arg(pack.byteHex()));
    }

    if (document.packs.size() > previewCount) {
        lines.append(QStringLiteral("... %1 more pack(s) omitted from text preview.")
                         .arg(document.packs.size() - previewCount));
    }

    return lines.join(u'\n');
}

int runParse(QCoreApplication& app, const QStringList& positional) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Parse CD-TEXT binary/dump inputs into a normalized structure."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("input"), QStringLiteral("Input file path."));

    QCommandLineOption formatOption(QStringLiteral("format"), QStringLiteral("Input format: cdt | packs-json | sample-dump | reference-sample"), QStringLiteral("format"));
    QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Write parsed JSON report to path."), QStringLiteral("path"));
    parser.addOption(formatOption);
    parser.addOption(jsonOption);
    parser.process(parserArgs(app, positional));

    if (parser.positionalArguments().isEmpty()) {
        parser.showHelp(1);
    }

    const auto format = requiredFormat(parser.value(formatOption));
    if (!format.has_value()) {
        qCritical().noquote() << QStringLiteral("Unknown --format value.");
        return 1;
    }

    CdTextDocumentParser documentParser;
    const auto result = documentParser.parseFile(parser.positionalArguments().constFirst(), *format);
    if (!result.ok) {
        qCritical().noquote() << result.errorMessage;
        return 1;
    }

    QString jsonError;
    writeJsonIfRequested(result.document.toJson(), parser.value(jsonOption), jsonError);
    if (!jsonError.isEmpty()) {
        qCritical().noquote() << jsonError;
        return 1;
    }

    qInfo().noquote() << documentSummaryText(result.document);
    return 0;
}

int runCompare(QCoreApplication& app, const QStringList& positional) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Strictly compare two CD-TEXT sources pack-by-pack and byte-by-byte."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("left"), QStringLiteral("Left input path."));
    parser.addPositionalArgument(QStringLiteral("right"), QStringLiteral("Right input path."));

    QCommandLineOption leftFormatOption(QStringLiteral("left-format"), QStringLiteral("Left input format."), QStringLiteral("format"));
    QCommandLineOption rightFormatOption(QStringLiteral("right-format"), QStringLiteral("Right input format."), QStringLiteral("format"));
    QCommandLineOption modeOption(QStringLiteral("mode"), QStringLiteral("Compare mode: exact | structure | schema"), QStringLiteral("mode"), QStringLiteral("exact"));
    QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Write diff JSON report to path."), QStringLiteral("path"));
    parser.addOption(leftFormatOption);
    parser.addOption(rightFormatOption);
    parser.addOption(modeOption);
    parser.addOption(jsonOption);
    parser.process(parserArgs(app, positional));

    if (parser.positionalArguments().size() < 2) {
        parser.showHelp(1);
    }

    const auto leftFormat = requiredFormat(parser.value(leftFormatOption));
    const auto rightFormat = requiredFormat(parser.value(rightFormatOption));
    if (!leftFormat.has_value() || !rightFormat.has_value()) {
        qCritical().noquote() << QStringLiteral("Both --left-format and --right-format must be one of: cdt, packs-json, sample-dump, reference-sample.");
        return 1;
    }
    const auto compareMode = requiredCompareMode(parser.value(modeOption));
    if (!compareMode.has_value()) {
        qCritical().noquote() << QStringLiteral("Unknown --mode value. Use exact, structure, or schema.");
        return 1;
    }

    CdTextDocumentParser documentParser;
    const auto left = documentParser.parseFile(parser.positionalArguments().at(0), *leftFormat);
    const auto right = documentParser.parseFile(parser.positionalArguments().at(1), *rightFormat);
    if (!left.ok) {
        qCritical().noquote() << left.errorMessage;
        return 1;
    }
    if (!right.ok) {
        qCritical().noquote() << right.errorMessage;
        return 1;
    }

    CdTextDiffEngine engine;
    const auto report = engine.compare(left.document, right.document, *compareMode);

    QString jsonError;
    writeJsonIfRequested(report.toJson(), parser.value(jsonOption), jsonError);
    if (!jsonError.isEmpty()) {
        qCritical().noquote() << jsonError;
        return 1;
    }

    qInfo().noquote() << report.toText();
    return report.identical ? 0 : 2;
}

int runExportCurrent(QCoreApplication& app, const QStringList& positional) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Export the current MultiLanguageCDManager pack assembly pipeline into a normalized developer report."));
    parser.addHelpOption();

    QCommandLineOption fixtureOption(QStringLiteral("fixture"),
                                     QStringLiteral("Built-in fixture name: sample-project | two-track-japanese"),
                                     QStringLiteral("name"),
                                     QStringLiteral("two-track-japanese"));
    QCommandLineOption projectJsonOption(QStringLiteral("project-json"),
                                         QStringLiteral("Path to JSON project spec instead of a built-in fixture."),
                                         QStringLiteral("path"));
    QCommandLineOption sampleDirOption(QStringLiteral("sample-dir"),
                                       QStringLiteral("Path to a captured reference sample directory containing cdtext-summary.json."),
                                       QStringLiteral("path"));
    QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Write export JSON report to path."), QStringLiteral("path"));
    QCommandLineOption rawOption(QStringLiteral("raw-out"),
                                 QStringLiteral("Write concatenated raw pack bytes to path."),
                                 QStringLiteral("path"));
    QCommandLineOption cdtOption(QStringLiteral("cdt-out"),
                                 QStringLiteral("Write concatenated raw pack bytes to path using a .cdt-style artifact name."),
                                 QStringLiteral("path"));
    QCommandLineOption sonyBinOption(QStringLiteral("sony-bin-out"),
                                     QStringLiteral("Write a Sony-style lead-in BIN artifact (18xN+1 bytes, trailing NUL terminator) to path."),
                                     QStringLiteral("path"));
    parser.addOption(fixtureOption);
    parser.addOption(projectJsonOption);
    parser.addOption(sampleDirOption);
    parser.addOption(jsonOption);
    parser.addOption(rawOption);
    parser.addOption(cdtOption);
    parser.addOption(sonyBinOption);
    parser.process(parserArgs(app, positional));

    CurrentProjectPackExporter exporter;
    ExportCurrentResult result;

    const int sourceOptionCount
        = (!parser.value(projectJsonOption).isEmpty() ? 1 : 0)
          + (!parser.value(sampleDirOption).isEmpty() ? 1 : 0);
    if (sourceOptionCount > 1) {
        qCritical().noquote() << QStringLiteral("Use only one of --project-json or --sample-dir.");
        return 1;
    }
    if (!parser.value(rawOption).isEmpty() && !parser.value(cdtOption).isEmpty()) {
        qCritical().noquote() << QStringLiteral("Use only one of --raw-out or --cdt-out.");
        return 1;
    }

    if (!parser.value(sampleDirOption).isEmpty()) {
        result = exporter.exportReferenceSampleDir(parser.value(sampleDirOption));
    } else if (!parser.value(projectJsonOption).isEmpty()) {
        QFile file(parser.value(projectJsonOption));
        if (!file.open(QIODevice::ReadOnly)) {
            qCritical().noquote() << QStringLiteral("Failed to open project JSON: %1").arg(parser.value(projectJsonOption));
            return 1;
        }
        QJsonParseError jsonError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &jsonError);
        if (document.isNull()) {
            qCritical().noquote() << QStringLiteral("Invalid project JSON: %1").arg(jsonError.errorString());
            return 1;
        }
        result = exporter.exportProjectSpec(document.object());
    } else {
        result = exporter.exportFixture(parser.value(fixtureOption));
    }

    if (!result.ok) {
        qCritical().noquote() << result.errorMessage;
        return 1;
    }

    QString jsonError;
    writeJsonIfRequested(result.toJson(), parser.value(jsonOption), jsonError);
    if (!jsonError.isEmpty()) {
        qCritical().noquote() << jsonError;
        return 1;
    }

    QString rawError;
    const QString rawPath = !parser.value(cdtOption).isEmpty()
        ? parser.value(cdtOption)
        : parser.value(rawOption);
    writeRawBlobIfRequested(result.document, rawPath, rawError);
    if (!rawError.isEmpty()) {
        qCritical().noquote() << rawError;
        return 1;
    }

    QString sonyBinError;
    writeSonyBinIfRequested(result.document, parser.value(sonyBinOption), sonyBinError);
    if (!sonyBinError.isEmpty()) {
        qCritical().noquote() << sonyBinError;
        return 1;
    }

    qInfo().noquote() << result.toText();
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cdtext-diff"));
    QCoreApplication::setOrganizationName(QStringLiteral("Epicreds"));

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        qCritical().noquote() << QStringLiteral("Usage: cdtext-diff <parse|compare|export-current> [options]");
        return 1;
    }

    const QString command = args.at(1).trimmed().toLower();
    if (command == QStringLiteral("parse")) {
        return runParse(app, args.mid(2));
    }
    if (command == QStringLiteral("compare")) {
        return runCompare(app, args.mid(2));
    }
    if (command == QStringLiteral("export-current")) {
        return runExportCurrent(app, args.mid(2));
    }

    qCritical().noquote() << QStringLiteral("Unknown command: %1").arg(command);
    return 1;
}
