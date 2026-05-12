#include "cdmanager/tools/cdtextdiff/CdTextDocumentParser.h"

#include <algorithm>

#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include "cdmanager/tools/cdtextdiff/CurrentProjectPackExporter.h"

namespace cdmanager::tools::cdtextdiff {

namespace {

ParseResult makeError(const QString& message) {
    ParseResult result;
    result.ok = false;
    result.errorMessage = message;
    return result;
}

QByteArray parseHexBytes(const QString& text, QString& error) {
    const QString collapsed = text.simplified().remove(u' ');
    if (collapsed.size() % 2 != 0) {
        error = QStringLiteral("Hex string length must be even.");
        return {};
    }

    const QByteArray bytes = QByteArray::fromHex(collapsed.toLatin1());
    if (bytes.isEmpty() && !collapsed.isEmpty()) {
        error = QStringLiteral("Failed to decode hex string.");
    }
    return bytes;
}

}  // namespace

ParseResult CdTextDocumentParser::parseFile(const QString& path, InputFormat format) const {
    if (format == InputFormat::ReferenceSample) {
        return parseReferenceSample(path);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeError(QStringLiteral("Failed to open file: %1").arg(path));
    }

    auto result = parseBytes(file.readAll(), format, path);
    if (result.ok) {
        result.document.sourcePath = path;
    }
    return result;
}

ParseResult CdTextDocumentParser::parseBytes(const QByteArray& bytes,
                                             InputFormat format,
                                             const QString& sourceLabel) const {
    switch (format) {
        case InputFormat::Cdt:
            return parseCdt(bytes, sourceLabel);
        case InputFormat::PacksJson:
            return parsePacksJson(bytes, sourceLabel);
        case InputFormat::SampleDump:
            return parseSampleDump(bytes, sourceLabel);
        case InputFormat::ReferenceSample:
            return makeError(QStringLiteral("Reference sample parsing requires a directory path, not raw bytes."));
    }

    return makeError(QStringLiteral("Unsupported input format."));
}

ParseResult CdTextDocumentParser::parseCdt(const QByteArray& bytes, const QString& sourceLabel) const {
    if (bytes.isEmpty()) {
        return makeError(QStringLiteral("CDT input is empty."));
    }

    ParsedCdTextDocument document;
    document.format = InputFormat::Cdt;
    document.sourcePath = sourceLabel;

    int packSize = 0;
    bool hasCrc = false;

    if (bytes.size() % 18 == 0) {
        packSize = 18;
        hasCrc = true;
        document.notes.append(QStringLiteral("Input length is divisible by 18; treating as full CD-TEXT packs with CRC."));
    } else if (bytes.size() % 16 == 0) {
        packSize = 16;
        hasCrc = false;
        document.notes.append(QStringLiteral("Input length is divisible by 16 only; treating as CRC-less raw pack core data."));
    } else {
        return makeError(QStringLiteral("CDT/raw input length %1 is not divisible by 18 or 16.")
                             .arg(bytes.size()));
    }

    const int packCount = bytes.size() / packSize;
    document.packs.reserve(packCount);

    for (int index = 0; index < packCount; ++index) {
        ParsedCdTextPack pack;
        pack.bytes = bytes.mid(index * packSize, packSize);
        pack.hasCrc = hasCrc;
        pack.sourceIndex = index;
        pack.sourceLabel = QStringLiteral("pack %1").arg(index);
        document.packs.append(pack);
    }

    ParseResult result;
    result.ok = true;
    result.document = document;
    return result;
}

ParseResult CdTextDocumentParser::parsePacksJson(const QByteArray& bytes, const QString& sourceLabel) const {
    QJsonParseError jsonError;
    const auto documentJson = QJsonDocument::fromJson(bytes, &jsonError);
    if (documentJson.isNull()) {
        return makeError(QStringLiteral("Invalid JSON: %1").arg(jsonError.errorString()));
    }

    const QJsonObject root = documentJson.object();
    QJsonArray packArray;

    if (root.contains(QStringLiteral("packAssembly"))) {
        packArray = root.value(QStringLiteral("packAssembly")).toObject().value(QStringLiteral("packs")).toArray();
    } else if (root.contains(QStringLiteral("packs"))) {
        packArray = root.value(QStringLiteral("packs")).toArray();
    } else {
        return makeError(QStringLiteral("JSON does not contain packAssembly.packs or packs."));
    }

    ParsedCdTextDocument document;
    document.format = InputFormat::PacksJson;
    document.sourcePath = sourceLabel;

    for (int index = 0; index < packArray.size(); ++index) {
        const QJsonObject packObject = packArray.at(index).toObject();
        QString error;
        QByteArray rawBytes;

        if (packObject.contains(QStringLiteral("rawHex"))) {
            rawBytes = parseHexBytes(packObject.value(QStringLiteral("rawHex")).toString(), error);
        } else {
            const auto byteValues = packObject.value(QStringLiteral("bytes")).toArray();
            for (const auto& value : byteValues) {
                rawBytes.append(static_cast<char>(value.toInt()));
            }
        }

        if (!error.isEmpty()) {
            return makeError(QStringLiteral("Pack %1 hex decode failed: %2").arg(index).arg(error));
        }

        ParsedCdTextPack pack;
        pack.bytes = rawBytes;
        pack.hasCrc = rawBytes.size() >= 18;
        pack.sourceIndex = packObject.value(QStringLiteral("index")).toInt(index);
        pack.sourceLabel = packObject.value(QStringLiteral("label")).toString(QStringLiteral("pack %1").arg(index));
        document.packs.append(pack);
    }

    document.notes.append(QStringLiteral("Parsed CDManager pack export JSON."));

    ParseResult result;
    result.ok = true;
    result.document = document;
    return result;
}

ParseResult CdTextDocumentParser::parseSampleDump(const QByteArray& bytes, const QString& sourceLabel) const {
    const QString text = QString::fromUtf8(bytes);
    const QStringList lines = text.split(u'\n');
    const QRegularExpression entryPattern(QStringLiteral(R"(^Entry\s+(\d+)\s*=\s*(.+)$)"));

    bool inCdTextSection = false;
    ParsedCdTextDocument document;
    document.format = InputFormat::SampleDump;
    document.sourcePath = sourceLabel;

    for (QString line : lines) {
        line = line.trimmed();
        if (line == QStringLiteral("[CDText]")) {
            inCdTextSection = true;
            continue;
        }
        if (inCdTextSection && line.startsWith(u'[')) {
            break;
        }
        if (!inCdTextSection) {
            continue;
        }

        const auto match = entryPattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }

        QString error;
        const QByteArray rawBytes = parseHexBytes(match.captured(2), error);
        if (!error.isEmpty()) {
            return makeError(QStringLiteral("Failed to decode sample dump entry %1: %2")
                                 .arg(match.captured(1), error));
        }

        ParsedCdTextPack pack;
        pack.bytes = rawBytes;
        pack.hasCrc = false;
        pack.sourceIndex = match.captured(1).toInt();
        pack.sourceLabel = QStringLiteral("Entry %1").arg(pack.sourceIndex);
        document.packs.append(pack);
    }

    if (document.packs.isEmpty()) {
        return makeError(QStringLiteral("No [CDText] entries found in sample dump."));
    }

    document.notes.append(QStringLiteral("Sample dump entries usually omit CRC bytes; compare core 16-byte payloads first."));

    ParseResult result;
    result.ok = true;
    result.document = document;
    return result;
}

ParseResult CdTextDocumentParser::parseReferenceSample(const QString& path) const {
    QDir dir(path);
    if (!dir.exists()) {
        return makeError(QStringLiteral("Reference sample directory does not exist: %1").arg(path));
    }

    CurrentProjectPackExporter exporter;
    const auto exportResult = exporter.exportReferenceSampleDir(path);
    if (!exportResult.ok) {
        return makeError(exportResult.errorMessage);
    }

    ParseResult result;
    result.ok = true;
    result.document = exportResult.document;
    result.document.format = InputFormat::ReferenceSample;
    result.document.sourcePath = path;
    result.document.reconstructedFromReferenceMetadata = true;
    result.document.notes.append(QStringLiteral("Rebuilt current CDManager packs from captured reference sample directory."));
    result.document.notes.append(QStringLiteral("This reference input is reconstructed from project-local captured metadata files, not parsed from raw lead-in pack bytes."));
    result.document.notes.append(QStringLiteral("Use it as a pack-generation baseline, but do not treat an identical result as proof that every byte matches a hardware-read lead-in dump."));
    for (const auto& note : exportResult.referenceNotes) {
        result.document.notes.append(note);
    }
    for (const auto& note : exportResult.analysisNotes) {
        result.document.notes.append(QStringLiteral("Analysis: %1").arg(note));
    }
    return result;
}

}  // namespace cdmanager::tools::cdtextdiff
