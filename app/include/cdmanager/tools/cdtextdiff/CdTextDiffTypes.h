#pragma once

#include <cstdint>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace cdmanager::tools::cdtextdiff {

enum class InputFormat {
    Cdt,
    PacksJson,
    SampleDump,
    ReferenceSample
};

enum class CompareMode {
    Exact,
    Structure,
    Schema
};

struct ParsedCdTextPack {
    QByteArray bytes;
    bool hasCrc {false};
    int sourceIndex {0};
    QString sourceLabel;

    bool isValid() const;
    std::uint8_t packType() const;
    std::uint8_t trackNumber() const;
    std::uint8_t sequenceNumber() const;
    std::uint8_t blockByte() const;
    std::uint8_t blockNumber() const;
    std::uint8_t characterPosition() const;
    QString packTypeLabel() const;
    QString byteHex() const;
    QString coreHex() const;
    QJsonObject toJson() const;
};

struct ParsedCdTextDocument {
    InputFormat format {InputFormat::Cdt};
    QString sourcePath;
    QVector<ParsedCdTextPack> packs;
    QStringList notes;
    bool reconstructedFromReferenceMetadata {false};

    bool hasAnyCrc() const;
    int packCount() const;
    int blockCount() const;
    QJsonObject toJson() const;
};

struct CdTextDiffByteDelta {
    int byteOffset {0};
    int leftValue {-1};
    int rightValue {-1};
};

struct CdTextDiffPackDelta {
    int packIndex {0};
    QString reason;
    ParsedCdTextPack left;
    ParsedCdTextPack right;
    QVector<CdTextDiffByteDelta> byteDeltas;

    bool identical() const;
    QJsonObject toJson() const;
};

struct CdTextDiffReport {
    ParsedCdTextDocument left;
    ParsedCdTextDocument right;
    CompareMode mode {CompareMode::Exact};
    bool identical {false};
    QVector<CdTextDiffPackDelta> packDeltas;
    QStringList documentNotes;

    QJsonObject toJson() const;
    QString toText() const;
};

QString inputFormatName(InputFormat format);
QString inputFormatDescription(InputFormat format);
QString compareModeName(CompareMode mode);
QJsonArray packsToJson(const QVector<ParsedCdTextPack>& packs);

}  // namespace cdmanager::tools::cdtextdiff
