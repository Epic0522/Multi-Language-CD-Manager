#include "cdmanager/tools/cdtextdiff/CdTextDiffTypes.h"

#include <algorithm>

#include <QJsonDocument>
#include <QJsonValue>

namespace cdmanager::tools::cdtextdiff {

namespace {

QString hexByte(int value) {
    if (value < 0) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QJsonObject packTypeCountsJson(const ParsedCdTextDocument& document) {
    QMap<QString, int> counts;
    for (const auto& pack : document.packs) {
        const QString key = QStringLiteral("%1(0x%2)")
            .arg(pack.packTypeLabel())
            .arg(pack.packType(), 2, 16, QLatin1Char('0'))
            .toUpper();
        counts[key] += 1;
    }

    QJsonObject object;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        object.insert(it.key(), it.value());
    }
    return object;
}

QJsonArray blockSummariesJson(const ParsedCdTextDocument& document) {
    struct BlockSummary {
        int packCount = 0;
        int minSequence = 255;
        int maxSequence = -1;
        QMap<QString, int> typeCounts;
    };

    QMap<int, BlockSummary> summaries;
    for (const auto& pack : document.packs) {
        auto& summary = summaries[pack.blockNumber()];
        summary.packCount += 1;
        summary.minSequence = std::min(summary.minSequence, static_cast<int>(pack.sequenceNumber()));
        summary.maxSequence = std::max(summary.maxSequence, static_cast<int>(pack.sequenceNumber()));
        const QString key = QStringLiteral("%1(0x%2)")
            .arg(pack.packTypeLabel())
            .arg(pack.packType(), 2, 16, QLatin1Char('0'))
            .toUpper();
        summary.typeCounts[key] += 1;
    }

    QJsonArray array;
    for (auto it = summaries.cbegin(); it != summaries.cend(); ++it) {
        QJsonObject typeCounts;
        for (auto typeIt = it->typeCounts.cbegin(); typeIt != it->typeCounts.cend(); ++typeIt) {
            typeCounts.insert(typeIt.key(), typeIt.value());
        }

        array.append(QJsonObject{
            {QStringLiteral("blockNumber"), it.key()},
            {QStringLiteral("packCount"), it->packCount},
            {QStringLiteral("sequenceMin"), it->minSequence == 255 ? -1 : it->minSequence},
            {QStringLiteral("sequenceMax"), it->maxSequence},
            {QStringLiteral("packTypeCounts"), typeCounts},
        });
    }
    return array;
}

QJsonObject sequenceSummaryJson(const ParsedCdTextDocument& document) {
    if (document.packs.isEmpty()) {
        return QJsonObject{
            {QStringLiteral("min"), -1},
            {QStringLiteral("max"), -1},
        };
    }

    int minSequence = 255;
    int maxSequence = -1;
    for (const auto& pack : document.packs) {
        minSequence = std::min(minSequence, static_cast<int>(pack.sequenceNumber()));
        maxSequence = std::max(maxSequence, static_cast<int>(pack.sequenceNumber()));
    }

    return QJsonObject{
        {QStringLiteral("min"), minSequence},
        {QStringLiteral("max"), maxSequence},
    };
}

QString packTypeCountsText(const ParsedCdTextDocument& document) {
    const auto object = packTypeCountsJson(document);
    QStringList parts;
    const auto keys = object.keys();
    for (const auto& key : keys) {
        parts.append(QStringLiteral("%1=%2").arg(key, QString::number(object.value(key).toInt())));
    }
    return parts.join(QStringLiteral(", "));
}

QString blockSummariesText(const ParsedCdTextDocument& document) {
    const auto array = blockSummariesJson(document);
    QStringList parts;
    for (const auto& value : array) {
        const auto block = value.toObject();
        QStringList typeParts;
        const auto typeObject = block.value(QStringLiteral("packTypeCounts")).toObject();
        const auto typeKeys = typeObject.keys();
        for (const auto& key : typeKeys) {
            typeParts.append(QStringLiteral("%1=%2").arg(key, QString::number(typeObject.value(key).toInt())));
        }
        parts.append(QStringLiteral("block%1 packs=%2 seq=%3..%4 [%5]")
                         .arg(block.value(QStringLiteral("blockNumber")).toInt())
                         .arg(block.value(QStringLiteral("packCount")).toInt())
                         .arg(block.value(QStringLiteral("sequenceMin")).toInt())
                         .arg(block.value(QStringLiteral("sequenceMax")).toInt())
                         .arg(typeParts.join(QStringLiteral(", "))));
    }
    return parts.join(QStringLiteral(" | "));
}

}  // namespace

bool ParsedCdTextPack::isValid() const {
    return bytes.size() >= 4;
}

std::uint8_t ParsedCdTextPack::packType() const {
    return isValid() ? static_cast<std::uint8_t>(bytes[0]) : 0;
}

std::uint8_t ParsedCdTextPack::trackNumber() const {
    return bytes.size() >= 2 ? static_cast<std::uint8_t>(bytes[1]) : 0;
}

std::uint8_t ParsedCdTextPack::sequenceNumber() const {
    return bytes.size() >= 3 ? static_cast<std::uint8_t>(bytes[2]) : 0;
}

std::uint8_t ParsedCdTextPack::blockByte() const {
    return bytes.size() >= 4 ? static_cast<std::uint8_t>(bytes[3]) : 0;
}

std::uint8_t ParsedCdTextPack::blockNumber() const {
    return static_cast<std::uint8_t>((blockByte() >> 4) & 0x0F);
}

std::uint8_t ParsedCdTextPack::characterPosition() const {
    return static_cast<std::uint8_t>(blockByte() & 0x0F);
}

QString ParsedCdTextPack::packTypeLabel() const {
    switch (packType()) {
        case 0x80:
            return QStringLiteral("TITLE");
        case 0x81:
            return QStringLiteral("PERFORMER");
        case 0x82:
            return QStringLiteral("SONGWRITER");
        case 0x83:
            return QStringLiteral("COMPOSER");
        case 0x84:
            return QStringLiteral("ARRANGER");
        case 0x85:
            return QStringLiteral("MESSAGE");
        case 0x86:
            return QStringLiteral("DISC_ID");
        case 0x87:
            return QStringLiteral("GENRE");
        case 0x8E:
            return QStringLiteral("UPC_ISRC");
        case 0x8F:
            return QStringLiteral("SIZE_INFO");
        default:
            return QStringLiteral("0x%1").arg(packType(), 2, 16, QLatin1Char('0')).toUpper();
    }
}

QString ParsedCdTextPack::byteHex() const {
    return QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

QString ParsedCdTextPack::coreHex() const {
    return QString::fromLatin1(bytes.left(16).toHex(' ')).toUpper();
}

QJsonObject ParsedCdTextPack::toJson() const {
    QJsonArray byteArray;
    for (const auto byte : bytes) {
        byteArray.append(static_cast<int>(static_cast<unsigned char>(byte)));
    }

    return QJsonObject{
        {QStringLiteral("sourceIndex"), sourceIndex},
        {QStringLiteral("sourceLabel"), sourceLabel},
        {QStringLiteral("hasCrc"), hasCrc},
        {QStringLiteral("packType"), static_cast<int>(packType())},
        {QStringLiteral("packTypeLabel"), packTypeLabel()},
        {QStringLiteral("trackNumber"), static_cast<int>(trackNumber())},
        {QStringLiteral("sequenceNumber"), static_cast<int>(sequenceNumber())},
        {QStringLiteral("blockByte"), static_cast<int>(blockByte())},
        {QStringLiteral("blockNumber"), static_cast<int>(blockNumber())},
        {QStringLiteral("characterPosition"), static_cast<int>(characterPosition())},
        {QStringLiteral("rawHex"), QString::fromLatin1(bytes.toHex()).toUpper()},
        {QStringLiteral("bytes"), byteArray},
    };
}

bool ParsedCdTextDocument::hasAnyCrc() const {
    for (const auto& pack : packs) {
        if (pack.hasCrc) {
            return true;
        }
    }
    return false;
}

int ParsedCdTextDocument::packCount() const {
    return packs.size();
}

int ParsedCdTextDocument::blockCount() const {
    if (packs.isEmpty()) {
        return 0;
    }

    int maxBlockNumber = -1;
    for (const auto& pack : packs) {
        maxBlockNumber = std::max(maxBlockNumber, static_cast<int>(pack.blockNumber()));
    }
    return maxBlockNumber + 1;
}

QJsonObject ParsedCdTextDocument::toJson() const {
    QJsonArray noteArray;
    for (const auto& note : notes) {
        noteArray.append(note);
    }

    return QJsonObject{
        {QStringLiteral("format"), inputFormatName(format)},
        {QStringLiteral("formatDescription"), inputFormatDescription(format)},
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("packCount"), packCount()},
        {QStringLiteral("blockCount"), blockCount()},
        {QStringLiteral("hasAnyCrc"), hasAnyCrc()},
        {QStringLiteral("reconstructedFromReferenceMetadata"), reconstructedFromReferenceMetadata},
        {QStringLiteral("sequenceSummary"), sequenceSummaryJson(*this)},
        {QStringLiteral("packTypeCounts"), packTypeCountsJson(*this)},
        {QStringLiteral("blockSummaries"), blockSummariesJson(*this)},
        {QStringLiteral("notes"), noteArray},
        {QStringLiteral("packs"), packsToJson(packs)},
    };
}

bool CdTextDiffPackDelta::identical() const {
    return reason.isEmpty() && byteDeltas.isEmpty();
}

QJsonObject CdTextDiffPackDelta::toJson() const {
    QJsonArray deltaArray;
    for (const auto& delta : byteDeltas) {
        deltaArray.append(QJsonObject{
            {QStringLiteral("byteOffset"), delta.byteOffset},
            {QStringLiteral("leftValue"), delta.leftValue},
            {QStringLiteral("rightValue"), delta.rightValue},
        });
    }

    return QJsonObject{
        {QStringLiteral("packIndex"), packIndex},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("left"), left.toJson()},
        {QStringLiteral("right"), right.toJson()},
        {QStringLiteral("byteDeltas"), deltaArray},
    };
}

QJsonObject CdTextDiffReport::toJson() const {
    QJsonArray noteArray;
    for (const auto& note : documentNotes) {
        noteArray.append(note);
    }

    QJsonArray deltaArray;
    for (const auto& delta : packDeltas) {
        deltaArray.append(delta.toJson());
    }

    return QJsonObject{
        {QStringLiteral("compareMode"), compareModeName(mode)},
        {QStringLiteral("identical"), identical},
        {QStringLiteral("notes"), noteArray},
        {QStringLiteral("left"), left.toJson()},
        {QStringLiteral("right"), right.toJson()},
        {QStringLiteral("packDeltas"), deltaArray},
    };
}

QString CdTextDiffReport::toText() const {
    QStringList lines;
    lines.append(QStringLiteral("CD-TEXT compare result (%1): %2")
                     .arg(compareModeName(mode))
                     .arg(identical ? QStringLiteral("identical")
                                    : QStringLiteral("different")));
    lines.append(QStringLiteral("Left : %1 (%2 packs)")
                     .arg(left.sourcePath, QString::number(left.packCount())));
    lines.append(QStringLiteral("Right: %1 (%2 packs)")
                     .arg(right.sourcePath, QString::number(right.packCount())));
    lines.append(QStringLiteral("Left fingerprint : %1").arg(packTypeCountsText(left)));
    lines.append(QStringLiteral("Right fingerprint: %1").arg(packTypeCountsText(right)));
    lines.append(QStringLiteral("Left blocks : %1").arg(blockSummariesText(left)));
    lines.append(QStringLiteral("Right blocks: %1").arg(blockSummariesText(right)));

    for (const auto& note : documentNotes) {
        lines.append(QStringLiteral("Note : %1").arg(note));
    }

    if (identical) {
        return lines.join(u'\n');
    }

    for (const auto& delta : packDeltas) {
        lines.append(QStringLiteral("--- pack %1 ---").arg(delta.packIndex));
        if (!delta.reason.isEmpty()) {
            lines.append(QStringLiteral("Reason: %1").arg(delta.reason));
        }
        lines.append(QStringLiteral("Left : %1").arg(delta.left.byteHex()));
        lines.append(QStringLiteral("Right: %1").arg(delta.right.byteHex()));
        if (!delta.byteDeltas.isEmpty()) {
            for (const auto& byteDelta : delta.byteDeltas) {
                lines.append(QStringLiteral("  byte[%1]: %2 -> %3")
                                 .arg(byteDelta.byteOffset)
                                 .arg(hexByte(byteDelta.leftValue))
                                 .arg(hexByte(byteDelta.rightValue)));
            }
        }
    }

    return lines.join(u'\n');
}

QString inputFormatName(InputFormat format) {
    switch (format) {
        case InputFormat::Cdt:
            return QStringLiteral("cdt");
        case InputFormat::PacksJson:
            return QStringLiteral("packs-json");
        case InputFormat::SampleDump:
            return QStringLiteral("sample-dump");
        case InputFormat::ReferenceSample:
            return QStringLiteral("reference-sample");
    }
    return QStringLiteral("unknown");
}

QString inputFormatDescription(InputFormat format) {
    switch (format) {
        case InputFormat::Cdt:
            return QStringLiteral("Compiled CD-TEXT binary or raw pack blob");
        case InputFormat::PacksJson:
            return QStringLiteral("CDManager exported pack JSON");
        case InputFormat::SampleDump:
            return QStringLiteral("Legacy CCD/CDM CDText dump");
        case InputFormat::ReferenceSample:
            return QStringLiteral("Captured reference sample directory rebuilt from parsed metadata, not direct raw lead-in packs");
    }
    return QStringLiteral("Unknown");
}

QString compareModeName(CompareMode mode) {
    switch (mode) {
        case CompareMode::Exact:
            return QStringLiteral("exact");
        case CompareMode::Structure:
            return QStringLiteral("structure");
        case CompareMode::Schema:
            return QStringLiteral("schema");
    }
    return QStringLiteral("unknown");
}

QJsonArray packsToJson(const QVector<ParsedCdTextPack>& packs) {
    QJsonArray array;
    for (const auto& pack : packs) {
        array.append(pack.toJson());
    }
    return array;
}

}  // namespace cdmanager::tools::cdtextdiff
