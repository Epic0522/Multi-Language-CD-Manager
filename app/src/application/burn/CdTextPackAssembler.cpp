#include "cdmanager/application/burn/CdTextPackAssembler.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <QByteArray>
#include <QString>

#include "cdmanager/domain/cdtext/CdTextField.h"

namespace cdmanager::application::burn {

// ---------- CRC-16 CCITT (polynomial 0x1021, initial 0x0000) ----------

namespace {

std::uint16_t computeCrc16(const std::uint8_t* data, std::size_t length) {
    std::uint16_t crc = 0x0000;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

void writePackCrc(CdTextPack& pack) {
    const std::uint16_t crc = computeCrc16(pack.data.data(), kPackDataSize + 4);
    pack.data[16] = static_cast<std::uint8_t>(crc >> 8);
    pack.data[17] = static_cast<std::uint8_t>(crc & 0xFF);
}

// ---------- Pack type / character code mapping ----------

std::uint8_t packTypeForFieldId(cdmanager::domain::cdtext::CdTextFieldId id) {
    switch (id) {
        case cdmanager::domain::cdtext::CdTextFieldId::AlbumTitle:
        case cdmanager::domain::cdtext::CdTextFieldId::TrackTitle:
            return kPackTypeTitle;
        case cdmanager::domain::cdtext::CdTextFieldId::AlbumArtist:
        case cdmanager::domain::cdtext::CdTextFieldId::TrackArtist:
            return kPackTypePerformer;
    }
    return kPackTypeTitle;
}

std::uint8_t languageCodeFor(cdmanager::domain::cdtext::CdTextLanguage language) {
    switch (language) {
        case cdmanager::domain::cdtext::CdTextLanguage::Latin:
            return 0x09;  // English
        case cdmanager::domain::cdtext::CdTextLanguage::Japanese:
            return 0x69;  // Japanese
    }
    return 0x00;
}

// ---------- Field collection ----------

struct CollectedField {
    cdmanager::domain::cdtext::CdTextFieldId id {};
    std::optional<int> trackNumber;
    QByteArray encodedBytes;
    bool emitWhenEmpty {false};
};

QVector<CollectedField> collectWritableFields(
    const CdTextWritePayload& payload,
    cdmanager::domain::cdtext::CdTextLanguage language
) {
    QVector<CollectedField> result;

    for (const auto& f : payload.albumWritableFields) {
        if (f.preparedField.field.language == language) {
            result.append({
                f.preparedField.field.id,
                f.preparedField.field.trackNumber,
                f.preparedField.encodedBytes,
                false,
            });
        }
    }
    for (const auto& track : payload.tracks) {
        bool hasWritableTrackArtist = false;
        for (const auto& f : track.writableFields) {
            if (f.preparedField.field.language == language) {
                result.append({
                    f.preparedField.field.id,
                    f.preparedField.field.trackNumber,
                    f.preparedField.encodedBytes,
                    false,
                });
                if (f.preparedField.field.id == cdmanager::domain::cdtext::CdTextFieldId::TrackArtist) {
                    hasWritableTrackArtist = true;
                }
            }
        }

        if (language == cdmanager::domain::cdtext::CdTextLanguage::Japanese && !hasWritableTrackArtist) {
            for (const auto& skipped : track.skippedFields) {
                if (skipped.preparedField.field.language != language
                    || skipped.preparedField.field.id != cdmanager::domain::cdtext::CdTextFieldId::TrackArtist) {
                    continue;
                }
                result.append({
                    skipped.preparedField.field.id,
                    skipped.preparedField.field.trackNumber,
                    QByteArray(),
                    true,
                });
                break;
            }
        }
    }

    return result;
}

// ---------- Block assembly ----------

struct BlockAssembly {
    QVector<CdTextPack> dataPacks;
    std::array<std::uint8_t, 16> typePackCounts {};
    int firstTrack = 99;
    int lastTrack = 0;
    std::uint8_t characterCode {kCharCodeIso88591};
    std::uint8_t languageCode {0x00};
};

BlockAssembly buildBlock(const QVector<CollectedField>& fields,
                         cdmanager::domain::cdtext::CdTextLanguage language,
                         std::uint8_t characterCode) {
    BlockAssembly block;
    block.characterCode = characterCode;
    block.languageCode = languageCodeFor(language);

    int packNumber = 0;

    const int terminatorSize = language == cdmanager::domain::cdtext::CdTextLanguage::Japanese ? 2 : 1;

    const auto appendFieldPacks = [&](std::uint8_t expectedPackType) {
        CdTextPack currentPack;
        bool hasOpenPack = false;
        int payloadOffset = 0;
        int packCountBeforeType = block.dataPacks.size();

        const auto flushCurrentPack = [&]() {
            if (!hasOpenPack) {
                return;
            }
            writePackCrc(currentPack);
            block.dataPacks.append(currentPack);
            ++packNumber;
            hasOpenPack = false;
            payloadOffset = 0;
        };

        for (const auto& cf : fields) {
            const QByteArray& bytes = cf.encodedBytes;
            if ((!cf.emitWhenEmpty && bytes.isEmpty()) || packTypeForFieldId(cf.id) != expectedPackType) {
                continue;
            }

            const std::uint8_t trackNum = static_cast<std::uint8_t>(
                cf.trackNumber.value_or(0)
            );

            if (trackNum > 0) {
                if (trackNum < block.firstTrack) block.firstTrack = trackNum;
                if (trackNum > block.lastTrack) block.lastTrack = trackNum;
            }

            QByteArray terminated = bytes;
            terminated.append(QByteArray(terminatorSize, '\0'));

            int offset = 0;
            while (offset < terminated.size()) {
                if (!hasOpenPack) {
                    currentPack = {};
                    currentPack.data[0] = expectedPackType;
                    currentPack.data[1] = trackNum;
                    currentPack.data[2] = static_cast<std::uint8_t>(packNumber);
                    currentPack.data[3] = static_cast<std::uint8_t>(offset & 0x0F);
                    hasOpenPack = true;
                    payloadOffset = 0;
                }

                const int remainingPayload = kPackDataSize - payloadOffset;
                const int remainingFieldBytes = terminated.size() - offset;
                const int chunkSize = std::min(remainingPayload, remainingFieldBytes);
                std::memcpy(&currentPack.data[4 + payloadOffset], terminated.constData() + offset,
                            static_cast<std::size_t>(chunkSize));
                payloadOffset += chunkSize;
                offset += chunkSize;

                if (payloadOffset == kPackDataSize) {
                    flushCurrentPack();
                }
            }
        }

        flushCurrentPack();
        block.typePackCounts.at(expectedPackType - 0x80)
            = static_cast<std::uint8_t>(block.dataPacks.size() - packCountBeforeType);
    };

    appendFieldPacks(kPackTypeTitle);
    appendFieldPacks(kPackTypePerformer);

    if (block.firstTrack > block.lastTrack) {
        block.firstTrack = 1;
        block.lastTrack = 1;
    }

    return block;
}

QVector<CdTextPack> makeSizeInfoPacks(const QByteArray& summaryBytes,
                                      int summaryStartPackNumber,
                                      int blockNumber,
                                      const std::array<std::uint8_t, 8>& blockLast,
                                      const std::array<std::uint8_t, 8>& blockLanguage) {
    Q_UNUSED(blockLast);
    Q_UNUSED(blockLanguage);

    QVector<CdTextPack> packs;
    packs.reserve(3);

    for (int index = 0; index < 3; ++index) {
        CdTextPack pack;
        pack.data[0] = kPackTypeSizeInfo;
        pack.data[1] = static_cast<std::uint8_t>(index);
        pack.data[2] = static_cast<std::uint8_t>(summaryStartPackNumber + index);
        pack.data[3] = static_cast<std::uint8_t>((blockNumber << 4) & 0xF0);
        std::memcpy(&pack.data[4], summaryBytes.constData() + (index * kPackDataSize), kPackDataSize);
        writePackCrc(pack);
        packs.append(pack);
    }

    return packs;
}

QByteArray buildSizeInfoSummary(const BlockAssembly& block,
                                const std::array<std::uint8_t, 8>& blockLast,
                                const std::array<std::uint8_t, 8>& blockLanguage) {
    QByteArray summary(36, '\0');
    summary[0] = static_cast<char>(block.characterCode);
    summary[1] = static_cast<char>(block.firstTrack);
    summary[2] = static_cast<char>(block.lastTrack);
    summary[3] = 0;

    for (int index = 0; index < 16; ++index) {
        std::uint8_t count = block.typePackCounts.at(index);
        if (index == (kPackTypeSizeInfo - 0x80)) {
            count = 3;
        }
        summary[4 + index] = static_cast<char>(count);
    }

    for (int index = 0; index < 8; ++index) {
        summary[20 + index] = static_cast<char>(blockLast.at(index));
        summary[28 + index] = static_cast<char>(blockLanguage.at(index));
    }

    return summary;
}

}  // namespace

// ---------- Public API ----------

CdTextPackAssembly CdTextPackAssembler::assemble(const CdTextWritePayload& payload) const {
    CdTextPackAssembly assembly;

    auto latinFields = collectWritableFields(
        payload, cdmanager::domain::cdtext::CdTextLanguage::Latin
    );
    const auto japaneseFields = collectWritableFields(
        payload, cdmanager::domain::cdtext::CdTextLanguage::Japanese
    );

    // Match the known-good single-block Japanese discs:
    // when real Japanese CD-TEXT exists, do not prepend a Latin placeholder block.
    // Older players may stop at the first empty Latin block and never reach the
    // following Japanese block, which is exactly the failure pattern seen on disc 4.
    if (!japaneseFields.isEmpty()) {
        latinFields.clear();
    }

    QVector<BlockAssembly> blocks;

    auto emitBlock = [&](const QVector<CollectedField>& fields,
                         cdmanager::domain::cdtext::CdTextLanguage language,
                         std::uint8_t charCode) {
        if (fields.isEmpty()) return;
        blocks.append(buildBlock(fields, language, charCode));
    };

    emitBlock(latinFields, cdmanager::domain::cdtext::CdTextLanguage::Latin, kCharCodeIso88591);
    emitBlock(japaneseFields, cdmanager::domain::cdtext::CdTextLanguage::Japanese, kCharCodeMsJis);

    std::array<std::uint8_t, 8> blockLast {};
    std::array<std::uint8_t, 8> blockLanguage {};

    int packCursor = 0;
    for (int blockIndex = 0; blockIndex < blocks.size() && blockIndex < 8; ++blockIndex) {
        const auto& block = blocks.at(blockIndex);
        packCursor += block.dataPacks.size() + 3;
        blockLast.at(blockIndex) = static_cast<std::uint8_t>(packCursor - 1);
        blockLanguage.at(blockIndex) = block.languageCode;
    }

    for (int blockIndex = 0; blockIndex < blocks.size() && blockIndex < 8; ++blockIndex) {
        const auto& block = blocks.at(blockIndex);
        for (auto pack : block.dataPacks) {
            pack.data[3] = static_cast<std::uint8_t>(((blockIndex & 0x0F) << 4) | (pack.data[3] & 0x0F));
            assembly.packs.append(pack);
        }

        const QByteArray summary = buildSizeInfoSummary(block, blockLast, blockLanguage);
        const int summaryStartPackNumber = block.dataPacks.size();
        assembly.packs += makeSizeInfoPacks(summary, summaryStartPackNumber, blockIndex, blockLast, blockLanguage);
    }

    return assembly;
}

// ---------- CdTextPack accessors ----------

std::uint8_t CdTextPack::packType() const {
    return data[0];
}

std::uint8_t CdTextPack::trackNumber() const {
    return data[1];
}

std::uint8_t CdTextPack::sequenceNumber() const {
    return data[2];
}

std::uint8_t CdTextPack::blockNumber() const {
    return static_cast<std::uint8_t>((data[3] >> 4) & 0x0F);
}

std::uint8_t CdTextPack::characterPosition() const {
    return static_cast<std::uint8_t>(data[3] & 0x0F);
}

QString CdTextPack::diagnosticString() const {
    QString typeLabel;
    switch (packType()) {
        case kPackTypeTitle:
            typeLabel = QStringLiteral("TITLE   ");
            break;
        case kPackTypePerformer:
            typeLabel = QStringLiteral("PERFORMER");
            break;
        case kPackTypeSizeInfo:
            typeLabel = QStringLiteral("SIZEINFO");
            break;
        default:
            typeLabel = QStringLiteral("0x%1    ")
                .arg(packType(), 2, 16, QLatin1Char('0'));
            break;
    }

    const QByteArray textBytes(
        reinterpret_cast<const char*>(&data[4]), kPackDataSize
    );
    const QString hex = QString::fromLatin1(textBytes.toHex(' ')).toUpper();

    return QStringLiteral("%1  track=%2  seq=%3  blk=%4  cpos=%5  %6")
        .arg(typeLabel)
        .arg(trackNumber(), 2, 10, QLatin1Char('0'))
        .arg(sequenceNumber(), 2, 10, QLatin1Char('0'))
        .arg(blockNumber())
        .arg(characterPosition())
        .arg(hex);
}

// ---------- CdTextPackAssembly ----------

int CdTextPackAssembly::packCount() const {
    return packs.size();
}

int CdTextPackAssembly::totalByteCount() const {
    return packs.size() * kPackTotalSize;
}

QString CdTextPackAssembly::diagnosticSummary() const {
    int sizeInfoPackCount = 0;
    for (const auto& pack : packs) {
        if (pack.packType() == kPackTypeSizeInfo) {
            ++sizeInfoPackCount;
        }
    }
    const int blockCount = sizeInfoPackCount > 0
        ? (sizeInfoPackCount + 2) / 3
        : 0;
    return QStringLiteral(
        "CD-TEXT Pack Assembly: %1 packs, %2 bytes, %3 block(s)"
    )
        .arg(packCount())
        .arg(totalByteCount())
        .arg(blockCount);
}

QString CdTextPackAssembly::diagnosticDetail() const {
    QString detail;
    for (const auto& pack : packs) {
        detail += pack.diagnosticString() + QStringLiteral("\n");
    }
    return detail;
}

}  // namespace cdmanager::application::burn
